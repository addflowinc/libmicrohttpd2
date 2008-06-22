/* Literal.c - Literal packet filters
 *       Copyright (C) 2002, 2003 Timo Schulz
 *
 * This file is part of OpenCDK.
 *
 * OpenCDK is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * OpenCDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif
#include <stdio.h>
#include <time.h>

#include "opencdk.h"
#include "main.h"
#include "filters.h"


/* Duplicate the string @s but strip of possible
   relative folder names of it. */
static char *
dup_trim_filename (const char *s)
{
  char *p = NULL;

  p = strrchr (s, '/');
  if (!p)
    p = strrchr (s, '\\');
  if (!p)
    return cdk_strdup (s);
  return cdk_strdup (p + 1);
}


static cdk_error_t
literal_decode (void *opaque, FILE * in, FILE * out)
{
  literal_filter_t *pfx = opaque;
  cdk_stream_t si, so;
  cdk_packet_t pkt;
  cdk_pkt_literal_t pt;
  byte buf[BUFSIZE];
  size_t nread;
  int bufsize;
  cdk_error_t rc;

  _cdk_log_debug ("literal filter: decode\n");

  if (!pfx || !in || !out)
    return CDK_Inv_Value;

  rc = _cdk_stream_fpopen (in, STREAMCTL_READ, &si);
  if (rc)
    return rc;

  cdk_pkt_new (&pkt);
  rc = cdk_pkt_read (si, pkt);
  if (rc || pkt->pkttype != CDK_PKT_LITERAL)
    {
      cdk_pkt_release (pkt);
      cdk_stream_close (si);
      return !rc ? CDK_Inv_Packet : rc;
    }

  rc = _cdk_stream_fpopen (out, STREAMCTL_WRITE, &so);
  if (rc)
    {
      cdk_pkt_release (pkt);
      cdk_stream_close (si);
      return rc;
    }

  pt = pkt->pkt.literal;
  pfx->mode = pt->mode;

  if (pfx->filename && pt->namelen > 0)
    {
      /* The name in the literal packet is more authorative. */
      cdk_free (pfx->filename);
      pfx->filename = dup_trim_filename (pt->name);
    }
  else if (!pfx->filename && pt->namelen > 0)
    pfx->filename = dup_trim_filename (pt->name);
  else if (!pt->namelen && !pfx->filename && pfx->orig_filename)
    {
      /* In this case, we need to derrive the output file name
         from the original name and cut off the OpenPGP extension.
         If this is not possible, we return an error. */
      if (!stristr (pfx->orig_filename, ".gpg") &&
          !stristr (pfx->orig_filename, ".pgp") &&
          !stristr (pfx->orig_filename, ".asc"))
        {
          cdk_pkt_release (pkt);
          cdk_stream_close (si);
          cdk_stream_close (so);
          _cdk_log_debug
            ("literal filter: no file name and no PGP extension\n");
          return CDK_Inv_Mode;
        }
      _cdk_log_debug ("literal filter: derrive file name from original\n");
      pfx->filename = dup_trim_filename (pfx->orig_filename);
      pfx->filename[strlen (pfx->filename) - 4] = '\0';
    }

  while (!feof (in))
    {
      _cdk_log_debug ("literal_decode: part on %d size %lu\n",
                      pfx->blkmode.on, pfx->blkmode.size);
      if (pfx->blkmode.on)
        bufsize = pfx->blkmode.size;
      else
        bufsize = pt->len < DIM (buf) ? pt->len : DIM (buf);
      nread = cdk_stream_read (pt->buf, buf, bufsize);
      if (nread == EOF)
        {
          rc = CDK_File_Error;
          break;
        }
      if (pfx->md)
        gcry_md_write (pfx->md, buf, nread);
      cdk_stream_write (so, buf, nread);
      pt->len -= nread;
      if (pfx->blkmode.on)
        {
          pfx->blkmode.size = _cdk_pkt_read_len (in, &pfx->blkmode.on);
          if (pfx->blkmode.size == (size_t) EOF)
            return CDK_Inv_Packet;
        }
      if (pt->len <= 0 && !pfx->blkmode.on)
        break;
    }

  cdk_stream_close (si);
  cdk_stream_close (so);
  cdk_pkt_release (pkt);
  return rc;
}


