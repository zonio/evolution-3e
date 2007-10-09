/***************************************************************************
 *  3E plugin for Evolution Data Server                                    *
 *                                                                         *
 *  Copyright (C) 2007 by Zonio                                            *
 *  www.zonio.net                                                          *
 *  Stanislav Slusny <stanislav.slusny@zonio.net>                          *
 *  Ondrej Jirman <ondrej.jirman@zonio.net>                                *
 *                                                                         *
 ***************************************************************************/

#include "e-cal-backend-3e.h"
#include "e-cal-backend-3e-priv.h"
#include "e-cal-backend-3e-utils.h"
#include "e-cal-backend-3e-sync.h"
#include "dns-txt-search.h"

#define EEE_SYNC_STAMP "EEE-SYNC-STAMP"

GQuark e_cal_eds_error_quark()
{
  static GQuark quark;
  return quark ? quark : (quark = g_quark_from_static_string("e_cal_eds_error"));
}

typedef enum
{
  E_CAL_EDS_ERROR_BAD_CACHE,
  E_CAL_EDS_ERROR_BAD_ICAL,
  E_CAL_EDS_ERROR_CONFLICT,
  E_CAL_EDS_ERROR_NO_STAMP,
  E_CAL_EDS_ERROR_PERMISSION,
} E_CAL_EDS_ERROR;

#define E_CAL_EDS_ERROR e_cal_eds_error_quark()

/** @addtogroup eds_conn */
/** @{ */

/** Setup 3E server connection data and check if connection works.
 * 
 * @param cb 3E calendar backend.
 * @param username Username used for authentication.
 * @param password Password.
 * @param test_conn Test connection to verify that username/password/server_uri is valid.
 * @param err Error pointer.
 * 
 * @return FALSE if data are not valid or connection does not work.
 */
