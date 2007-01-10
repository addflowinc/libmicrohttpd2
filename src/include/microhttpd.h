/*
     This file is part of libmicrohttpd
     (C) 2006, 2007 Christian Grothoff (and other contributing authors)

     libmicrohttpd is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
     option) any later version.

     libmicrohttpd is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with libmicrohttpd; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/

/**
 * @file microhttpd.h
 * @brief public interface to libmicrohttpd
 * @author Christian Grothoff
 *
 * All symbols defined in this header start with MHD.  MHD is a
 * micro-httpd library.  As such, it does not have any API for logging
 * errors.<p>
 *
 * All functions are guaranteed to be completely reentrant and
 * thread-safe.<p>
 *
 * TODO:
 * - proper API for file uploads via HTTP
 * - We probably need a significantly more extensive API for
 *   proper SSL support (set local certificate, etc.)
 */

#ifndef MHD_MICROHTTPD_H
#define MHD_MICROHTTPD_H

#include <sys/types.h>
#include <sys/select.h>
#include <sys/socket.h>

#ifdef __cplusplus
extern "C" {
#if 0 /* keep Emacsens' auto-indent happy */
}
#endif
#endif

#define MHD_VERSION 0x00000000

#define MHD_YES 1

#define MHD_NO 0

#define MHD_HTTP_OK 200

#define MHD_HTTP_NOT_FOUND 404


/**
 * Options for the MHD daemon.  Note that if neither
 * MHD_USER_THREAD_PER_CONNECTION nor MHD_USE_SELECT_INTERNALLY are
 * used, the client wants control over the process and will call the
 * appropriate microhttpd callbacks.<p>
 *
 * Note that it is legal to specify that both IPv4 and IPv6
 * should be used.  However, if neither IPv4 nor IPv6 is
 * specified, starting the daemon will fail.<p>
 *
 * Starting the daemon may also fail if a particular option is not
 * implemented or not supported on the target platform (i.e. no
 * support for SSL, threads or IPv6).
 */
enum MHD_OPTION {
  /**
   * No options selected.
   */
  MHD_NO_OPTION = 0,

  /**
   * Run in debug mode.  If this flag is used, the
   * library should print error messages and warnings
   * to stderr.
   */
  MHD_USE_DEBUG = 1,

  /**
   * Run in https mode.
   */
  MHD_USE_SSL = 2,

  /**
   * Run using one thread per connection.
   */
  MHD_USE_THREAD_PER_CONNECTION = 4,  

  /**
   * Run using an internal thread doing SELECT.
   */
  MHD_USE_SELECT_INTERNALLY = 8,

  /**
   * Run using the IPv4 protocol
   */
  MHD_USE_IPv4 = 16,

  /**
   * Run using the IPv6 protocol
   */
  MHD_USE_IPv6 = 32,
};

/**
 * The MHD_ValueKind specifies the source of
 * the key-value pairs in the HTTP protocol.
 */
enum MHD_ValueKind {

  /**
   * Response header
   */
  MHD_RESPONSE_HEADER_KIND = 0;

  /**
   * HTTP header
   */
  MHD_HEADER_KIND = 1;

  /**
   * Cookies
   */
  MHD_COOKIE_KIND = 2;

  /**
   * POST data
   */
  MHD_POSTDATA_KIND = 4;

  /**
   * GET (URI) arguments
   */
  MHD_GET_ARGUMENT_KIND = 8;

};

struct MHD_Daemon;

struct MHD_Session;

struct MHD_Response;

/**
 * Allow or deny a client to connect.
 * 
 *
 * @param addr address information from the client
 * @param addrlen length of the address information
 * @return MHD_YES if connection is allowed, MHD_NO if not
 */
typedef int 
(*MHD_AcceptPolicyCallback)(void * cls,
			    const struct sockaddr * addr,
			    socklen_t addrlen);

/**
 * A client has requested the given url using the given method ("GET",
 * "PUT", "DELETE", "POST", etc).  The callback must call MHS
 * callbacks to provide content to give back to the client and return
 * an HTTP status code (i.e. 200 for OK, 404, etc.).
 *
 * @return MHS_YES if the connection was handled successfully,
 *         MHS_NO if the socket must be closed due to a serios
 *         error while handling the request
 */
