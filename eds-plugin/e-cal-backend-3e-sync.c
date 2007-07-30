/**************************************************************************************************
 *  3E plugin for Evolution Data Server                                                           * 
 *                                                                                                *
 *  Copyright (C) 2007 by Zonio                                                                   *
 *  www.zonio.net                                                                                 *
 *  stanislav.slusny@zonio.net                                                                    *
 *                                                                                                *
 **************************************************************************************************/

#include "e-cal-backend-3e.h"
#include "e-cal-backend-3e-priv.h"
#include "e-cal-backend-3e-utils.h"
#include "e-cal-backend-3e-sync.h"

#define EEE_SYNC_STAMP "EEE-SYNC-STAMP"

// FIXME
GQuark es_server_error_quark()
{
  static GQuark quark;
  return quark ? quark : (quark = g_quark_from_static_string("es_server_error"));
}

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

/** 
 * @brief Opens connection to the server and authorizes user (calls XML-RPC authorize method).
 * 
 * @param cb Calendar backend
 * @param err Error code (if any)
 */
gboolean
e_cal_sync_server_open(ECalBackend3e* cb, GError** err)
{
  ECalBackend3ePrivate*        priv;
  GError*                      local_err = NULL;

  T("Opening connection.");

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  priv = cb->priv;
  g_return_val_if_fail(priv != NULL, FALSE);
  g_return_val_if_fail(priv->conn != NULL, FALSE);

  xr_client_open(priv->conn, priv->server_uri, &local_err);
  if (local_err)
  {
    g_propagate_error(err, local_err);
    goto err0;
  }
  ESClient_auth(priv->conn, priv->username, priv->password, &local_err);
  if (local_err)
  {
    g_propagate_error(err, local_err);
    goto err1;
  }

  return TRUE;

err1:
  xr_client_close(priv->conn);
err0:
  return FALSE;
}

/** 
 * @brief Shows box with error message, used when synchronization on particular component failed.
 *        User is informed by showing summary of the component.
 * 
 * @param backend Calendar backend
 * @param comp Troubleshooting component
 * @param err  Error with filled code and error message.
 */
void
e_cal_sync_error_message(ECalBackend* backend, ECalComponent* comp, GError* err)
{
  ECalComponentText               summary;
  char                            *message;

  g_return_if_fail(backend != NULL);
  g_return_if_fail(comp != NULL);
  g_return_if_fail(err != NULL);

  e_cal_component_get_summary(comp, &summary);

  g_warning("Error(%d): %s", err->code, err->message); 
  message = g_strdup_printf("Synchronization of event \"%s\" failed: %s. Server refused the change."
                            " The change will not be propagated to the server until you modify the"
                            " component.",
                            summary.value, err->message);
  e_cal_backend_notify_error(backend, message);
  g_free(message);
}

void
e_cal_sync_error_resolve(ECalBackend3e* cb, GError* err)
{
  ECalBackend3ePrivate         *priv;

  g_return_if_fail(cb != NULL);
  priv = cb->priv;

  if (err->domain == es_server_error_quark())
  {
    switch (err->code)
    {
      case ES_XMLRPC_ERROR_NO_PERMISSION:
        D("Resolvingerror ES_XMLRPC_ERROR_NO_PERMISSION: Setting read-only calendar");
        priv->has_write_permission = FALSE;
        break;

      default:
        break;
    }
  }
}

/** 
 * @brief Calls XML-RPC deleteObject method on component comp. If failed, returns FALSE and fills
 * err.
 * 
 * @param cb Calendar backend.
 * @param comp Component, that is to be removed.
 * @param conn_opened If TRUE, no new connection is opened and authorize XML-RPC method is not
 * called. Otherwise, new connection is opened, authorize XML RPC is callend and after calling
 * deleteObject method, connection is closed.
 * @param err Error 
 */
gint
e_cal_sync_rpc_deleteObject(ECalBackend3e* cb,
                            ECalComponent* comp,
                            gboolean conn_opened,
                            GError** err)
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

  D("Deleting from server.");

  priv = cb->priv;
  backend = E_CAL_BACKEND(cb);

  if (conn_opened || e_cal_sync_server_open(cb, &local_err))
  {
    e_cal_component_get_uid(comp, &uid);
    rid = e_cal_component_get_recurid_as_string (comp);
    uid_copy = g_strdup_printf("UID:%s", uid);

    if (!ESClient_deleteObject(priv->conn, priv->calspec, uid_copy, &local_err))
    {
      e_cal_sync_error_resolve(cb, local_err);
      g_propagate_error(err, local_err);
      e_cal_component_set_local_state(E_CAL_BACKEND(cb), comp);
      ok = FALSE;
    }

    if (!conn_opened)
      xr_client_close(priv->conn);

    g_free(uid_copy);
  }
  else
  {
    g_propagate_error(err, local_err);
    ok = FALSE;
  }

  return ok;
}