gboolean e_cal_backend_3e_setup_connection(ECalBackend3e* cb, const char* username, const char* password, gboolean test_conn, GError** err)
{
  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(username != NULL, FALSE);
  g_return_val_if_fail(password != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  e_cal_backend_3e_close_connection(cb);

  g_free(cb->priv->username);
  g_free(cb->priv->password);
  g_free(cb->priv->server_uri);
  cb->priv->server_uri = NULL;

  cb->priv->username = g_strdup(username);
  cb->priv->password = g_strdup(password);

  if (test_conn)
  {
    if (!e_cal_backend_3e_open_connection(cb, err))
      return FALSE;

    e_cal_backend_3e_close_connection(cb);
  }

  return TRUE;
}

/** Open connection to the 3E server and authenticate user.
 *
 * This function will do nothing and return TRUE if connection is already
 * opened.
 * 
 * @param cb 3E calendar backend.
 * @param err Error pointer.
 *
 * @return TRUE if connection was already open, FALSE on error.
 */
gboolean e_cal_backend_3e_open_connection(ECalBackend3e* cb, GError** err)
{
  GError* local_err = NULL;
  char* server_hostname;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  if (cb->priv->server_uri == NULL)
  {
    server_hostname = get_eee_server_hostname(cb->priv->username);
    if (server_hostname == NULL)
    {
      g_set_error(err, 0, -1, "Can't resolve server URI for username '%s'", cb->priv->username);
      return FALSE;
    }
    cb->priv->server_uri = g_strdup_printf("https://%s/RPC2", server_hostname);
    g_free(server_hostname);
  }

  if (cb->priv->username == NULL || cb->priv->password == NULL || cb->priv->server_uri == NULL)
  {
    g_set_error(err, 0, -1, "Connection was not setup correctly, can't open.");
    return FALSE;
  }

  if (cb->priv->conn == NULL)
  {
    cb->priv->conn = xr_client_new(err);
    if (cb->priv->conn == NULL)
      return FALSE;
    cb->priv->is_open = FALSE;
  }

  if (cb->priv->is_open)
    return TRUE;

  if (xr_client_open(cb->priv->conn, cb->priv->server_uri, err) < 0)
    return FALSE;

  ESClient_auth(cb->priv->conn, cb->priv->username, cb->priv->password, &local_err);
  if (local_err)
  {
    g_propagate_error(err, local_err);
    xr_client_close(cb->priv->conn);
    return FALSE;
  }

  cb->priv->is_open = TRUE;

  return TRUE;
}

/** Close connection to the server.
 * 
 * @param cb 3E calendar backend.
 */
void e_cal_backend_3e_close_connection(ECalBackend3e* cb)
{
  g_return_if_fail(cb != NULL);

  if (cb->priv->is_open)
  {
    xr_client_close(cb->priv->conn);
    cb->priv->is_open = FALSE;
  }
}

/** Get connection state.
 * 
 * @param cb 3E calendar backend.
 * 
 * @return TRUE if open, FALSE if closed.
 */
gboolean e_cal_backend_3e_connection_is_open(ECalBackend3e* cb)
{
  g_return_val_if_fail(cb != NULL, FALSE);

  return cb->priv->is_open;
}

/** Close conenction and free private data.
 * 
 * @param cb 3E calendar backend.
 */
void e_cal_backend_3e_free_connection(ECalBackend3e* cb)
{
  g_return_if_fail(cb != NULL);

  e_cal_backend_3e_close_connection(cb);

  g_free(cb->priv->username);
  g_free(cb->priv->password);
  g_free(cb->priv->server_uri);
  if (cb->priv->conn)
    xr_client_free(cb->priv->conn);

  cb->priv->username = NULL;
  cb->priv->password = NULL;
  cb->priv->server_uri = NULL;
  cb->priv->conn = NULL;
}

/** @} */

/** @addtogroup eds_cal */
/** @{ */

/** Load calendar name, owner and permission from the ESource.
 * 
 * @param cb 3E calendar backend.
 * 
 * @return TRUE on success, FALSE otherwise.
 */
gboolean e_cal_backend_3e_calendar_info_load(ECalBackend3e* cb)
{
  ESource *source;

  source = e_cal_backend_get_source(E_CAL_BACKEND(cb));

  g_free(cb->priv->calname);
  g_free(cb->priv->owner);
  g_free(cb->priv->perm);
  cb->priv->calname = g_strdup(e_source_get_property(source, "eee-calname"));
  cb->priv->owner = g_strdup(e_source_get_property(source, "eee-owner"));
  cb->priv->perm = g_strdup(e_source_get_property(source, "eee-perm"));

  g_free (cb->priv->calspec);
  cb->priv->calspec = g_strdup_printf ("%s:%s", cb->priv->owner, cb->priv->calname);

  if (cb->priv->calname == NULL || cb->priv->owner == NULL)
    return FALSE;

  return TRUE;
}

/** Check if calendar is owned by the user who accesses it.
 * 
 * @param cb 3E calendar backend.
 * 
 * @return TRUE if owned, FALSE if shared.
 */
gboolean e_cal_backend_3e_calendar_is_owned(ECalBackend3e* cb)
{
  if (cb->priv->username == NULL || cb->priv->owner == NULL)
    return FALSE;
  return !g_ascii_strcasecmp(cb->priv->username, cb->priv->owner);
}

/** Check if calendar has given permission. 
 * 
 * @param cb 3E calendar backend.
 * @param perm Permission string ("read", "write").
 * 
 * @return TRUE if @a perm is equal to or subset of calendar's permission.
 */
gboolean e_cal_backend_3e_calendar_has_perm(ECalBackend3e* cb, const char* perm)
{
  if (cb->priv->perm == NULL)
    return FALSE;
  if (!g_ascii_strcasecmp(perm, cb->priv->perm))
    return TRUE;
  if (!g_ascii_strcasecmp(cb->priv->perm, "write"))
    return TRUE;
  return FALSE;
}

/** Set permission in the priv structure of calendar backend.
 * 
 * @param cb 3E calendar backend.
 * @param perm Permission string ("read", "write").
 */
void e_cal_backend_3e_calendar_set_perm(ECalBackend3e* cb, const char* perm)
{
  g_free(cb->priv->perm);
  cb->priv->perm = g_strdup(perm);
}

/** Load permission from the calendar list.
 * 
 * @param cb 3E calendar backend.
 * @param err Error pointer.
 * 
 * @return TRUE if user has read or write access, FALSE on error or no
 * permission.
 */
gboolean e_cal_backend_3e_calendar_load_perm(ECalBackend3e* cb, GError** err)
{
  GError* local_err = NULL;
  GSList* cals;
  GSList* iter;

  if (e_cal_backend_3e_calendar_is_owned(cb))
  {
    e_cal_backend_3e_calendar_set_perm(cb, "write");
    return TRUE;
  }

  if (!e_cal_backend_3e_open_connection(cb, &local_err))
  {
    g_propagate_error(err, local_err);
    return FALSE;
  }

  cals = ESClient_getCalendars(cb->priv->conn, &local_err);
  if (local_err)
  {
    g_propagate_error(err, local_err);
    return FALSE;
  }
  for (iter = cals; iter; iter = iter->next)
  {
    ESCalendar* cal = iter->data;
    if (!g_ascii_strcasecmp(cal->owner, cb->priv->owner) && 
        !g_ascii_strcasecmp(cal->name, cb->priv->calname))
    {
      e_cal_backend_3e_calendar_set_perm(cb, cal->perm);
      Array_ESCalendar_free(cals);
      return TRUE;
    }
  }
  Array_ESCalendar_free(cals);

  e_cal_backend_3e_calendar_set_perm(cb, "none");
  return FALSE;
}

/** @} */

/** @addtogroup eds_sync */
/** @{ */

void e_cal_backend_3e_set_sync_mode(ECalBackend3e* cb, int mode)
{
  g_atomic_int_set(&cb->priv->sync_mode, mode);
  if (mode == SYNC_WORK)
    g_cond_signal(cb->priv->sync_cond);
}

int e_cal_backend_3e_get_sync_mode(ECalBackend3e* cb)
{
  return g_atomic_int_get(&cb->priv->sync_mode);
}

/** Show box with error message, used when synchronization on particular component failed.
 * User is informed by showing summary of the component.
 * 
 * @param backend Calendar backend
 * @param comp Troubleshooting component
 * @param err Error pointer.
 */
void e_cal_sync_error_message(ECalBackend* backend, ECalComponent* comp, GError* err)
{
  ECalComponentText               summary;
  char                            *message;

  g_return_if_fail(backend != NULL);
  g_return_if_fail(comp != NULL);
  g_return_if_fail(err != NULL);
  g_return_if_fail(E_IS_CAL_COMPONENT(comp));

  e_cal_component_get_summary(comp, &summary);

  g_warning("Error(%d): %s", err->code, err->message); 
  message = g_strdup_printf("Synchronization of event \"%s\" failed: %s. Server refused the change."
                            " The change will not be propagated to the server until you modify the"
                            " component.",
                            summary.value, err->message);
  e_cal_backend_notify_error(backend, message);
  g_free(message);
}

void e_cal_sync_error_resolve(ECalBackend3e* cb, GError* err)
{
  /* XML-RPC errors have domain == 0 */
  if (err->domain == 0 && err->code == ES_XMLRPC_ERROR_NO_PERMISSION)
      e_cal_backend_3e_calendar_set_perm(cb, "read");
}

/** Calls XML-RPC deleteObject method on component comp. If failed, returns FALSE and fills
 * err.
 * 
 * @param cb 3E calendar backend.
 * @param comp Component, that is to be removed.
 * @param err Error pointer.
 */
gint e_cal_sync_rpc_deleteObject(ECalBackend3e* cb, ECalComponent* comp, GError** err)
{
  ECalBackend3ePrivate*        priv;
  const char*                  uid;
  const char*                  rid;
  ECalComponentId *id;
  gchar*                       uid_copy;
  GError*                      local_err = NULL;
  ECalBackend*                 backend;
  gboolean                     ok = TRUE;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(comp != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(comp), FALSE);

  D("Deleting from server.");

  priv = cb->priv;
  backend = E_CAL_BACKEND(cb);

  if (e_cal_backend_3e_open_connection(cb, &local_err))
  {
    e_cal_component_get_uid(comp, &uid);
    rid = e_cal_component_get_recurid_as_string (comp);

    if (!ESClient_deleteObject(priv->conn, priv->calspec, uid, &local_err))
    {
      e_cal_sync_error_resolve(cb, local_err);
      if (local_err)
        g_propagate_error(err, local_err);
      e_cal_component_set_local_state(E_CAL_BACKEND(cb), comp);
      ok = FALSE;
    }
  }
  else
  {
    if (local_err)
      g_propagate_error(err, local_err);
    ok = FALSE;
  }

  return ok;
}

typedef struct
{
  GHashTable* tzids;
} ECalBackend3eTzidList;

typedef struct
{
  ECalBackend3e* cb;
  gboolean ok;
  GError** error;
} ECalBackend3eTzidUserData;

static void collect_tzids(icalparameter* param, void *data)
{
  const char* tzid = icalparameter_get_tzid(param);
  ECalBackend3eTzidList* tlist = data;

  if (tzid)
    g_hash_table_insert(tlist->tzids, g_strdup(tzid), NULL);
}

/*
 * Adds timezone to the server - calls XML RPC addoObject on the timezone.
 */
void e_cal_sync_add_timezone(gpointer key, gpointer value, gpointer user_data)
{
  char*                                     tzid;
  ECalBackend3eTzidUserData*                data;
  ECalBackend3ePrivate*                     priv;
  const icaltimezone*                       cache_zone;
  icaltimezone*                             new_zone;
  icalcomponent*                            cache_tzcomp = NULL, *new_comp = NULL;
  char*                                     zone_str;
  GError*                                   local_err = NULL;

  g_return_if_fail(key != NULL);
  /* value is NULL */
  g_return_if_fail(user_data != NULL);
  data = user_data;
  g_return_if_fail(data->ok);

  tzid = key;
  priv = data->cb->priv;

  /* if error flag is set, do nothing */
  if (!data->ok)
    return;

  /* otherwise error should be clear */
  g_return_if_fail(data->error == NULL || *data->error == NULL);

  /* find timezone component in cache */
  cache_zone = e_cal_backend_cache_get_timezone(priv->cache, tzid);
  cache_tzcomp = icaltimezone_get_component((icaltimezone*)cache_zone);

  if (cache_zone)
  {
    /* zone found in cache. if it is synchronized, no work has to be done */
    if (icomp_get_sync_state(cache_tzcomp) != E_CAL_COMPONENT_LOCALLY_CREATED)
      return;

    new_comp = icalcomponent_new_clone(cache_tzcomp);
    new_zone = icaltimezone_new();
    /* sets new_comp with new_zone */
    icaltimezone_set_component(new_zone, new_comp);
    /* we want to have objects on server in state E_CAL_COMPONENT_IN_SYNCH */
    icomp_set_sync_state(new_comp, E_CAL_COMPONENT_IN_SYNCH);
  }
  else
  {
    /* it is not there! */
    g_warning("Could not find timezone %s in the cache!", tzid);

    g_set_error(data->error, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_BAD_CACHE,
                "Internal client error. Please contact your IT support!");

    data->ok = FALSE;

    return;
  }

  if (!ESClient_addObject(priv->conn, priv->calspec, icalcomponent_as_ical_string(new_comp),
                          &local_err))
  {
    if (local_err)
    {
      g_warning("Error, can't add timezone object(%d:%s)!", local_err->code, local_err->message);
      g_propagate_error(data->error, local_err);
    }
    else
    {
      g_warning("Error, can't add timezone object, no error message!");

      g_set_error(data->error, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_BAD_CACHE,
                  "Internal client error. Please contact your IT support!");
    }

    data->ok = FALSE;
    icaltimezone_free(new_zone, 1);
  }
  else
  {
    /* change synchronization state */
    icomp_set_sync_state(new_comp, E_CAL_COMPONENT_IN_SYNCH);
    e_cal_backend_cache_put_timezone(priv->cache, new_zone);
  }
}

/*
 * Extracts all timezones related to the compoment ccomp. Each component
 * is checked, if it is already on the server. If not, it is added.
 */
gboolean e_cal_sync_add_timezones(ECalBackend3e* cb, ECalComponent* ccomp, GError** err)
{
  ECalBackend3eTzidList* tzlist;
  icalcomponent* icomp;
  ECalBackend3eTzidUserData user_data;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(ccomp != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(ccomp), FALSE);

  /* extract component's timezones */
  tzlist = g_new0(ECalBackend3eTzidList, 1);
  tzlist->tzids = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);

  icomp = e_cal_component_get_icalcomponent(ccomp);
  icalcomponent_foreach_tzid(icomp, collect_tzids, tzlist);

  user_data.cb = cb;
  user_data.ok = TRUE;
  user_data.error = err;
  g_hash_table_foreach(tzlist->tzids, e_cal_sync_add_timezone, &user_data);

  /* free tzlist */
  g_hash_table_destroy(tzlist->tzids);
  g_free(tzlist);

  return user_data.ok;
}

