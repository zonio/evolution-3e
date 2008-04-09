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

#include <gio/gio.h>
#include <openssl/sha.h>
#include "e-cal-backend-3e-priv.h"

typedef struct _attachment attachment;
struct _attachment
{
  char* eee_uri;           /**< eee:// URI */
  char* local_uri;         /**< file:// URI */
  char* sha1;
  char* filename;
  gboolean is_on_server;   /**< Attachment is known to be stored on the server. */
  gboolean is_in_cache;    /**< Attachment is known to be stored locally. */
};

static void attachment_free(attachment* a)
{
  g_free(a->eee_uri);
  g_free(a->local_uri);
  g_free(a->sha1);
  g_free(a->filename);
  g_free(a);
}

static char* checksum_file(GFile* file)
{
  char buf[4096];
  gssize read_bytes = -1;
  guchar raw_sha1[SHA_DIGEST_LENGTH];
  SHA_CTX sha1_ctx;
  guint i;

  GFileInputStream* stream = g_file_read(file, NULL, NULL);

  if (stream)
  {
    SHA1_Init(&sha1_ctx);
    while ((read_bytes = g_input_stream_read(G_INPUT_STREAM(stream), buf, 4096, NULL, NULL)) > 0)
      SHA1_Update(&sha1_ctx, buf, read_bytes);
    SHA1_Final(raw_sha1, &sha1_ctx);
    g_object_unref(stream);
  }

  if (read_bytes < 0)
    return NULL;

  GString* sha1 = g_string_sized_new(SHA_DIGEST_LENGTH * 2);
  for (i = 0; i < SHA_DIGEST_LENGTH; i++)
    g_string_append_printf(sha1, "%02hhx", raw_sha1[i]);

  return g_string_free(sha1, FALSE);
}

static attachment* get_attacmhent(ECalBackend3e* cb, ECalComponent* comp, const char* uri)
{
  GSList* iter;
  attachment* a;

  for (iter = cb->priv->attachments; iter; iter = iter->next)
  {
    a = iter->data;

    if (!g_ascii_strcasecmp(a->local_uri, uri) || !g_ascii_strcasecmp(a->eee_uri, uri))
      return a;
  }

  a = NULL;

  if (g_str_has_prefix(uri, "eee://"))
  {
    char** parts = g_strsplit(uri + 6, "/", 0);
    const char* uid;

    e_cal_component_get_uid(comp, &uid);

    if (g_strv_length(parts) == 4)
    {
      a = g_new0(attachment, 1);
      a->eee_uri = g_strdup(uri);
      a->local_uri = g_strdup_printf("file://%s/%s-%s", e_cal_backend_3e_get_cache_path(cb), uid, parts[3]);
      a->sha1 = g_strdup(parts[2]);
      a->filename = g_strdup(parts[3]);
      GFile* file = g_file_new_for_uri(a->local_uri);
      a->is_in_cache = g_file_query_exists(file, NULL);
      g_object_unref(file);
    }

    g_strfreev(parts);
  }
  else if (g_str_has_prefix(uri, "file://"))
  {
    GFile* file = g_file_new_for_uri(uri);
    char* sha1 = checksum_file(file);
    char* basename = g_file_get_basename(file);
    char* filename = basename;
    const char* uid;

    e_cal_component_get_uid(comp, &uid);

    if (g_str_has_prefix(basename, uid))
      filename = basename + strlen(uid) + 1;

    a = g_new0(attachment, 1);
    a->filename = g_strdup(filename);
    a->sha1 = sha1;
    a->local_uri = g_strdup(uri);
    a->eee_uri = g_strdup_printf("eee://%s/attach/%s/%s", cb->priv->owner, sha1, filename = g_uri_escape_string(filename, NULL, FALSE));
    a->is_in_cache = g_file_query_exists(file, NULL);

    g_object_unref(file);
    g_free(filename);
    g_free(basename);
  }

  if (a)
  {
    cb->priv->attachments = g_slist_append(cb->priv->attachments, a);
    e_cal_backend_3e_attachment_store_save(cb);
  }

  return a;
}