/** 
 * @brief Calls XML-RPC updateObject method on component ccomp.
 * 
 * @param cb Calendar backend.
 * @param ccomp 
 * @param conn_opened If TRUE, no new connection is opened and authorize XML-RPC method is not
 * called. Otherwise, new connection is opened, authorize XML RPC is callend and after calling
 * updateObject method, connection is closed.
 * @param err 
 * 
 * @return 
 */
gboolean
e_cal_sync_rpc_updateObject(ECalBackend3e* cb,
                            ECalComponent* ccomp,
                            gboolean conn_opened, /* connection already opened */
                            GError** err)
{
  gchar*                       object;
  ECalBackend3ePrivate*        priv;
  gboolean                     ok = TRUE;
  GError*                      local_err = NULL;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(ccomp != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  T("Updating on server.");

  priv = cb->priv;

  if (conn_opened || e_cal_sync_server_open(cb, &local_err))
  {
    object = e_cal_component_get_as_string(ccomp);

    if (!ESClient_updateObject(priv->conn, priv->calspec, object, &local_err))
    {
      e_cal_sync_error_resolve(cb, local_err);
      g_propagate_error(err, local_err);
      e_cal_component_set_local_state(E_CAL_BACKEND(cb), ccomp);
      ok = FALSE;
    }
    else
    {
      e_cal_component_set_sync_state(ccomp, E_CAL_COMPONENT_IN_SYNCH);
      if (!e_cal_backend_cache_put_component(priv->cache, ccomp))
      {
        g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_BAD_CACHE,
                    "Cannot put component into the cache!");
        ok = FALSE; 
      }
    }

    if (!conn_opened)
      xr_client_close(priv->conn);
    g_free(object);
  }
  else
  {
    g_propagate_error(err, local_err);
    ok = FALSE;
  }

  return ok;
}

/** 
 * @brief Calls XML-RPC addObject method on componnet ccomp.
 * 
 * @param cb  Calendar backend.
 * @param ccomp Ical component.
 * @param conn_opened If TRUE, no new connection is opened and authorize XML-RPC method is not
 * called. Otherwise, new connection is opened, authorize XML RPC is callend and after calling
 * addObject method, connection is closed.
 * @param err 
 * 
 * @return 
 */
gboolean
e_cal_sync_rpc_addObject(ECalBackend3e* cb,
                         ECalComponent* ccomp,
                         gboolean conn_opened, /* connection already opened */
                         GError** err)
{
  gchar*                       object;
  ECalBackend3ePrivate*        priv;
  gboolean                     ok = TRUE;
  GError*                      local_err = NULL;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(ccomp != NULL, FALSE);
  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  T("Adding to server.");

  priv = cb->priv;

  if (conn_opened || e_cal_sync_server_open(cb, &local_err))
  {
    object = e_cal_component_get_as_string(ccomp);

    if (!ESClient_addObject(priv->conn, priv->calspec, object, &local_err))
    {
      g_warning("error, can't add calendar object(%d:%s)", local_err->code, local_err->message);

      e_cal_component_set_local_state(E_CAL_BACKEND(cb), ccomp);
      e_cal_sync_error_resolve(cb, local_err);
      g_propagate_error(err, local_err);
      ok = FALSE;
    }
    else
    {
      e_cal_component_set_sync_state(ccomp, E_CAL_COMPONENT_IN_SYNCH);

      if (!e_cal_backend_cache_put_component(priv->cache, ccomp))
      {
        g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_BAD_CACHE,
                    "Cannot put component into the cache!");
        ok = FALSE; 
      }
    }

    if (!conn_opened)
      xr_client_close(priv->conn);

    g_free(object);
  }
  else
  {
    g_propagate_error(err, local_err);
    ok = FALSE;
  }

  return ok;
}