/** Calls XML-RPC updateObject method on component ccomp.
 * 
 * @param cb 3E calendar backend.
 * @param ccomp Ical component.
 * @param err Error pointer.
 * 
 * @return 
 */
gboolean e_cal_sync_rpc_updateObject(ECalBackend3e* cb, ECalComponent* ccomp, GError** err)
{
  gchar*                       object;
  ECalBackend3ePrivate*        priv;
  gboolean                     ok = TRUE;
  GError*                      local_err = NULL;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(ccomp != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(ccomp), FALSE);

  T("Updating on server.");

  priv = cb->priv;

  if (e_cal_backend_3e_open_connection(cb, &local_err))
  {

    /* add related timezones */
    if (!e_cal_sync_add_timezones(cb, ccomp, &local_err))
    {
      if (!local_err)
      {
        g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_BAD_CACHE,
                    "Cannot send timezones to the server!");
      }
      else
        g_propagate_error(err, local_err);

      goto err;
    }

    object = e_cal_component_get_as_string(ccomp);

    if (!ESClient_updateObject(priv->conn, priv->calspec, object, &local_err))
    {
      e_cal_sync_error_resolve(cb, local_err);
      if (local_err)
        g_propagate_error(err, local_err);
      e_cal_component_set_local_state(E_CAL_BACKEND(cb), ccomp);
      goto err1;
    }
    else
    {
      e_cal_component_set_sync_state(ccomp, E_CAL_COMPONENT_IN_SYNCH);
      if (!e_cal_backend_cache_put_component(priv->cache, ccomp))
      {
        g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_BAD_CACHE,
                    "Cannot put component into the cache!");
        goto err1;
      }
    }

    g_free(object);
  }
  else
  {
    if (local_err)
      g_propagate_error(err, local_err);

    return FALSE;
  }

  return TRUE;

err1:
  g_free(object);
err:
  e_cal_backend_3e_close_connection(cb);

  return FALSE;
}

/** Calls XML-RPC addObject method on componnet ccomp.
 * 
 * @param cb 3E calendar backend.
 * @param ccomp Ical component.
 * @param err Error pointer.
 * 
 * @return 
 */