typedef int
(*MHD_AccessHandlerCallback)(void * cls,
			     struct MHD_Session * session,
			     const char * url,
			     const char * method);

/**
 * Iterator over key-value pairs.  This iterator
 * can be used to iterate over all of the cookies,
 * headers, or POST-data fields of a request, and
 * also to iterate over the headers that have been
 * added to a response.
 *
 * @return MHD_YES to continue iterating, 
 *         MHD_NO to abort the iteration
 */
typedef int
(*MHD_KeyValueIterator)(void * cls,
			enum MHD_ValueKind kind,
			const char * key,
			const char * value);

/**
 * Callback used by libmicrohttpd in order to obtain content.  The
 * callback is to copy at most "max" bytes of content into "buf".  The
 * total number of bytes that has been placed into "buf" should be
 * returned.<p>
 *
 * Note that returning zero will cause libmicrohttpd to try again,
 * either "immediately" if in multi-threaded mode (in which case the
 * callback may want to do blocking operations) or in the next round
 * if MHD_run is used.  Returning 0 for a daemon that runs in internal
 * select mode is an error (since it would result in busy waiting) and
 * will cause the program to be aborted (abort()).
 *
 * @param cls extra argument to the callback
 * @param pos position in the datastream to access;
 *        note that if an MHD_Response object is re-used,
 *        it is possible for the same content reader to
 *        be queried multiple times for the same data;
 *        however, if an MHD_Response is not re-used,
 *        libmicrohttpd guarantees that "pos" will be
 *        the sum of all non-negative return values 
 *        obtained from the content reader so far.
 * @return -1 on error (libmicrohttpd will no longer
 *  try to read content and instead close the connection
 *  with the client). 
 */
typedef int
(*MHD_ContentReaderCallback)(void * cls,
			     size_t pos,
			     char * buf,
			     int max);

/**
 * This method is called by libmicrohttpd if we
 * are done with a content reader.  It should
 * be used to free resources associated with the
 * content reader.
 */
typedef void
(*MHD_ContentReaderFreeCallback)(void * cls);

/**
 * Start a webserver on the given port.
 * @param port port to bind to
 * @param apc callback to call to check which clients
 *        will be allowed to connect
 * @param apc_cls extra argument to apc
 * @param dh default handler for all URIs
 * @param dh_cls extra argument to dh
 * @return NULL on error, handle to daemon on success
 */
struct MHD_Daemon *
MHD_start_daemon(unsigned int options,
		 unsigned short port,
		 MHD_AcceptPolicyCallback apc,
		 void * apc_cls,
		 MHD_AccessHandlerCallback dh,
		 void * dh_cls);



/**
 * Shutdown an http daemon.
 */
void
MHD_stop_daemon(struct MHD_Daemon * daemon);


/**
 * Obtain the select sets for this daemon.
 *
 * @return MHD_YES on success, MHD_NO if this
 *         daemon was not started with the right
 *         options for this call.
 */
int 
MHD_get_fdset(struct MHD_Daemon * daemon,
	      fd_set * read_fd_set,
	      fd_set * write_fd_set,
	      fd_set * except_fd_set,
	      int * max_fd);

/**
 * Run webserver operations (without blocking unless
 * in client callbacks).  This method should be called
 * by clients in combination with MHD_get_fdset
 * if the client-controlled select method is used. 
 *
 * @return MHD_YES on success, MHD_NO if this
 *         daemon was not started with the right
 *         options for this call.
 */
int
MHD_run(struct MHD_Daemon * daemon);


/**
 * Register an access handler for all URIs beginning with uri_prefix.
 *
 * @param uri_prefix 
 * @return MRI_NO if a handler for this exact prefix
 *         already exists
 */
int 
MHD_register_handler(struct MHD_Daemon * daemon,
		     const char * uri_prefix,
		     MHD_AccessHandlerCallback dh,
		     void * dh_cls);

