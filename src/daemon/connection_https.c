/*
     This file is part of libmicrohttpd
     (C) 2007, 2008 Daniel Pittman and Christian Grothoff

     This library is free software; you can redistribute it and/or
     modify it under the terms of the GNU Lesser General Public
     License as published by the Free Software Foundation; either
     version 2.1 of the License, or (at your option) any later version.

     This library is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     Lesser General Public License for more details.

     You should have received a copy of the GNU Lesser General Public
     License along with this library; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA

*/

/**
 * @file connection.c
 * @brief  Methods for managing SSL/TLS connections. This file is only
 *         compiled if ENABLE_HTTPS is set.
 * @author Sagie Amir
 * @author Christian Grothoff
 */

#include "internal.h"
#include "connection.h"
#include "memorypool.h"
#include "response.h"
#include "reason_phrase.h"

#include "microhttpsd.h"
/* get opaque type */
#include "gnutls_int.h"

/* forward declarations used when setting secure connection callbacks */
int MHD_connection_handle_read (struct MHD_Connection *connection);
int MHD_connection_handle_write (struct MHD_Connection *connection);
int MHD_connection_handle_idle (struct MHD_Connection *connection);

// TODO rm - appears in a switch default clause
static void
connection_close_error (struct MHD_Connection *connection)
{
  SHUTDOWN (connection->socket_fd, SHUT_RDWR);
  CLOSE (connection->socket_fd);
  connection->socket_fd = -1;
  connection->state = MHD_CONNECTION_CLOSED;
  if (connection->daemon->notify_completed != NULL)
    connection->daemon->notify_completed (connection->daemon->
                                          notify_completed_cls, connection,
                                          &connection->client_context,
                                          MHD_REQUEST_TERMINATED_WITH_ERROR);
}

/* get cipher spec for this connection */
gnutls_cipher_algorithm_t
MHDS_get_session_cipher (struct MHD_Connection *session)
{
  return gnutls_cipher_get (session->tls_session);
}

gnutls_mac_algorithm_t
MHDS_get_session_mac (struct MHD_Connection * session)
{
  return gnutls_mac_get (session->tls_session);
}

gnutls_compression_method_t
MHDS_get_session_compression (struct MHD_Connection * session)
{
  return gnutls_compression_get (session->tls_session);
}

gnutls_certificate_type_t
MHDS_get_session_cert_type (struct MHD_Connection * session)
{
  return gnutls_certificate_type_get (session->tls_session);
}

static ssize_t
MHDS_con_read (struct MHD_Connection *connection)
{
  ssize_t size = gnutls_record_recv (connection->tls_session,
                                     &connection->read_buffer[connection->
                                                              read_buffer_offset],
                                     connection->read_buffer_size);
  return size;
}

static ssize_t
MHDS_con_write (struct MHD_Connection *connection)
{
  ssize_t sent = gnutls_record_send (connection->tls_session,
                                     &connection->write_buffer[connection->
                                                               write_buffer_send_offset],
                                     connection->write_buffer_append_offset
                                     - connection->write_buffer_send_offset);
  return sent;
}

int
MHDS_connection_handle_idle (struct MHD_Connection *connection)
{
  unsigned int timeout;
  while (1)
    {
#if HAVE_MESSAGES
      MHD_DLOG (connection->daemon, "MHDS reached case: %d, l: %d, f: %s\n",
                connection->s_state, __LINE__, __FUNCTION__);
#endif
      switch (connection->s_state)
        {
        case MHDS_HANDSHAKE_FAILED:
          connection->socket_fd = -1;
        case MHDS_CONNECTION_INIT:
          /* wait for request */
        case MHDS_HANDSHAKE_COMPLETE:

        case MHDS_CONNECTION_CLOSED:
          if (connection->socket_fd != -1)
            connection_close_error (connection);
          break;

        default:
          break;
        }
      break;
    }

  timeout = connection->daemon->connection_timeout;

  if ((connection->socket_fd != -1) && (timeout != 0)
      && (time (NULL) - timeout > connection->last_activity))
    {
      connection_close_error (connection);
      return MHD_NO;
    }
  return MHD_YES;
}