gboolean e_cal_sync_rpc_addObject(ECalBackend3e* cb, ECalComponent* ccomp, GError** err)
{
  gchar*                       object;
  ECalBackend3ePrivate*        priv;
  gboolean                     ok = TRUE;
  GError*                      local_err = NULL;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(ccomp != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(ccomp), FALSE);

  T("Adding to server.");

  priv = cb->priv;

  if (e_cal_backend_3e_open_connection(cb, &local_err))
  {
    /* add related timezones */
    if (!e_cal_sync_add_timezones(cb, ccomp, &local_err))
    {
      if (!local_err)
      {
        g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_BAD_CACHE,
                    "Cannot send timezones to the server!");
      }
      else
        g_propagate_error(err, local_err);

      goto err;
    }

    object = e_cal_component_get_as_string(ccomp);

    if (!ESClient_addObject(priv->conn, priv->calspec, object, &local_err))
    {
      g_warning("error, can't add calendar object(%d:%s)", local_err->code, local_err->message);

      e_cal_component_set_local_state(E_CAL_BACKEND(cb), ccomp);
      e_cal_sync_error_resolve(cb, local_err);
      if (local_err)
        g_propagate_error(err, local_err);
      goto err1;
    }
    else
    {
      e_cal_component_set_sync_state(ccomp, E_CAL_COMPONENT_IN_SYNCH);

      if (!e_cal_backend_cache_put_component(priv->cache, ccomp))
      {
        g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_BAD_CACHE,
                    "Cannot put component into the cache!");
        goto err1;
      }
    }

    g_free(object);
  }
  else
  {
    if (local_err)
      g_propagate_error(err, local_err);

    return FALSE;
  }

  return TRUE;

err1:
  g_free(object);
err:
  e_cal_backend_3e_close_connection(cb);

  return FALSE;
}

/** Queries objects from server (calls queryObjects method). Backend connection
 * is used, which must be properly opened already. Ical components from
 * corresponding time window are returned as one icalcomponent of type
 * ICAL_VCALENDAR_COMPONENT. Returned icalcomponent should be freed by
 * icalcomponent_free(queried_comps).
 *
 * @param cb 3E calendar backend.
 * @param sync_start Start of the time window (can be NULL).
 * @param sync_stop End of the time window (must be non-NULL).
 * @param err Error pointer.
 * 
 * @return 
 */
static icalcomponent* e_cal_sync_query_server_objects(ECalBackend3e* cb, const char* sync_start, const char* sync_stop, GError** err)
{
  ECalBackend3ePrivate*       priv;
  GError*                     local_err = NULL;
  gchar*                      query_server_objects_str;
  icalcomponent*              queried_comps = NULL;
  gchar*                      str_server_objects;

  g_return_val_if_fail(cb != NULL, NULL);
  g_return_val_if_fail(sync_stop != NULL, NULL);
  g_return_val_if_fail(err == NULL || *err == NULL, NULL);
  /* sync_start can be NULL */

  priv = cb->priv;

  query_server_objects_str = sync_start
    ? g_strdup_printf("date_from('%s') and date_to('%s')", sync_start, sync_stop)
    : g_strdup_printf("date_to('%s')", sync_stop);

  str_server_objects = ESClient_queryObjects(priv->conn, priv->calspec,
                                             query_server_objects_str,
                                             &local_err);
  if (!str_server_objects)
  {
    if (local_err)
      g_propagate_error(err, local_err);
    goto out;
  }

  /*
   * queried_comps is one big component with subcomponents
   * returned from queryObjects call 
   */
  queried_comps = icalparser_parse_string(str_server_objects);
  if (queried_comps == NULL)
  {
    g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_BAD_ICAL,
                "Cannot parse components returned from server!");
    goto out1;
  }

  return queried_comps;

out1:
  g_free(str_server_objects);
out:
  return NULL;
}

/** Discriminates between fatal and non-fatal errors. Fatal errors do not allow
 * to continue synchronization (No write persmission on calendar),
 * non-fatal errors are errors related to individual ical component.
 * 
 * @param err Error pointer.
 * 
 * @return TRUE, if error is fatal.
 */
static gboolean e_cal_sync_error_is_fatal(GError* err)
{
  /* XML RPC client error */
  if (err->domain == xr_client_error_quark())
    return TRUE;

  /* XML-RPC server error */
  if (err->domain == 0)
  {
    switch (err->code)
    {
      case ES_XMLRPC_ERROR_NO_PERMISSION:
        // FIXME: reread permission from server

        return TRUE;

      case ES_XMLRPC_ERROR_AUTH_FAILED:
      case ES_XMLRPC_ERROR_INTERNAL_SERVER_ERROR:
      case ES_XMLRPC_ERROR_UNKNOWN_USER:
        return TRUE;

      default:
        return FALSE;
    }
  }

  /* unknown error? */
  return TRUE;
}

/** Goes through the clients-changes list (list of locally modified components,
 * not yet commited to the server) and apllies the changes one-by-one. Stops on fatal error.
 * 
 * @param cb 3E calendar backend.
 * @param err Error pointer.
 * 
 * @return TRUE, if no error occured, otherwise FALSE.
 */
// FIXME: what returns? ok is rewritten by the last component
gboolean e_cal_sync_client_to_server_sync(ECalBackend3e* cb, GError** err)
{
  gboolean                     ok = TRUE;
  ECalComponent*               ccomp;
  GList                        *citer;
  ECalBackend3ePrivate         *priv;
  GError*                      local_err = NULL;
  const char                   *uid, *rid;
  gboolean                     finished = FALSE;

  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  g_return_val_if_fail(cb != NULL, FALSE);
  priv = cb->priv;

  D("starting client to server synchronization for %s: %d changes",
    priv->calname,
    g_list_length(priv->sync_clients_changes));

  /*
   * go through the clients-changes list, solve each component
   */
  for (citer = g_list_last(priv->sync_clients_changes);
       !finished && citer;
       citer = g_list_previous(citer))
  {
    ccomp = E_CAL_COMPONENT(citer->data);

    if (e_cal_component_is_local(ccomp))
    {
      e_cal_backend_cache_put_component(priv->cache, ccomp);
      continue;
    }

    switch (e_cal_component_get_sync_state(ccomp))
    {
      case E_CAL_COMPONENT_LOCALLY_CREATED:
        ok = e_cal_sync_rpc_addObject(cb, ccomp, &local_err);
        break;
      case E_CAL_COMPONENT_LOCALLY_MODIFIED:
        ok = e_cal_sync_rpc_updateObject(cb, ccomp, &local_err);
        break;
      case E_CAL_COMPONENT_LOCALLY_DELETED:
        if ((ok = e_cal_sync_rpc_deleteObject(cb, ccomp, &local_err)))
        {
          e_cal_component_get_ids(ccomp, &uid, &rid);
          if (!e_cal_backend_cache_remove_component(priv->cache, uid, rid))
          {
            g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_BAD_CACHE,
                        "Cannot remove component from the cache!");
            ok = FALSE;
          }
        }
        break;
      case E_CAL_COMPONENT_IN_SYNCH:
        g_warning("Component should be changed, but is in synch state");
        break;
      default:
        g_warning("Component should be changed, but has bad state signature!");
    }

    if (ok)
      /*
       * no error on this component, remove from the list
       */
      priv->sync_clients_changes = g_list_remove(priv->sync_clients_changes, ccomp);
    else
    {
      /*
       * only fatal errors stops synchronization process
       */
      finished = e_cal_sync_error_is_fatal(local_err);
      if (local_err)
        g_propagate_error(err, local_err);
    }
  }

  D("finishing client to server synchronization for %s: leaving %d changes",
    priv->calname,
    g_list_length(priv->sync_clients_changes));

  return ok;
}