/** 
 * @brief Queries objects from server (calls queryObjects method). Backend connection
 * is used, which must be properly opened already. Ical components from corresponding
 * time window are returned as one icalcomponent of type ICAL_VCALENDAR_COMPONENT.
   Returned icalcomponent* should be freed by icalcomponent_free(queried_comps).

 * @param cb Calendar backend.
 * @param sync_start Start of the time window (can be NULL).
 * @param sync_stop  End of the time window (must be non-NULL).
 * @param err Error structure
 * 
 * @return 
 */
static icalcomponent*
e_cal_sync_query_server_objects(ECalBackend3e* cb,
                                const char* sync_start,
                                const char* sync_stop,
                                GError** err)
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

/** 
 * @brief Discriminates between fatal and non-fatal errors.
 * Fatal errors do not allow to continue synchronization (No write persmission on calendar),
 * non-fatal errors are errors related to individual ical component.
 * 
 * @param err Error.
 * 
 * @return TRUE, if error is fatal.
 */
static gboolean
e_cal_sync_error_is_fatal(GError* err)
{
  /* XML RPC client error */
  if (err->domain == xr_client_error_quark())
    return TRUE;

  /* 3E server error */
  if (err->domain == es_server_error_quark())
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

/** 
 * @brief Goes through the clients-changes list (list of locally modified components,
 * not yet commited to the server) and apllies the changes one-by-one. Stops on fatal error.
 * 
 * @param cb 3e calendar backend.
 * @param err Error.
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
        ok = e_cal_sync_rpc_addObject(cb, ccomp, TRUE, &local_err);
        break;
      case E_CAL_COMPONENT_LOCALLY_MODIFIED:
        ok = e_cal_sync_rpc_updateObject(cb, ccomp, TRUE, &local_err);
        break;
      case E_CAL_COMPONENT_LOCALLY_DELETED:
        if ((ok = e_cal_sync_rpc_deleteObject(cb, ccomp, TRUE, &local_err)))
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
      g_propagate_error(err, local_err);
    }
  }

  D("finishing client to server synchronization for %s: leaving %d changes",
    priv->calname,
    g_list_length(priv->sync_clients_changes));

  return ok;
}

/** 
 * @brief Wake the synchronization thread.
 * 
 * @param cb 3e calendar backend.
 */
void
server_sync_signal(ECalBackend3e* cb)
{
  ECalBackend3ePrivate* priv;

  g_return_if_fail(cb != NULL);
 
  priv = cb->priv;

  g_mutex_lock(priv->sync_mutex);
  priv->sync_mode = SYNC_DIE;

  g_cond_signal(priv->sync_cond);
  g_mutex_unlock(priv->sync_mutex);
}

/** 
 * @brief 
 * 
 * @param cb 
 * 
 * @return 
 */
ECalComponent*
e_cal_sync_find_settings(ECalBackend3e* cb)
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

/** 
 * @brief 
 * 
 * @param cb 
 * @param stamp 
 */
void
e_cal_sync_load_stamp(ECalBackend3e* cb,
                      gchar** stamp)
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

/** 
 * @brief 
 * 
 * @param cb 
 * @param stamp 
 */
void
e_cal_sync_save_stamp(ECalBackend3e* cb,
                      const char* stamp)
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

gboolean
e_cal_sync_refresh_permission(ECalBackend3e* cb, GError** err)
{
  GError*                   local_err = NULL;
  ECalBackend3ePrivate*     priv;
  GSList*                   perms;
  GSList*                   iter;
  ESPermission*             perm;

  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);
  g_return_val_if_fail(cb != NULL, FALSE);
  priv = cb->priv;
  g_return_val_if_fail(priv->conn != NULL, FALSE);

  T("cal name %s", priv->calname);

  if (priv->is_owned)
    return TRUE;

  /* if getPermissions fails, calendar will be set as read-only */
  priv->has_write_permission = FALSE;

  perms = ESClient_getPermissions(priv->conn, priv->calspec, &local_err);
  if (local_err)
  {
    g_propagate_error(err, local_err);
    return FALSE;
  }

  D("PERMISSIONS FOR CALENDAR %s", priv->calspec);
  for (iter = perms; iter; iter = iter->next)
  {
    perm = iter->data;
    g_debug("User %s permission %s", perm->user, perm->perm);

    if (((strcmp(perm->user, "*") == 0) || (strcmp(perm->user, priv->username) == 0))
        && (strcmp(perm->perm, "write") == 0))
        priv->has_write_permission = TRUE;
  }

  D("WRITE PERMISSION %d", priv->has_write_permission);

  return TRUE;
}