static char
intmode_to_char (int mode)
{
  switch (mode)
    {
    case CDK_LITFMT_BINARY:
      return 'b';
    case CDK_LITFMT_TEXT:
      return 't';
    case CDK_LITFMT_UNICODE:
      return 'u';
    default:
      return 'b';
    }

  return 'b';
}


static cdk_error_t
literal_encode (void *opaque, FILE * in, FILE * out)
{
  literal_filter_t *pfx = opaque;
  cdk_pkt_literal_t pt;
  cdk_stream_t si;
  cdk_packet_t pkt;
  size_t filelen;
  cdk_error_t rc;

  _cdk_log_debug ("literal filter: encode\n");

  if (!pfx || !in || !out)
    return CDK_Inv_Value;
  if (!pfx->filename)
    {
      pfx->filename = cdk_strdup ("_CONSOLE");
      if (!pfx->filename)
        return CDK_Out_Of_Core;
    }

  rc = _cdk_stream_fpopen (in, STREAMCTL_READ, &si);
  if (rc)
    return rc;

  filelen = strlen (pfx->filename);
  cdk_pkt_new (&pkt);
  pt = pkt->pkt.literal = cdk_calloc (1, sizeof *pt + filelen - 1);
  if (!pt)
    {
      cdk_pkt_release (pkt);
      cdk_stream_close (si);
      return CDK_Out_Of_Core;
    }
  memcpy (pt->name, pfx->filename, filelen);
  pt->namelen = filelen;
  pt->name[pt->namelen] = '\0';
  pt->timestamp = (u32) time (NULL);
  pt->mode = intmode_to_char (pfx->mode);
  pt->len = cdk_stream_get_length (si);
  pt->buf = si;
  pkt->old_ctb = 1;
  pkt->pkttype = CDK_PKT_LITERAL;
  pkt->pkt.literal = pt;
  rc = _cdk_pkt_write_fp (out, pkt);

  cdk_pkt_release (pkt);
  cdk_stream_close (si);
  return rc;
}


int
_cdk_filter_literal (void *opaque, int ctl, FILE * in, FILE * out)
{
  if (ctl == STREAMCTL_READ)
    return literal_decode (opaque, in, out);
  else if (ctl == STREAMCTL_WRITE)
    return literal_encode (opaque, in, out);
  else if (ctl == STREAMCTL_FREE)
    {
      literal_filter_t *pfx = opaque;
      if (pfx)
        {
          _cdk_log_debug ("free literal filter\n");
          cdk_free (pfx->filename);
          pfx->filename = NULL;
          cdk_free (pfx->orig_filename);
          pfx->orig_filename = NULL;
          return 0;
        }
    }
  return CDK_Inv_Mode;
}


static int
text_encode (void *opaque, FILE * in, FILE * out)
{
  const char *s;
  char buf[2048];

  if (!in || !out)
    return CDK_Inv_Value;

  /* FIXME: This code does not work for very long lines. */
  while (!feof (in))
    {
      s = fgets (buf, DIM (buf) - 1, in);
      if (!s)
        break;
      _cdk_trim_string (buf, 1);
      fwrite (buf, 1, strlen (buf), out);
    }

  return 0;
}


static int
text_decode (void *opaque, FILE * in, FILE * out)
{
  text_filter_t *tfx = opaque;
  const char *s;
  char buf[2048];

  if (!tfx || !in || !out)
    return CDK_Inv_Value;

  while (!feof (in))
    {
      s = fgets (buf, DIM (buf) - 1, in);
      if (!s)
        break;
      _cdk_trim_string (buf, 0);
      fwrite (buf, 1, strlen (buf), out);
      fwrite (tfx->lf, 1, strlen (tfx->lf), out);
    }

  return 0;
}


int
_cdk_filter_text (void *opaque, int ctl, FILE * in, FILE * out)
{
  if (ctl == STREAMCTL_READ)
    return text_encode (opaque, in, out);
  else if (ctl == STREAMCTL_WRITE)
    return text_decode (opaque, in, out);
  else if (ctl == STREAMCTL_FREE)
    {
      text_filter_t *tfx = opaque;
      if (tfx)
        {
          _cdk_log_debug ("free text filter\n");
          tfx->lf = NULL;
        }
    }
  return CDK_Inv_Mode;
}