#if 0
static xr_client_conn* open_remote_attachment(ECalBackend3e* cb, const char* uri)
{
  GError* local_err = NULL;
  xr_client_conn* conn;

  char* server_hostname = get_eee_server_hostname(cb->priv->username);
  if (server_hostname == NULL)
  {
    g_set_error(err, 0, -1, "Can't resolve server URI for username '%s'", cb->priv->username);
    goto err;
  }
  cb->priv->server_uri = g_strdup_printf("https://%s/RPC2", server_hostname);
  g_free(server_hostname);

  conn = xr_client_new(&local_err);
  if (conn)
  {
    if (xr_client_open(conn, cb->priv->server_uri, &local_err))
    {
      xr_client_basic_auth(conn, cb->priv->username, cb->priv->password);
      return conn;
    }
    else
      xr_client_free(conn);
  }

  g_clear_error(&local_err);

  return NULL;
}
#endif

static gboolean upload_attachment(ECalBackend3e* cb, attachment* att, GError** err)
{
  GError* local_err = NULL;
  char buf[4096];
  gssize read_bytes = -1;
  gboolean retval = FALSE;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  g_print("ATTACH: upload(%s -> %s)\n", att->local_uri, att->eee_uri);

  GFile* file = g_file_new_for_uri(att->local_uri);
  if (!g_file_query_exists(file, NULL))
  {
    g_set_error(err, 0, -1, "Attachment not found in cache '%s'", att->local_uri);
    g_object_unref(file);
    return FALSE;
  }

  GFileInfo* info = g_file_query_info(file, G_FILE_ATTRIBUTE_STANDARD_SIZE, 0, NULL, NULL);
  GFileInputStream* stream = g_file_read(file, NULL, NULL);
  goffset size = g_file_info_get_size(info);

  xr_client_conn* conn = xr_client_new(&local_err);
  xr_client_open(conn, cb->priv->server_uri, &local_err);
  xr_http* http = xr_client_get_http(conn);

  char* resource = g_strdup_printf("/attach/%s/%s", att->sha1, att->filename);
  xr_http_setup_request(http, "POST", resource, "");
  g_free(resource);
  xr_http_set_basic_auth(http, cb->priv->username, cb->priv->password);
  xr_http_set_message_length(http, size);
  xr_http_write_header(http, &local_err);
  while ((read_bytes = g_input_stream_read(G_INPUT_STREAM(stream), buf, sizeof(buf), NULL, NULL)) > 0)
  {
    xr_http_write(http, buf, read_bytes, &local_err);
    if (e_cal_backend_3e_sync_should_stop(cb) && read_bytes == sizeof(buf))
    {
      read_bytes = -1;
      g_set_error(&local_err, 0, 1, "Upload cancelled.");
      break;
    }
  }
  xr_http_write_complete(http, &local_err);

  xr_http_read_header(http, &local_err);
  GString* msg = xr_http_read_all(http, &local_err);

  if (read_bytes >= 0 && local_err == NULL && xr_http_get_code(http) == 200)
  {
    att->is_on_server = TRUE;
    e_cal_backend_3e_attachment_store_save(cb);
    retval = TRUE;
  }
  else
  {
    char* error_msg = "Unknown error";
    if (local_err)
      error_msg = local_err->message;
    if (msg)
      error_msg = msg->str;
    g_set_error(err, 0, -1, "Upload failed '%s' (%s)", att->local_uri, error_msg);
    g_clear_error(&local_err);
  }

  if (msg)
    g_string_free(msg, TRUE);
  xr_client_free(conn);
  g_object_unref(stream);
  g_object_unref(info);
  g_object_unref(file);

  return retval;
}