int
MHDS_connection_handle_read (struct MHD_Connection *connection)
{
  int ret;

  connection->last_activity = time (NULL);

  if (connection->s_state == MHDS_CONNECTION_CLOSED)
    return MHD_NO;

  /* discover content type */
  unsigned char msg_type;
  if (recv (connection->socket_fd, &msg_type, 1, MSG_PEEK) == -1)
    {
#if HAVE_MESSAGES
      MHD_DLOG (connection->daemon, "Failed to peek into TLS content type\n");
#endif
      return MHD_NO;
    }

  switch (msg_type)
    {
    case GNUTLS_CHANGE_CIPHER_SPEC:

      break;
    case GNUTLS_ALERT:
      /*
       * this call of _gnutls_recv_int expects 0 bytes read.
       * done to decrypt alert message
       */
      _gnutls_recv_int (connection->tls_session, GNUTLS_ALERT,
                        GNUTLS_HANDSHAKE_FINISHED, 0);

      /* CLOSE_NOTIFY */
      if (connection->tls_session->internals.last_alert ==
          GNUTLS_A_CLOSE_NOTIFY)
        {
          gnutls_bye (connection->tls_session, GNUTLS_SHUT_WR);
          connection->tls_session->internals.read_eof = 1;
          connection->socket_fd = -1;
          gnutls_deinit (connection->tls_session);
          return MHD_YES;
        }
      /* non FATAL or WARNING */
      else if (connection->tls_session->internals.last_alert !=
               GNUTLS_AL_FATAL)
        {
#if HAVE_MESSAGES
          MHD_DLOG (connection->daemon,
                    "Received TLS alert: %s\n",
                    gnutls_alert_get_name ((int) connection->tls_session->
                                           internals.last_alert));
#endif
          return MHD_YES;
        }
      /* FATAL */
      else if (connection->tls_session->internals.last_alert ==
               GNUTLS_AL_FATAL)
        {
          connection->tls_session->internals.resumable = RESUME_FALSE;
          connection->tls_session->internals.valid_connection = VALID_FALSE;
          connection->socket_fd = -1;
          gnutls_deinit (connection->tls_session);

          return MHD_NO;
        }
      /* this should never execut */
      else
        {
#if HAVE_MESSAGES
          MHD_DLOG (connection->daemon,
                    "Received unrecognized alert: %s\n",
                    connection->tls_session->internals.last_alert);
#endif
          return MHD_NO;
        }


      /* forward application level content to MHD */
    case GNUTLS_APPLICATION_DATA:
      return MHD_connection_handle_read (connection);

    case GNUTLS_HANDSHAKE:
      ret = gnutls_handshake (connection->tls_session);
      if (ret == 0)
        {
          connection->s_state = MHDS_HANDSHAKE_COMPLETE;
          connection->state = MHD_CONNECTION_INIT;
        }
      /* set connection as closed */
      else
        {
#if HAVE_MESSAGES
          MHD_DLOG (connection->daemon,
                    "Error: Handshake has failed (%s)\n", ret);
#endif
          connection->s_state = MHDS_HANDSHAKE_FAILED;
          gnutls_bye (connection->tls_session, GNUTLS_SHUT_WR);
          gnutls_deinit (connection->tls_session);
          connection->socket_fd = -1;
          return MHD_NO;

        }
      break;
    case GNUTLS_INNER_APPLICATION:
      break;
    }

  return MHD_YES;
}

int
MHDS_connection_handle_write (struct MHD_Connection *connection)
{
  connection->last_activity = time (NULL);
  while (1)
    {
#if HAVE_MESSAGES
      MHD_DLOG (connection->daemon, "MHDS reached case: %d, l: %d, f: %s\n",
                connection->s_state, __LINE__, __FUNCTION__);
#endif
      switch (connection->s_state)
        {

          /* these cases shouldn't occur */
        case MHDS_HANDSHAKE_COMPLETE:
        case MHDS_CONNECTION_INIT:
          // TODO do we have to write back a responce ?
        case MHDS_HANDSHAKE_FAILED:
          /* we should first exit MHDS_REPLY_SENDING */

        case MHDS_CONNECTION_CLOSED:
          if (connection->socket_fd != -1)
            connection_close_error (connection);
          return MHD_NO;
        }
    }
  return MHD_YES;
}

void
MHD_set_https_calbacks (struct MHD_Connection *connection)
{
  connection->recv_cls = &MHDS_con_read;
  connection->send_cls = &MHDS_con_write;
  connection->read_handler = &MHDS_connection_handle_read;
  connection->write_handler = &MHD_connection_handle_write;
  connection->idle_handler = &MHD_connection_handle_idle;
}