/** Wake the synchronization thread.
 * 
 * @param cb 3E calendar backend.
 */
void server_sync_signal(ECalBackend3e* cb)
{
  ECalBackend3ePrivate* priv;

  g_return_if_fail(cb != NULL);
 
  priv = cb->priv;

  g_mutex_lock(priv->sync_mutex);
  priv->sync_terminated = TRUE;

  g_cond_signal(priv->sync_cond);
  g_mutex_unlock(priv->sync_mutex);
}

ECalComponent* e_cal_sync_find_settings(ECalBackend3e* cb)
{
  GList                           *components, *l;
  ECalBackend3ePrivate            *priv;
  ECalComponent                   *comp, *found = NULL;
  ECalComponentText               summary;

  g_return_val_if_fail(cb != NULL, NULL);
  priv = cb->priv;

  components = e_cal_backend_cache_get_components(priv->cache);

  for (l = components; !found && l; l = l->next)
  {
    comp = E_CAL_COMPONENT(l->data);

    if (!comp)
      continue;

    e_cal_component_get_summary(comp, &summary);
    if ((e_cal_component_get_vtype(comp) == E_CAL_COMPONENT_EVENT)
      && g_ascii_strcasecmp(summary.value, EEE_SYNC_STAMP) == 0)
    {
      found = comp;
      g_object_ref(found);
    }
  }

  g_list_foreach(components, (GFunc) g_object_unref, NULL);
  g_list_free(components);

  return found;
}

void e_cal_sync_load_stamp(ECalBackend3e* cb, gchar** stamp)
{
	ESource                 *source;
  gchar                   *path;
  ECalBackend3ePrivate    *priv;

  g_return_if_fail(cb != NULL);
  g_return_if_fail(stamp != NULL);
  priv = cb->priv;

  if (!priv->settings)
  {
    *stamp = NULL;
    return;
  }

  *stamp = g_strdup(e_cal_component_get_stamp(priv->settings));

  D("loaded stamp %s", *stamp);
}

void e_cal_sync_save_stamp(ECalBackend3e* cb, const char* stamp)
{
  gchar                           *path;
  ECalBackend3ePrivate            *priv;
  icalcomponent                   *icomp;
  ECalComponentText               summary;
	struct icaltimetype             itt_utc;
  ECalComponentDateTime           dt;

  g_return_if_fail(cb != NULL);
  g_return_if_fail(stamp != NULL);

  priv = cb->priv;

  if (!priv->settings)
  {
    priv->settings = e_cal_component_new();
    icomp = icalcomponent_new(ICAL_VEVENT_COMPONENT);
    e_cal_component_set_icalcomponent(priv->settings,  icomp);

    itt_utc = icaltime_from_string("19700101T000000");
    dt.value = &itt_utc;
    dt.tzid = g_strdup ("UTC");
    e_cal_component_set_dtstamp(priv->settings, &itt_utc);
    e_cal_component_set_dtstart(priv->settings, &dt);
    e_cal_component_set_dtend(priv->settings, &dt);
    e_cal_component_set_created(priv->settings, &itt_utc);
    e_cal_component_set_transparency(priv->settings, E_CAL_COMPONENT_TRANSP_TRANSPARENT);

    summary.value = g_strdup(EEE_SYNC_STAMP);
    summary.altrep = NULL;
    e_cal_component_set_summary(priv->settings, &summary);
  }

  e_cal_component_set_stamp(priv->settings, stamp);
  e_cal_component_commit_sequence(priv->settings);
  if (!e_cal_backend_cache_put_component(priv->cache, priv->settings))
    g_warning("Could not set cache-stamp");
}

gpointer e_cal_sync_main_thread(gpointer data)
{
  ECalBackend3e               *cb;
  ECalBackend3ePrivate        *priv;
  GError                      *err = NULL;
  GTimeVal                    alarm_clock;

  T("");

  g_return_val_if_fail(data != NULL, NULL);

  cb = E_CAL_BACKEND_3E(data);
  priv = cb->priv;

  g_mutex_lock(priv->sync_mutex);

  while (!priv->sync_terminated)
  {
    if (e_cal_backend_3e_get_sync_mode(cb) == SYNC_SLEEP)
    {
      /* just sleep until we get woken up again */
      g_cond_wait(priv->sync_cond, priv->sync_mutex);

      /* check if we should die, work or sleep again */
      continue;
    }

    if (!e_cal_sync_incremental_synchronization(cb, &err))
      // if (!e_cal_sync_total_synchronization(cb, &err))
    {
      if (err)
      {
        g_warning("Synchronization failed with message %s", err->message);
        /*
        char* message = g_strdup_printf("Synchronization failed: %s!", err->message);
        e_cal_backend_notify_error(E_CAL_BACKEND(cb), message);
        g_free(message);
        */
        g_clear_error(&err);
      }
      else
      {
        g_warning("Synchronization failed with no error message");
        /*
        char* message = g_strdup_printf("Synchronization failed: no error message!");
        e_cal_backend_notify_error(E_CAL_BACKEND(cb), message);
        g_free(message);
        */
      }
    }

    /* get some rest :) */
    g_get_current_time(&alarm_clock);
    alarm_clock.tv_sec += DEFAULT_REFRESH_TIME;
    g_cond_timed_wait(priv->sync_cond, 
                      priv->sync_mutex, 
                      &alarm_clock);
  }

  /* killed */
  g_mutex_unlock(priv->sync_mutex);

  return NULL;
}