static gboolean download_attachment(ECalBackend3e* cb, attachment* att, GError** err)
{
  GError* local_err = NULL;
  char buf[4096];
  gssize bytes_read = -1;
  gboolean retval = FALSE;
  GString* msg = NULL;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  g_print("ATTACH: download(%s -> %s)\n", att->eee_uri, att->local_uri);

  GFile* file = g_file_new_for_uri(att->local_uri);
  if (g_file_query_exists(file, NULL))
  {
    g_object_unref(file);
    return TRUE;
  }

  char* tmp_path = g_strdup_printf("%s.tmp", att->local_uri);
  GFile* tmp_file = g_file_new_for_uri(tmp_path);
  g_free(tmp_path);

  GFileOutputStream* stream = g_file_replace(file, NULL, FALSE, 0, NULL, NULL);

  xr_client_conn* conn = xr_client_new(&local_err);
  xr_client_open(conn, cb->priv->server_uri, &local_err);
  xr_client_basic_auth(conn, cb->priv->username, cb->priv->password);
  xr_http* http = xr_client_get_http(conn);

  char* resource = g_strdup_printf("/attach/%s/%s", att->sha1, att->filename);
  xr_http_setup_request(http, "GET", resource, "");
  g_free(resource);
  xr_http_write_header(http, &local_err);
  xr_http_write_complete(http, &local_err);
  xr_http_read_header(http, &local_err);

  if (xr_http_get_code(http) == 200)
  {
    while ((bytes_read = xr_http_read(http, buf, sizeof(buf), &local_err)) > 0)
    {
      g_output_stream_write(G_OUTPUT_STREAM(stream), buf, bytes_read, NULL, NULL);
      if (e_cal_backend_3e_sync_should_stop(cb) && bytes_read == sizeof(buf))
      {
        bytes_read = -1;
        g_set_error(&local_err, 0, 1, "Download cancelled.");
        break;
      }
    }

    if (bytes_read >= 0)
    {
      //XXX: check real sha1 against att->sha1
      //char* sha1 = checksum_file(tmp_file);
      g_file_move(tmp_file, file, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL);
      att->is_on_server = TRUE;
      att->is_in_cache = TRUE;
      e_cal_backend_3e_attachment_store_save(cb);
      retval = TRUE;
    }
  }
  else if (local_err == NULL)
    msg = xr_http_read_all(http, &local_err);

  if (!retval)
  {
    char* error_msg = "Unknown error";
    if (local_err)
      error_msg = local_err->message;
    if (msg)
      error_msg = msg->str;
    g_set_error(err, 0, -1, "Download failed '%s' (%s)", att->eee_uri, local_err ? local_err->message : "Unknown error");
    g_clear_error(&local_err);
  }

  if (msg)
    g_string_free(msg, TRUE);
  xr_client_free(conn);
  g_object_unref(stream);
  g_object_unref(file);
  g_object_unref(tmp_file);

  return retval;
}

static char* convert_attachment_to_local(ECalBackend3e* cb, ECalComponent* comp, const char* uri)
{
  attachment* a = get_attacmhent(cb, comp, uri);

  return a ? g_strdup(a->local_uri) : NULL;
}

static char* convert_attachment_to_remote(ECalBackend3e* cb, ECalComponent* comp, const char* uri)
{
  attachment* a = get_attacmhent(cb, comp, uri);

  return a ? g_strdup(a->eee_uri) : NULL;
}

static char* get_attachments_store_file(ECalBackend3e* cb)
{
  return g_build_filename(e_cal_backend_3e_get_cache_path(cb), "attachments.xml", NULL);
}

/** @addtogroup eds_attach */
/** @{ */

/** Save attachments list to the XML file.
 * 
 * @param cb 3E calendar backend.
 * 
 * @return TRUE on success, FALSE otherwise.
 */
gboolean e_cal_backend_3e_attachment_store_save(ECalBackend3e* cb)
{
  GSList* iter;
  xmlDoc* doc = xmlNewDoc(BAD_CAST "1.0");
  xmlNode* root = xmlNewNode(NULL, BAD_CAST "store");
  xmlDocSetRootElement(doc, root);

  for (iter = cb->priv->attachments; iter; iter = iter->next)
  {
    attachment* a = iter->data;

    xmlNode* att = xmlNewChild(root, NULL, BAD_CAST "attachemnt", NULL);
    xmlSetProp(att, BAD_CAST "eee_uri", BAD_CAST a->eee_uri);
    xmlSetProp(att, BAD_CAST "local_uri", BAD_CAST a->local_uri);
    xmlSetProp(att, BAD_CAST "sha1", BAD_CAST a->sha1);
    xmlSetProp(att, BAD_CAST "filename", BAD_CAST a->filename);
    xmlSetProp(att, BAD_CAST "is_on_server", BAD_CAST (a->is_on_server ? "1" : "0"));
    xmlSetProp(att, BAD_CAST "is_in_cache", BAD_CAST (a->is_in_cache ? "1" : "0"));
  }

  char* path = get_attachments_store_file(cb);
  int rs = xmlSaveFormatFile(path, doc, 1);
  g_free(path);

  xmlFreeDoc(doc);

  return rs != -1;
}