/**
 * Unregister an access handler for the URIs beginning with
 * uri_prefix.
 *
 * @param uri_prefix 
 * @return MHD_NO if a handler for this exact prefix
 *         is not known for this daemon
 */
int 
MHD_unregister_handler(struct MHD_Daemon * daemon,
		       const char * uri_prefix,
		       MHD_AccessHandlerCallback dh,
		       void * dh_cls);

/**
 * Get all of the headers from the request.
 *
 * @param iterator callback to call on each header;
 *        maybe NULL (then just count headers)
 * @param iterator_cls extra argument to iterator
 * @return number of entries iterated over
 */ 
int
MHD_get_session_values(struct MHD_Session * session,
		       enum MHD_ValueKind kind,
		       MHD_KeyValueIterator * iterator,
		       void * iterator_cls);

/**
 * Get a particular header value.  If multiple
 * values match the kind, return any one of them.
 *
 * @param key the header to look for
 * @return NULL if no such item was found
 */ 
const char *
MHD_lookup_session_value(struct MHD_Session * session,
			 enum MHD_ValueKind kind,
			 const char * key);

/**
 * Queue a response to be transmitted to the client (as soon as
 * possible).
 * 
 * @param session the session identifying the client
 * @param status_code HTTP status code (i.e. 200 for OK)
 * @param response response to transmit
 * @return MHD_NO on error (i.e. reply already sent),
 *         MHD_YES on success or if message has been queued
 */
int 
MHD_queue_response(struct MHD_Session * session,
		   unsigned int status_code,
		   struct MHD_Response * response);

	       
/**
 * Create a response object.  The response object can be extended with
 * header information and then be used any number of times.
 *
 * @param size size of the data portion of the response, -1 for unknown
 * @param crc callback to use to obtain response data
 * @param crc_cls extra argument to crc
 * @param crfc callback to call to free crc_cls resources
 * @return NULL on error (i.e. invalid arguments, out of memory)
 */
struct MHD_Response *
MHD_create_response_from_callback(size_t size,
				  MHD_ContentReaderCallback crc,
				  void * crc_cls,
				  MHD_ContentReaderFreeCallback crfc);

/**
 * Create a response object.  The response object can be extended with
 * header information and then be used any number of times.
 *
 * @param size size of the data portion of the response
 * @param data the data itself
 * @param must_free libmicrohttpd should free data when done
 * @param must_copy libmicrohttpd must make a copy of data 
 *        right away, the data maybe released anytime after
 *        this call returns
 * @return NULL on error (i.e. invalid arguments, out of memory)
 */
struct MHD_Response *
MHD_create_response_from_data(size_t size,
			      void * data,
			      int must_free,
			      int must_copy);

/**
 * Destroy a response object and associated resources.  Note that
 * libmicrohttpd may keep some of the resources around if the response
 * is still in the queue for some clients, so the memory may not
 * necessarily be freed immediatley.
 */
void
MHD_destroy_response(struct MHD_Response * response);

/**
 * Add a header line to the response.
 *
 * @return MHD_NO on error (i.e. invalid header or content format).
 */
int
MHD_add_response_header(struct MHD_Response * response,
			const char * header,
			const char * content);

/**
 * Delete a header line from the response.
 *
 * @return MHD_NO on error (no such header known)
 */
int
MHD_del_response_header(struct MHD_Response * response,
			const char * header,
			const char * content);

/**
 * Get all of the headers added to a response.
 *
 * @param iterator callback to call on each header;
 *        maybe NULL (then just count headers)
 * @param iterator_cls extra argument to iterator
 * @return number of entries iterated over
 */ 
int
MHD_get_response_headers(struct MHD_Response * response,
			 MHD_KeyValueIterator * iterator,
			 void * iterator_cls);

/**
 * FIXME-CHRIS
 * @return -1 if no data uploaded; otherwise number of bytes
 *            read into buf; 0 for end of transmission
 */
int 
MHD_read_file_upload(struct MHD_Session * session,
		     void * buf,
		     size_t len);



#if 0 /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif

#endif