/** Insert the component into client-changes list (list of locally changed, not
 * yet commited components).
 * 
 * @param cb 3E calendar backend.
 * @param comp ical component.
 */
void e_cal_sync_client_changes_insert(ECalBackend3e* cb, ECalComponent* comp)
{
  ECalBackend3ePrivate        *priv;

  g_return_if_fail(cb != NULL);
  g_return_if_fail(comp != NULL);
  g_return_if_fail(E_IS_CAL_COMPONENT(comp));
  priv = cb->priv;

  g_object_ref(comp);
  priv->sync_clients_changes = g_list_insert(priv->sync_clients_changes, comp, 0);

  D("marking component as changed by the client, now %d changes: %s",
    g_list_length(priv->sync_clients_changes), e_cal_component_get_as_string(comp));	
}

/** Removes the component from clients-changes list (list of locally changed,
 * not yet commited components).
 * 
 * @param cb 3E calendar backend.
 * @param comp ical component.
 */
void e_cal_sync_client_changes_remove(ECalBackend3e* cb, ECalComponent *comp)
{
  ECalBackend3ePrivate        *priv;
  GList                       *node;

  g_return_if_fail(cb != NULL);
  g_return_if_fail(comp != NULL);
  g_return_if_fail(E_IS_CAL_COMPONENT(comp));
  priv = cb->priv;

  node = g_list_find_custom(priv->sync_clients_changes, comp, e_cal_component_compare);
  if (!node)
  {
    g_warning("Searched component was not marked as changed - cannot remove it");
    return;
  }

  priv->sync_clients_changes = g_list_delete_link(priv->sync_clients_changes, node);
  g_object_unref(comp);

  D("unmarking component as changed, now %d changes", g_list_length(priv->sync_clients_changes));
}

/** Builds clients-changes list from scratch: goes through the components in
 * cache and locally modified, not yet commited components are put in the list.
 * 
 * @param cb  3E calendar backend.
 */
void e_cal_sync_rebuild_clients_changes_list(ECalBackend3e* cb)
{
  ECalBackend3ePrivate        *priv;
  ECalComponentSyncState      state;
  ECalComponent               *ccomp;
  GList                       *cobjs, *citer;

  g_return_if_fail(cb != NULL);
  priv = cb->priv;

  T("rebuild client's changes list: %s", priv->calname);

  if (priv->sync_clients_changes)
  {
    g_list_foreach(priv->sync_clients_changes, (GFunc)g_object_unref, NULL);
    g_list_free(priv->sync_clients_changes);
  }

  priv->sync_clients_changes = NULL;
  cobjs = e_cal_backend_cache_get_components(priv->cache);

  for (citer = cobjs; citer; citer = g_list_next(citer))
  {
    ccomp = E_CAL_COMPONENT(citer->data);
    state = e_cal_component_get_sync_state(ccomp);

    if (state != E_CAL_COMPONENT_IN_SYNCH)
      e_cal_sync_client_changes_insert(cb, ccomp);
  }

  g_list_foreach(cobjs, (GFunc) g_object_unref, NULL);
  g_list_free(cobjs);

  T("DONE");
}

/** Applies remote server change (represented by component scomp) to the
 * client's cache.
 * 
 * @param cb 3E calendar backend.
 * @param scomp New component or new version of the component
 * 
 * @return Returns TRUE, if everything is ok.
 */
gboolean e_cal_sync_mirror_server_change(ECalBackend3e* cb, icalcomponent* scomp)
{
  char*                       compstr;
  // GError*                     local_err = NULL;
  ECalComponent*              escomp;
  ECalBackend3ePrivate        *priv;
  ECalComponentId             *id;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(scomp != NULL, FALSE);

  priv = cb->priv;
  T("");

  escomp = e_cal_component_new();

  if (!e_cal_component_set_icalcomponent(escomp, icalcomponent_new_clone(scomp)))
  {
    g_warning("Cannot parse component queried from server!");
    goto out;
  }

  e_cal_component_set_sync_state(escomp, E_CAL_COMPONENT_IN_SYNCH);

  if (!e_cal_backend_cache_put_component(priv->cache, escomp))
  {
    g_warning("Cannot put component into the cache!");
    goto out;
  }
  else
  {
    compstr = e_cal_component_get_as_string(escomp);	

    if (!icomp_get_deleted_status(scomp))
      e_cal_backend_notify_object_created(E_CAL_BACKEND(cb), compstr);
    else
    {
      id = e_cal_component_get_id(escomp);
      e_cal_backend_notify_object_removed(E_CAL_BACKEND(cb), id, compstr, NULL);
      e_cal_component_free_id(id);
    }

    g_free(compstr);
  }
  g_object_unref(escomp);

  return TRUE;

out:
  // FIXME: e_cal_component_free(scomp);
  g_object_unref(escomp);
 return FALSE;
}

/** Collects locally changed components from cache into the hash map (key is
 * component's UID)
 * 
 * @param cb 3E calendar backend.
 * @param remove_unchanged If TRUE, inserted component is removed from the cache.
 * 
 * @return Hash table, with locally changed components.
 */
GHashTable* e_cal_sync_collect_cache_hash(ECalBackend3e* cb, gboolean remove_unchanged)
{
  GList                    *cobjs;
  GList                    *citer;
  ECalComponent            *ccomp = NULL;
  ECalComponentSyncState   sync_state;
  const char               *uid;	
  const char               *rid;
  ECalComponentId          *id;
  char                     *str_comp;
  char                     *uid_copy;
  GHashTable               *cache_hash;
  ECalBackend3ePrivate     *priv;
  ECalBackendCache         *bcache;

  // FIXME: this could be done by clients-changes list... (?)

  g_return_val_if_fail(cb != NULL, NULL);
  priv = cb->priv;
  g_return_val_if_fail(priv != NULL, NULL);
  bcache = priv->cache;
  g_return_val_if_fail(bcache != NULL, NULL);

  cache_hash = g_hash_table_new(g_str_hash, g_str_equal);
  cobjs = e_cal_backend_cache_get_components(bcache);

  for (citer = cobjs; citer; citer = g_list_next (citer))
  {
    ccomp = E_CAL_COMPONENT(citer->data);
    sync_state = e_cal_component_get_sync_state(ccomp);
    e_cal_component_get_ids(ccomp, &uid, &rid);

    if (sync_state == E_CAL_COMPONENT_IN_SYNCH)
    {
      if (remove_unchanged)
      {
        if (!e_cal_backend_cache_remove_component(priv->cache, uid, rid))
          g_warning("Could not remove component from cache!");
        else
        {
          str_comp = g_strdup(e_cal_component_get_as_string(ccomp));
          id = e_cal_component_get_id(ccomp);
          e_cal_backend_notify_object_removed(E_CAL_BACKEND(cb), id,
                                              e_cal_component_get_as_string(ccomp), NULL);
          e_cal_component_free_id(id);
        }
      }
    }
    else
    {
      uid_copy = g_strdup_printf("UID:%s", uid);
      g_hash_table_insert(cache_hash, (gpointer)uid_copy, ccomp);
      g_object_ref(ccomp);
    }
  }

  g_list_foreach(cobjs, (GFunc) g_object_unref, NULL);
  g_list_free(cobjs);

  return cache_hash;
}