/** Load attachments list from the XML file.
 * 
 * @param cb 3E calendar backend.
 */
void e_cal_backend_3e_attachment_store_load(ECalBackend3e* cb)
{
  char* path = get_attachments_store_file(cb);
  xmlDoc* doc = xmlReadFile(path, "UTF-8", XML_PARSE_NOERROR|XML_PARSE_NOWARNING|XML_PARSE_NONET);
  g_free(path);
  xmlNode* root = xmlDocGetRootElement(doc);

  e_cal_backend_3e_attachment_store_free(cb);

  if (root)
  {
    xmlNode* item;

    for (item = root->children; item; item = item->next)
    {
      if (item->type != XML_ELEMENT_NODE)
        continue;

      xmlChar* eee_uri = xmlGetProp(item, BAD_CAST "eee_uri");
      xmlChar* local_uri = xmlGetProp(item, BAD_CAST "local_uri");
      xmlChar* sha1 = xmlGetProp(item, BAD_CAST "sha1");
      xmlChar* filename = xmlGetProp(item, BAD_CAST "filename");
      xmlChar* is_on_server = xmlGetProp(item, BAD_CAST "is_on_server");
      xmlChar* is_in_cache = xmlGetProp(item, BAD_CAST "is_in_cache");

      attachment* a = g_new0(attachment, 1);
      a->eee_uri = g_strdup((char*)eee_uri);
      a->local_uri = g_strdup((char*)local_uri);
      a->sha1 = g_strdup((char*)sha1);
      a->filename = g_strdup((char*)filename);
      a->is_on_server = is_on_server ? is_on_server[0] == '1' : FALSE;
      a->is_in_cache = is_in_cache ? is_in_cache[0] == '1' : FALSE;

      xmlFree(eee_uri);
      xmlFree(local_uri);
      xmlFree(sha1);
      xmlFree(filename);
      xmlFree(is_on_server);
      xmlFree(is_in_cache);

      cb->priv->attachments = g_slist_append(cb->priv->attachments, a);
    }
  }

  xmlFreeDoc(doc);
}

/** Free attachments list.
 * 
 * @param cb 3E calendar backend.
 */
void e_cal_backend_3e_attachment_store_free(ECalBackend3e* cb)
{
  g_slist_foreach(cb->priv->attachments, (GFunc)attachment_free, NULL);
  g_slist_free(cb->priv->attachments);
  cb->priv->attachments = NULL;
}

/** Convert attachment URIs from the eee:// format to the file:// format.
 * 
 * @param cb 3E calendar backend.
 * @param comp ECalComponent object.
 * 
 * @return FALSE if any one of the file:// URIs could not be converted.
 */
gboolean e_cal_backend_3e_convert_attachment_uris_to_local(ECalBackend3e* cb, ECalComponent* comp)
{
  GSList* old_attachments = NULL;
  GSList* new_attachments = NULL;
  GSList* iter;
  gboolean retval = TRUE;

  g_return_val_if_fail(comp != NULL, FALSE);

  e_cal_component_get_attachment_list(comp, &old_attachments);
  for (iter = old_attachments; iter; iter = iter->next)
  {
    //XXX: check for success
    if (g_str_has_prefix(iter->data, "eee://"))
      new_attachments = g_slist_append(new_attachments, convert_attachment_to_local(cb, comp, iter->data));
    else
      new_attachments = g_slist_append(new_attachments, g_strdup(iter->data));
  }
  e_cal_component_set_attachment_list(comp, new_attachments);

  g_slist_free(old_attachments);
  g_slist_foreach(new_attachments, (GFunc)g_free, NULL);
  g_slist_free(new_attachments);

  return retval;
}

/** Convert attachment URIs from the file:// format to the eee:// format.
 * 
 * @param cb 3E calendar backend.
 * @param comp ECalComponent object.
 * 
 * @return FALSE if any one of the file:// URIs could not be converted.
 */