/** 
 * @brief 
 * 
 * @param data 
 * 
 * @return 
 */
gpointer
e_cal_sync_main_thread(gpointer data)
{
  ECalBackend3e               *cb;
  ECalBackend3ePrivate        *priv;
  GError                      *err = NULL;
  GTimeVal                    alarm_clock;

  T("");

  cb = E_CAL_BACKEND_3E(data);
  g_return_val_if_fail(cb != NULL, NULL);
  D("sync thread started");

  priv = cb->priv;
  g_mutex_lock(priv->sync_mutex);

  while (priv->sync_mode != SYNC_DIE)
  {
    if (priv->sync_mode == SYNC_SLEEP)
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
        g_clear_error(&err);
      }
      else
        g_warning("Synchronization failed with no error message");
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
  D("sync thread stops");

  return NULL;
}

/** 
 * @brief Insert the component into client-changes list (list of locally changed,
 * not yet commited components).
 * 
 * @param cb 3E calendar backend.
 * @param comp ical component.
 */
void
e_cal_sync_client_changes_insert(ECalBackend3e* cb, ECalComponent* comp)
{
  ECalBackend3ePrivate        *priv;

  g_return_if_fail(cb != NULL);
  g_return_if_fail(comp != NULL);
  priv = cb->priv;

  g_object_ref(comp);
  priv->sync_clients_changes = g_list_insert(priv->sync_clients_changes, comp, 0);

  D("marking component as changed by the client, now %d changes: %s",
    g_list_length(priv->sync_clients_changes), e_cal_component_get_as_string(comp));	
}

/** 
 * @brief Removes the component from clients-changes list (list of locally changed,
 * not yet commited components).
 * 
 * @param cb 3e calendar backend.
 * @param comp ical component.
 */
void
e_cal_sync_client_changes_remove(ECalBackend3e* cb, ECalComponent *comp)
{
  ECalBackend3ePrivate        *priv;
  GList                       *node;

  g_return_if_fail(cb != NULL);
  g_return_if_fail(comp != NULL);
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

/** 
 * @brief Builds clients-changes list from scratch: goes through the components in cache
 * and locally modified, not yet commited components are put in the list.
 * 
 * @param cb 3e calendar backend
 */
void
rebuild_clients_changes_list(ECalBackend3e* cb)
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

/** 
 * @brief Applies remote server change (represented by component scomp) to the client's cache.
 * 
 * @param cb EEE calendar backend.
 * @param scomp New component or new version of the component
 * 
 * @return Returns TRUE, if everything is ok.
 */
gboolean
e_cal_sync_mirror_server_change(ECalBackend3e* cb, icalcomponent* scomp)
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

/** 
 * @brief Collects locally changed components from cache into the hash map (key is component's UID)
 * 
 * @param cb EEE calendar backend.
 * @param remove_unchanged If TRUE, inserted component is removed from the cache.
 * 
 * @return Hash table, with locally changed components.
 */
GHashTable*
e_cal_sync_collect_cache_hash(ECalBackend3e* cb, gboolean remove_unchanged)
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
    }
  }

  g_list_foreach(cobjs, (GFunc) g_object_unref, NULL);
  g_list_free(cobjs);

  return cache_hash;
}

/** 
 * @brief Tries to resolve conflict. Conflict is the situation, when one component is changed both
 * on server and client. The newer change wins.
 * 
 * @param cb EEE calendar backend.
 * @param scomp Server's change.
 * @param ccomp Client's change.
 * 
 * @return TRUE, if no error occured.
 */
gboolean
e_cal_sync_resolve_conflict(ECalBackend3e* cb, icalcomponent* scomp, ECalComponent* ccomp)
{
  ECalBackend3ePrivate*     priv;
  time_t                    ctime;
  time_t                    stime;
  ECalComponent*            new_escomp;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(scomp != NULL, FALSE);
  g_return_val_if_fail(ccomp != NULL, FALSE);

  priv = cb->priv;
  ctime = e_cal_component_get_dtstamp_as_timet(ccomp);
  stime = icomp_get_dtstamp_as_timet(scomp);

  T("RESOLVING CONFLICT");

  if (ctime > stime)
  {
    /* client's component is newer */
    // FIXME: what to do...
    g_warning("Client's component is newer");
  }
  else
  {
    g_warning("Server's component is newer");

    // FIXME: free old ccomp ?
    /* server's component is newer */
    new_escomp = e_cal_component_new();

    if (!e_cal_component_set_icalcomponent(new_escomp, icalcomponent_new_clone(scomp)))
    {
      g_warning("Cannot parse component queried from server!");
      goto error;
    }

    e_cal_component_set_sync_state(new_escomp, E_CAL_COMPONENT_IN_SYNCH);

    if (!e_cal_backend_cache_put_component(priv->cache, new_escomp))
    {
      g_warning("Cannot put component into the cache!");
      goto error;
    }

    g_object_unref(new_escomp);
  }

  return TRUE;

error:
    g_object_unref(new_escomp);

  return FALSE;
}