/** Tries to resolve conflict. Conflict is the situation, when one component is
 * changed both on server and client. The newer change wins.
 * 
 * @param cb 3E calendar backend.
 * @param scomp Component with server's change.
 * @param ccomp Component with client's change.
 * @param err Error pointer.
 * 
 * @return TRUE, if no error occured.
 */
gboolean e_cal_sync_resolve_conflict(ECalBackend3e* cb, icalcomponent* scomp, ECalComponent* ccomp, GError** err)
{
  ECalBackend3ePrivate*     priv;
  time_t                    ctime;
  time_t                    stime;
  ECalComponent*            new_escomp;
  ECalComponentSyncState    sync_state;
  char*                     new_uid;
  GError*                   local_err = NULL;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(scomp != NULL, FALSE);
  g_return_val_if_fail(ccomp != NULL, FALSE);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(ccomp), FALSE);

  sync_state = e_cal_component_get_sync_state(ccomp);
  g_return_val_if_fail(sync_state != E_CAL_COMPONENT_IN_SYNCH, FALSE);

  priv = cb->priv;
  ctime = e_cal_component_get_dtstamp_as_timet(ccomp);
  stime = icomp_get_dtstamp_as_timet(scomp);

  T("RESOLVING CONFLICT");

  if (ctime > stime)
  {
    g_warning("Client's component is newer");
    /* client's component is newer */
    if (icomp_get_deleted_status(scomp))
    {
      /* server component was deleted */
      if (e_cal_component_has_deleted_status(ccomp))
        /* client component was deleted too => no conflict in fact */
        return TRUE;

      /* 
       * server component was deleted and client's component was not deleted.
       * we cannot send updateObject or addObject on deleted component,
       * server does not allow this operation. we have to create copy of client's
       * component - change it's UID
       */

        new_uid = e_cal_component_gen_uid();
        e_cal_component_set_uid(ccomp, new_uid);
        g_free(new_uid);
    }
    else
    {
      /* server component was not deleted, delete it */
      new_escomp = e_cal_component_new();

      if (!e_cal_component_set_icalcomponent(new_escomp, icalcomponent_new_clone(scomp)))
      {
        g_warning("Cannot parse component queried from server!");
        g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_BAD_ICAL,
                    "Cannot parse component queried from server!");
        goto error;
      }

      if (!e_cal_sync_rpc_deleteObject(cb, new_escomp, &local_err))
      {
        if (local_err)
          g_propagate_error(err, local_err);

        goto error;
      }
      
      g_object_unref(new_escomp);

      if (e_cal_component_has_deleted_status(ccomp))
        /* client's component was deleted too, no more work */
        return TRUE;
    }

    /* client's component was added or updated,
       send client's version to server  */
    if (sync_state == E_CAL_COMPONENT_LOCALLY_CREATED)
    {
      /* add ccomp */
      if (!e_cal_sync_rpc_addObject(cb, ccomp, &local_err))
      {
        if (local_err)
          g_propagate_error(err, local_err);

        return FALSE;
      }
    }
    else if (sync_state == E_CAL_COMPONENT_LOCALLY_MODIFIED)
    {
      /* update ccomp */
      if (!e_cal_sync_rpc_updateObject(cb, ccomp, &local_err))
      {
        if (local_err)
          g_propagate_error(err, local_err);

        return FALSE;
      }
    }

    /* insert client's version to the cache  */
    if (!e_cal_backend_cache_put_component(priv->cache, ccomp))
    {
      g_warning("Cannot put component into the cache!");
      g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_CONFLICT,
                  "Cannot put component into the cache!");

      return FALSE;
    }
  }
  else
  {
    g_warning("Server's component is newer");

    /* server's component is newer */
    new_escomp = e_cal_component_new();

    if (!e_cal_component_set_icalcomponent(new_escomp, icalcomponent_new_clone(scomp)))
    {
      g_warning("Cannot parse component queried from server!");
      g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_BAD_ICAL,
                  "Cannot parse component queried from server!");
      goto error;
    }

    e_cal_component_set_sync_state(new_escomp, E_CAL_COMPONENT_IN_SYNCH);

    if (!e_cal_backend_cache_put_component(priv->cache, new_escomp))
    {
      g_warning("Cannot put component into the cache!");
      g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_CONFLICT,
                  "Cannot put component into the cache!");
      goto error;
    }

    g_object_unref(new_escomp);
  }

  return TRUE;

error:
    g_object_unref(new_escomp);

  return FALSE;
}

/** Fills the now variable with actual time in string format.
 * 
 * @param now String, 255 character long. 
 * 
 * @return TRUE, if ok.
 */
gboolean e_cal_sync_get_now(char* now)
{
  time_t                   now_time = time(NULL);
  struct tm                tm_now;

  gmtime_r(&now_time, &tm_now);
  if (!(strftime(now, 255, "%F %T", &tm_now)))
  {
    g_warning("Cannot convert time_now to string format!");
    return FALSE;
  }

  return TRUE;
}

void print_func(gpointer key, gpointer value, gpointer user_data)
{
  char* k = (char*)key;
  D("%s", k);
}

