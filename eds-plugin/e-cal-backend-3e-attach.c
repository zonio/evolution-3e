/* 
 * Authors: Ondrej Jirman <ondrej.jirman@zonio.net>
 *          Stanislav Slusny <stanislav.slusny@zonio.net>
 *
 * Copyright 2008 Zonio, s.r.o.
 * 
 * This file is part of evolution-3e.
 *
 * Libxr is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 2 of the License, or (at your option) any
 * later version.
 *
 * Libxr is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with evolution-3e.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "e-cal-backend-3e-priv.h"

#if 0
//ATTACH: some leftover API that may be useful if there will be a need to maintain
//some attachment metadata storage

typedef struct _ECalBackend3eAttachment ECalBackend3eAttachment;

/** Attachment description object.
 */
struct _ECalBackend3eAttachment
{
  char* eee_uri;           /**< eee:// URI */
  char* local_uri;         /**< file:// URI */
  gboolean is_on_server;   /**< Attachment is known to be stored on the server. */
  gboolean is_in_cache;    /**< Attachment is known to be stored locally. */
};

ECalBackend3eAttachment* e_cal_backend_3e_attachment_new_local(const char* local_uri);
ECalBackend3eAttachment* e_cal_backend_3e_attachment_new_remote(const char* eee_uri);
char* e_cal_backend_3e_attachment_get_path(ECalBackend3eAttachment* att);
char* e_cal_backend_3e_attachment_get_sha1_from_file(ECalBackend3eAttachment* att);
char* e_cal_backend_3e_attachment_get_sha1_from_uri(ECalBackend3eAttachment* att);
gboolean e_cal_backend_3e_attachment_store_download(ECalBackend3e* cb, ECalBackend3eAttachment* att, GError* err);
gboolean e_cal_backend_3e_attachment_store_upload(ECalBackend3e* cb, ECalBackend3eAttachment* att, GError* err);
gboolean e_cal_backend_3e_attachment_store_put(ECalBackend3e* cb, ECalBackend3eAttachment* att);
ECalBackend3eAttachment* e_cal_backend_3e_attachment_store_get(ECalBackend3e* cb, char* uri);
GSList* e_cal_backend_3e_icomp_get_attachments(ECalBackend3e* cb, icalcomponent* icomp);
gboolean e_cal_backend_3e_icomp_attachments_downloaded(ECalBackend3e* cb, icalcomponent* icomp);
gboolean e_cal_backend_3e_icomp_attachments_uploaded(ECalBackend3e* cb, icalcomponent* icomp);
#endif

/** @addtogroup eds_attach */
/** @{ */

/** Convert attachment URIs from the eee:// format to the file:// format.
 * 
 * @param cb 3E calendar backend.
 * @param comp ECalComponent object.
 * 
 * @return New ECalComponent object with converted URIs or NULL if any one of
 * the eee:// URIs could not be converted.
 */
ECalComponent* e_cal_backend_3e_convert_attachment_uris_to_local(ECalBackend3e* cb, ECalComponent* comp)
{
  g_return_val_if_fail(comp != NULL, NULL);

  return NULL;
}

/** Convert attachment URIs from the file:// format to the eee:// format.
 * 
 * @param cb 3E calendar backend.
 * @param comp ECalComponent object.
 * 
 * @return New ECalComponent object with converted URIs or NULL if any one of
 * the file:// URIs could not be converted.
 */
ECalComponent* e_cal_backend_3e_convert_attachment_uris_to_remote(ECalBackend3e* cb, ECalComponent* comp)
{
  g_return_val_if_fail(comp != NULL, NULL);

  return NULL;
}

/** Upload all attachments to the 3E server.
 *
 * This method must cancel uploads and return FALSE if sync thread shutdown is requested.
 * 
 * @param cb 3E calendar backend.
 * @param comp ECalComponent object.
 * @param err Error pointer.
 * 
 * @return TRUE on success (all attachments are on the server), FALSE otherwise.
 */
gboolean e_cal_backend_3e_upload_attachments(ECalBackend3e* cb, ECalComponent* comp, GError** err)
{
  g_return_val_if_fail(comp != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  return TRUE;
}

/** Download all attachments from the 3E server.
 * 
 * This method must cancel downloads and return FALSE if sync thread shutdown is requested.
 *
 * @param cb 3E calendar backend.
 * @param comp ECalComponent object.
 * @param err Error pointer.
 * 
 * @return TRUE on success (all attachments are on the local storage), FALSE otherwise.
 */
gboolean e_cal_backend_3e_download_attachments(ECalBackend3e* cb, ECalComponent* comp, GError** err)
{
  g_return_val_if_fail(comp != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  return TRUE;
}

/* @} */