/** 
 * @brief Fills the now variable with actual time in string format.
 * 
 * @param now String, 255 character long. 
 * 
 * @return TRUE, if ok.
 */
gboolean
e_cal_sync_get_now(char* now)
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

/** 
 * @brief 
 * 
 * @param cb 
 * @param incremental 
 * 
 * @return 
 */
gboolean
e_cal_sync_run_synchronization(ECalBackend3e* cb, gboolean incremental, GError** err)
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
  g_return_val_if_fail(priv->conn != NULL, FALSE);

  T("cal name %s, incremental %d, sync stamp %s", priv->calname, incremental, priv->sync_stamp);

  if (!e_cal_sync_server_open(cb, &local_err))
  {
    g_warning("Unable to open connection to the server(%d): %s",
              local_err->code, local_err->message);
    e_cal_sync_error_resolve(cb, local_err);
    goto err0;
  }

  if (!e_cal_sync_refresh_permission(cb, &local_err))
  {
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
          else if (!e_cal_sync_resolve_conflict(cb, scomp, ccomp))
          {
            g_set_error(err, E_CAL_EDS_ERROR, E_CAL_EDS_ERROR_CONFLICT,
                        "Cannot resolve conflict!");
            goto err3;
          }
        }
      }
      scomp = icalcomponent_get_next_component(server_components, bkind);
    }
  }

  /* send changes from client->server only when we have write persmissions */
  if (priv->has_write_permission)
  {
    if (!e_cal_sync_client_to_server_sync(cb, &local_err))
    {
      g_propagate_error(err, local_err);
      goto err3;
    }
  }

  icalcomponent_free(server_components);
  /* free cache_hash */
  g_free(last_sync_stamp);
  xr_client_close(priv->conn);
  e_cal_sync_save_stamp(cb, now);
  g_free(priv->sync_stamp);
  priv->sync_stamp = g_strdup(now);

  D("Synchronization OK.");
  return TRUE;

err3:
  icalcomponent_free(server_components);
err2:
  /* FIXME: free cache_hash */
err1:
  xr_client_close(priv->conn);
err0:
  g_free(last_sync_stamp);
  g_warning("Synchronization failed.");
  return FALSE;
}

/** 
 * @brief Runs total synchronization.
 * 
 * @param cb EEE calendar backend.
 * 
 * @return FALSE, if synchronization process failed, TRUE otherwise.
 */
gboolean
e_cal_sync_total_synchronization(ECalBackend3e* cb, GError** err)
{
  GError* local_err = NULL;

  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  if (!e_cal_sync_run_synchronization(cb, FALSE, &local_err))
  {
    g_warning("Total synchronization failed: %s!", local_err ? local_err->message
              : "No error message");
    g_propagate_error(err, local_err);
    return FALSE;
  }

  return TRUE;
}

/** 
 * @brief Runs incremental synchronization.
 * 
 * @param cb EEE calendar backend.
 * 
 * @return FALSE, if synchronization process failed, TRUE otherwise.
 */
gboolean
e_cal_sync_incremental_synchronization(ECalBackend3e* cb, GError** err)
{
  GError* local_err = NULL;
  gboolean go_on = TRUE;

  g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

  if (!e_cal_sync_run_synchronization(cb, TRUE, &local_err))
  {
    // FIXME: decide, whether to run total synchronization...
    g_warning("Incremental synchronization failed: %s!", local_err ? local_err->message
              : "No error message");

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
        g_warning("Total synchronization failed: %s!", local_err ? local_err->message
                  : "No error message");
        g_propagate_error(err, local_err);
        return FALSE;
      }
    }
    else
    {
      g_propagate_error(err, local_err);
      return FALSE;
    }
  }

  return TRUE;
}