gboolean e_cal_sync_run_synchronization(ECalBackend3e* cb, gboolean incremental, GError** err)
{
  ECalBackend3ePrivate*     priv;
  char                     *last_sync_stamp = NULL;
  char                      now[256];
  GError*                   local_err = NULL;
  GHashTable*               cache_hash;
  const char               *uid;	
  char                     *uid_copy;
  gboolean                 ok;
  icalcomponent_kind       bkind;
  icalcomponent            *server_components;
  icalcomponent            *scomp;

  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  g_return_val_if_fail(cb != NULL, FALSE);
  priv = cb->priv;

  T("cal name %s, incremental %d, sync stamp %s", priv->calname, incremental, priv->sync_stamp);

  if (!e_cal_backend_3e_open_connection(cb, &local_err))
  {
    g_warning("Unable to open connection to the server(%d): %s",
              local_err->code, local_err->message);
    e_cal_sync_error_resolve(cb, local_err);
    goto err0;
  }

  if (!e_cal_backend_3e_calendar_load_perm(cb, &local_err))
  {
    if (local_err)
      g_propagate_error(err, local_err);
    goto err1;
  }

  /*
   * incremental synchronization needs last time stamp
   */
  if (incremental && !priv->sync_stamp)
  {
    g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_NO_STAMP,
                "No synchronization stamp - cannot run incremental synchronization!");
    goto err1;
  }

  last_sync_stamp = incremental ? g_strdup(priv->sync_stamp) : NULL;

  if (!e_cal_sync_get_now(now))
    goto err1;

  D("Synchronizing %s: %s - %s", priv->calname, last_sync_stamp ? last_sync_stamp : "", now);

  /* build hash map with components, that are locally changed.
   * if total synchro mode, unchanged components are removed from the cache.
   */
  cache_hash = e_cal_sync_collect_cache_hash(cb, !incremental);
  if (!cache_hash)
    goto err1;

  server_components = e_cal_sync_query_server_objects(cb, last_sync_stamp, now, &local_err);
  if (!server_components)
  {
    if (local_err)
      g_propagate_error(err, local_err);
    goto err2;
  }

  bkind = e_cal_backend_get_kind(E_CAL_BACKEND(cb));

  if (icalcomponent_isa(server_components) == ICAL_VCALENDAR_COMPONENT)
  {
    /* scomp is subcomponent of queried_comps */
    scomp = icalcomponent_get_first_component(server_components, bkind);

    while (scomp)
    {
      /* scomp is component, that was modified on server */
      uid = icomp_get_uid(scomp);
      uid_copy = g_strstrip(g_strdup(uid));

      /*
      g_debug("Looking for UID \"%s\", calendar %s", uid_copy, priv->calname);

      D("PRINTING HASH:");
      g_hash_table_foreach(cache_hash, print_func, NULL);
      D("DONE PRINTING HASH:");
      */
      ECalComponent* ccomp = g_hash_table_lookup(cache_hash, uid_copy);
      // if (!icomp_get_deleted_status(scomp))
      {
        if (!ccomp)
          /* scomp: modified on server, not modified on client */
          e_cal_sync_mirror_server_change(cb, scomp);
        else if (!e_cal_component_is_local(ccomp))
        {
          /* scomp: modified on server, modified on client
           * ==> resolve conflict
           */
          D("CONFICTING CLIENTS COMP: %s", e_cal_component_get_as_string(ccomp));
          D("CONFLICTING SERVERS COMP: %s", icalcomponent_as_ical_string(scomp));

          if (incremental)
          {
            g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_CONFLICT,
                        "Conflicting components!");
            goto err3;
          }
          else if (!e_cal_sync_resolve_conflict(cb, scomp, ccomp, &local_err))
          { 
            if (local_err)
              g_propagate_error(err, local_err);
            goto err3;
          }
        }
      }
      scomp = icalcomponent_get_next_component(server_components, bkind);
    }
  }

  /* send changes from client->server only when we have write persmissions */
  if (e_cal_backend_3e_calendar_has_perm(cb, "write"))
  {
    if (!e_cal_sync_client_to_server_sync(cb, &local_err))
    {
      if (local_err)
        g_propagate_error(err, local_err);
      goto err3;
    }
  }

  icalcomponent_free(server_components);
  g_hash_table_destroy(cache_hash);
  g_free(last_sync_stamp);
  e_cal_sync_save_stamp(cb, now);
  g_free(priv->sync_stamp);
  priv->sync_stamp = g_strdup(now);
  e_cal_backend_3e_close_connection(cb);

  D("Synchronization of %s:%s OK.", priv->username, priv->calname);
  return TRUE;

err3:
  icalcomponent_free(server_components);
err2:
  g_hash_table_destroy(cache_hash);
err1:
  e_cal_backend_3e_close_connection(cb);
err0:
  g_free(last_sync_stamp);
  g_warning("Synchronization of %s:%s failed.", priv->username, priv->calname);
  return FALSE;
}

/** Runs total synchronization.
 * 
 * @param cb 3E calendar backend.
 * @param err Error pointer.
 * 
 * @return FALSE, if synchronization process failed, TRUE otherwise.
 */
gboolean e_cal_sync_total_synchronization(ECalBackend3e* cb, GError** err)
{
  GError* local_err = NULL;
  ECalBackend3ePrivate* priv;

  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  if (!e_cal_sync_run_synchronization(cb, FALSE, &local_err))
  {
    priv = cb->priv;

    g_warning("Total synchronization of %s:%s failed: %s!", priv->username, priv->calname,
              local_err ? local_err->message : "No error message");
    if (local_err)
      g_propagate_error(err, local_err);
    return FALSE;
  }

  return TRUE;
}

/** Run incremental synchronization.
 * 
 * @param cb 3E calendar backend.
 * @param err Error pointer.
 * 
 * @return FALSE, if synchronization process failed, TRUE otherwise.
 */
gboolean e_cal_sync_incremental_synchronization(ECalBackend3e* cb, GError** err)
{
  GError* local_err = NULL;
  gboolean go_on = TRUE;
  ECalBackend3ePrivate* priv;

  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  if (!e_cal_sync_run_synchronization(cb, TRUE, &local_err))
  {
    // FIXME: decide, whether to run total synchronization...
    priv = cb->priv;

    g_warning("Incremental synchronization of %s:%s failed: %s!", priv->username, priv->calname,
              local_err ? local_err->message : "No error message");

    if (local_err && local_err->domain == e_cal_eds_error_quark())
    {
      switch (local_err->code)
      {
        case E_CAL_EDS_ERROR_PERMISSION:
          go_on = FALSE;
          break;

        default:
          /* go_on = TRUE */
          break;
      }
    }
    /* else go_on = TRUE */

    if (go_on)
    {
      g_clear_error(&local_err);
      if (!e_cal_sync_run_synchronization(cb, FALSE, &local_err))
      {
        g_warning("Total synchronization of %s:%s failed: %s!", priv->username, priv->calname,
                  local_err ? local_err->message : "No error message");
        if (local_err)
          g_propagate_error(err, local_err);
        return FALSE;
      }
    }
    else
    {
      if (local_err)
        g_propagate_error(err, local_err);
      return FALSE;
    }
  }

  return TRUE;
}

/** @} */