gboolean e_cal_backend_3e_convert_attachment_uris_to_remote(ECalBackend3e* cb, ECalComponent* comp)
{
  GSList* old_attachments = NULL;
  GSList* new_attachments = NULL;
  GSList* iter;
  gboolean retval = TRUE;

  g_return_val_if_fail(comp != NULL, FALSE);

  e_cal_component_get_attachment_list(comp, &old_attachments);
  for (iter = old_attachments; iter; iter = iter->next)
  {
    //XXX: check for success
    if (g_str_has_prefix(iter->data, "file://"))
      new_attachments = g_slist_append(new_attachments, convert_attachment_to_remote(cb, comp, iter->data));
    else
      new_attachments = g_slist_append(new_attachments, g_strdup(iter->data));
  }
  e_cal_component_set_attachment_list(comp, new_attachments);

  g_slist_free(old_attachments);
  g_slist_foreach(new_attachments, (GFunc)g_free, NULL);
  g_slist_free(new_attachments);

  return retval;
}

/** Convert attachment URIs from the file:// format to the eee:// format.
 * 
 * @param cb 3E calendar backend.
 * @param comp ECalComponent object.
 * 
 * @return FALSE if any one of the file:// URIs could not be converted.
 */
gboolean e_cal_backend_3e_convert_attachment_uris_to_remote_icalcomp(ECalBackend3e* cb, icalcomponent* comp)
{
  GSList* old_attachments = NULL;
  GSList* new_attachments = NULL;
  GSList* iter;
  gboolean retval = TRUE;
  icalproperty* prop;

  g_return_val_if_fail(comp != NULL, FALSE);

  ECalComponent* ecomp = e_cal_component_new();
  e_cal_component_set_icalcomponent(ecomp, icalcomponent_new_clone(comp));

  for (prop = icalcomponent_get_first_property(comp, ICAL_ATTACH_PROPERTY); prop; 
       prop = icalcomponent_get_next_property(comp, ICAL_ATTACH_PROPERTY))
  {
    icalattach* att = icalproperty_get_attach(prop);
		if (icalattach_get_is_url(att))
    {
      char* new_url = NULL;
      char* new_buf = NULL;
			const char* data = icalattach_get_url(att);
			int buf_size = strlen(data);
			char* buf = g_malloc0(buf_size + 1);
			icalvalue_decode_ical_string(data, buf, buf_size);

      if (g_str_has_prefix(buf, "file://"))
      {
        new_url = convert_attachment_to_remote(cb, ecomp, buf);
        if (new_url)
        {
          buf_size = 2 * strlen(new_url);
          new_buf = g_malloc0(buf_size);
          icalvalue_encode_ical_string(new_url, new_buf, buf_size);
          icalproperty_set_attach(prop, icalattach_new_from_url(new_buf));
          g_free(new_buf);
        }
      }

      g_free(buf);
		}
  }

  g_object_unref(ecomp);

  return retval;
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
  GError* local_err = NULL;
  GSList* attachments = NULL;
  GSList* iter;
  gboolean rs = TRUE;

  g_return_val_if_fail(comp != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  e_cal_component_get_attachment_list(comp, &attachments);
  for (iter = attachments; iter; iter = iter->next)
  {
    attachment* a = get_attacmhent(cb, comp, iter->data);
    if (a)
    {
      if (!upload_attachment(cb, a, &local_err))
      {
        e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb), "Can't upload attachment.", local_err);
        g_clear_error(&local_err);
        rs = FALSE;
      }
    }
  }

  g_slist_free(attachments);

  return rs;
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
  GError* local_err = NULL;
  GSList* attachments = NULL;
  GSList* iter;
  gboolean rs = TRUE;

  g_return_val_if_fail(comp != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  e_cal_component_get_attachment_list(comp, &attachments);
  for (iter = attachments; iter; iter = iter->next)
  {
    attachment* a = get_attacmhent(cb, comp, iter->data);
    if (a)
    {
      if (!download_attachment(cb, a, &local_err))
      {
        e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb), "Can't download attachment.", local_err);
        g_clear_error(&local_err);
        rs = FALSE;
      }
    }
  }

  g_slist_free(attachments);

  return rs;
}

/* @} */
