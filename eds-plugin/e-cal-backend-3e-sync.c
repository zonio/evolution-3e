#include "e-cal-backend-3e.h"
#include "e-cal-backend-3e-priv.h"
#include "e-cal-backend-3e-utils.h"
#include "e-cal-backend-3e-sync.h"

/* synchronization internal methods */
#define EEE_SYNC_STAMP "EEE-SYNC-STAMP"

ECalComponent*
e_cal_sync_find_this_in_cache(ECalBackend3e* cb,
                              ECalComponent* needle)
{
  const gchar*                uid;
  const gchar*                rid;
  ECalComponent*              found;
  ECalBackend3ePrivate*       priv;

  g_return_val_if_fail(needle != NULL, NULL);
  g_return_val_if_fail(cb != NULL, NULL);

  priv = cb->priv;

  e_cal_component_get_uid(needle, &uid);
  rid = e_cal_component_get_recurid_as_string(needle);

  return e_cal_backend_cache_get_component(priv->cache, uid, rid);
}

gboolean
e_cal_sync_server_open(ECalBackend3e* cb)
{
  ECalBackend3ePrivate*        priv;
  GError*                      err = NULL;

  g_return_val_if_fail(cb != NULL, FALSE);

  priv = cb->priv;

  xr_client_open(priv->conn, priv->server_uri, &err);
  if (err)
  {
    e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb),
                                      "Failed to estabilish connection to the server", err);
    g_clear_error(&err);
    goto err0;
  }

  // FIXME
  ESClient_auth(priv->conn, priv->username, priv->password, &err);
  //ESClient_auth(priv->conn, priv->username, "qwe", &err);
  if (err != NULL)
  {
    e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb),
                                      "Authentication failed", err);
    g_clear_error(&err);
    goto err1;
  }

  return TRUE;

err1:
  xr_client_close(priv->conn);
err0:
  return FALSE;
}

// delete component from server
gboolean
e_cal_sync_server_object_delete(ECalBackend3e* cb,
                                ECalComponent* comp,
                                gboolean conn_opened) /* connection already opened */
{
  ECalBackend3ePrivate*        priv;
  const char*                  uid;
  const char*                  rid;
  ECalComponentId *id;
  gchar*                       uid_copy;
  GError*                      err = NULL;
  ECalBackend*                 backend;
  gboolean                     ok = TRUE;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(comp != NULL, FALSE);

  priv = cb->priv;
  backend = E_CAL_BACKEND(cb);

  D("deleting from server");

  if (conn_opened || e_cal_sync_server_open(cb))
  {
    e_cal_component_get_uid(comp, &uid);
    rid = e_cal_component_get_recurid_as_string (comp);
    uid_copy = g_strdup_printf("UID:%s", uid);

    if (!ESClient_deleteObject(priv->conn, priv->calspec, uid_copy, &err))
    {
      e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb), "Failed to do cache synchronization",
                                        err);
      g_clear_error(&err);
      ok = FALSE;
    }

    if (!conn_opened)
      xr_client_close(priv->conn);
    g_free(uid_copy);
  }
  else
    return FALSE;

  return ok;
}

// update component on server
gboolean
e_cal_sync_server_object_update(ECalBackend3e* cb,
                                ECalComponent* ccomp,
                                gboolean conn_opened) /* connection already opened */
{
  gchar*                       object;
  ECalBackend3ePrivate*        priv;
  gboolean                     ok = TRUE;
  GError*                      err = NULL;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(ccomp != NULL, FALSE);

  D(" update on server");
  priv = cb->priv;

  if (conn_opened || e_cal_sync_server_open(cb))
  {
    object = e_cal_component_get_as_string(ccomp);

    if (!ESClient_updateObject(priv->conn, priv->calspec, object, &err))
    {
      e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb),
                                        "Failed to do cache synchronization",
                                        err);
      g_clear_error(&err);
      ok = FALSE;
    }
    else
    {
      e_cal_component_set_sync_state(ccomp, E_CAL_COMPONENT_IN_SYNCH);
      if (!e_cal_backend_cache_put_component(priv->cache, ccomp))
        g_warning("Cannot put component into the cache!");
    }

    if (!conn_opened)
      xr_client_close(priv->conn);
    g_free(object);
  }
  else
    return FALSE;

  return ok;
}

// add component on server
gboolean
e_cal_sync_server_object_add(ECalBackend3e* cb,
                             ECalComponent* ccomp,
                             gboolean conn_opened) /* connection already opened */
{
  gchar*                       object;
  ECalBackend3ePrivate*        priv;
  gboolean                     ok = TRUE;
  GError*                      err = NULL;

  D("add to server");
  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(ccomp != NULL, FALSE);

  priv = cb->priv;

  if (conn_opened || e_cal_sync_server_open(cb))
  {
    object = e_cal_component_get_as_string(ccomp);

    if (!ESClient_addObject(priv->conn, priv->calspec, object, &err))
    {
      e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb),
                                        "Failed to do cache synchronization", err);
      g_clear_error(&err);
      ok = FALSE;
    }
    else
    {
      e_cal_component_set_sync_state(ccomp, E_CAL_COMPONENT_IN_SYNCH);

      if (!e_cal_backend_cache_put_component(priv->cache, ccomp))
        g_warning("Cannot put component into the cache!");
    }

    g_free(object);
    if (!conn_opened)
      xr_client_close(priv->conn);
  }
  else
    return FALSE;

  return ok;
}

// free result with icalcomponent_free
static icalcomponent*
e_cal_sync_query_server_objects(ECalBackend3e* cb,
                                const char* sync_start,
                                const char* sync_stop)
{
  ECalBackend3ePrivate*       priv;
  GError*                     err = NULL;
  gchar*                      query_server_objects_str;
  icalcomponent*              queried_comps = NULL;
  gchar*                      str_server_objects;

  g_return_val_if_fail(cb != NULL, FALSE);
  g_return_val_if_fail(sync_stop != NULL, FALSE);
  // sync_start can be NULL

  priv = cb->priv;

  query_server_objects_str = sync_start
    ? g_strdup_printf("date_from('%s') and date_to('%s')", sync_start, sync_stop)
    : g_strdup_printf("date_to('%s')", sync_stop);

  str_server_objects = ESClient_queryObjects(priv->conn, priv->calspec,
                                             query_server_objects_str,
                                             &err);
  if (err)
  {
    e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb),
                                      "Cannot fetch calendar objects from server", err);
    g_clear_error(&err);
    goto out1;
  }

  // queried_comps is one big component with subcomponents
  // returned from queryObjects call
  queried_comps = icalparser_parse_string(str_server_objects);
  if (queried_comps == NULL)
  {
    g_warning("Could not parse components returned from server!");
    goto out1;
  }

  return queried_comps;

out1:
  icalcomponent_free(queried_comps);
  return NULL;
}

// create a list with server objects
// if last_synchro_time is NULL, queries all server objects
gboolean
e_cal_sync_server_to_client_sync(ECalBackend* backend,
                                 const char* sync_start,
                                 const char* sync_stop)
{
  ECalBackend3e*              cb;
  ECalBackend3ePrivate*       priv;
  gchar*                      str_server_objects;
  GError*                     err = NULL;
  gchar*                      query_server_objects_str;
  icalcomponent*              queried_comps;
  icalcomponent_kind          kind;
  icalcomponent_kind          bkind;
  icalcomponent*              scomp;
  ECalComponent*              escomp = NULL, *eccomp;
  gboolean                    ok = FALSE;
  const gchar*                rid;
  const gchar*                uid;

  g_return_val_if_fail(backend != NULL, FALSE);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  queried_comps = e_cal_sync_query_server_objects(cb, sync_start, sync_stop);

  if (queried_comps == NULL)
  {
    g_warning("Could not parse components returned from server!");
    goto out1;
  }

  kind  = icalcomponent_isa(queried_comps);
  bkind = e_cal_backend_get_kind(E_CAL_BACKEND(cb));

  if (kind == ICAL_VCALENDAR_COMPONENT)
  {
    // scomp is subcomponent of queried_comps
    scomp = icalcomponent_get_first_component (queried_comps, bkind);

    while (scomp)
    {
      T("appending new component to server list");

      // scomp will be modified to ECalComponent escomp
      escomp = e_cal_component_new();
      if (!e_cal_component_set_icalcomponent(escomp, icalcomponent_new_clone(scomp)))
      {
        g_warning("Cannot parse component queried from server!");
        goto out2;
      }
      // eccomp is corresponding ECalComponent in client's cache (if any)
      eccomp = e_cal_sync_find_this_in_cache(cb, escomp);

      if (!eccomp)
      {
        // this component was is not yet in client's cache
        if (!icomp_get_deleted_status(scomp))
        {
          // REMOTELY CREATED
          e_cal_component_set_sync_state(escomp, E_CAL_COMPONENT_IN_SYNCH);
          if (!e_cal_backend_cache_put_component(priv->cache, escomp))
          {
            g_warning("Could not put component into the cache!");
            goto out2;
          }

          char* compstr;
          compstr = e_cal_component_get_as_string(escomp);	
          e_cal_backend_notify_object_created(backend, compstr);
          g_free(compstr);
        }
        // otherwise do nothing
      }
      else
      {
        // we already have it in client's cache
        if (!icomp_get_deleted_status(scomp))
        {
          // REMOTELY MODIFIED
          e_cal_component_set_sync_state(escomp, E_CAL_COMPONENT_IN_SYNCH);
          if (!e_cal_backend_cache_put_component(priv->cache, escomp))
          {
            g_warning("Could not put component into the cache!");
            goto out2;
          }

          char* cache_comp_str;
          cache_comp_str = e_cal_component_get_as_string(eccomp);
          e_cal_backend_notify_object_modified(backend, cache_comp_str,
                                                e_cal_component_get_as_string(escomp));
          g_free(cache_comp_str);
        }
        else
        {
          // REMOTELY DELETED
          e_cal_component_get_ids(escomp, &uid, &rid);
          if (!e_cal_backend_cache_remove_component(priv->cache, uid, rid))
          {
            g_warning("Could not remove component from cache!");
            goto out2;
          }

          char* oostr = e_cal_component_get_as_string(eccomp);
          ECalComponentId *id = e_cal_component_get_id(eccomp);
          e_cal_backend_notify_object_removed(backend, id, oostr, NULL);
          e_cal_component_free_id(id);
          g_free(oostr);
        }
      }

      g_object_unref(escomp);
      scomp = icalcomponent_get_next_component(queried_comps, bkind);
    }
  }

  icalcomponent_free(queried_comps);
  ok = TRUE;
out2:
  // g_free(escomp);
  // g_free(queried_comps);
out1:

  return ok;
}

// synchronizes objects, from client's perspective
// component is only in the client's cache, not on server yet
gboolean
e_cal_sync_client_to_server_sync(ECalBackend3e* cb)
{
  gboolean                     ok = TRUE;
  ECalComponent*               ccomp;
  GList                        *citer;
  ECalBackend3ePrivate         *priv;
  GError*                   err = NULL;
  const char                   *uid, *rid;

  g_return_val_if_fail(cb != NULL, FALSE);

  priv = cb->priv;

  D("starting client to server synchronization for %s: %d changes",
    priv->calname,
    g_list_length(priv->sync_clients_changes));

  /* send changes from client -> server */
  for (citer = g_list_last(priv->sync_clients_changes);
       ok && citer;
       citer = g_list_previous(citer))
  {
    ccomp = E_CAL_COMPONENT(citer->data);

    switch (e_cal_component_get_sync_state(ccomp))
    {
      case E_CAL_COMPONENT_LOCALLY_CREATED:
        ok = e_cal_sync_server_object_add(cb, ccomp, TRUE);
        break;
      case E_CAL_COMPONENT_LOCALLY_MODIFIED:
        ok = e_cal_sync_server_object_update(cb, ccomp, TRUE);
        break;
      case E_CAL_COMPONENT_LOCALLY_DELETED:
        ok = e_cal_sync_server_object_delete(cb, ccomp, TRUE);
        if (ok)
        {
          e_cal_component_get_ids(ccomp, &uid, &rid);
          if (!e_cal_backend_cache_remove_component(priv->cache, uid, rid))
            g_warning("Could not remove component from cache!");
        }
        break;
      case E_CAL_COMPONENT_IN_SYNCH:
        g_warning("Component should be changed, but is in synch state");
        break;
      default:
        g_warning("Component should be changed, but has bad state signature!");
    }

    if (ok)
      priv->sync_clients_changes = g_list_remove(priv->sync_clients_changes, ccomp);
  }

  D("finishing client to server synchronization for %s: leaving %d changes",
    priv->calname,
    g_list_length(priv->sync_clients_changes));

  if (!ok)
    g_warning("Could not finish client->server synchronization");

  return ok;
}

// wake up synchronization thread
void
server_sync_signal(ECalBackend3e* cb,
                   gboolean stop)
{
  ECalBackend3ePrivate* priv;

  g_return_if_fail(cb != NULL);
 
  priv = cb->priv;

  g_mutex_lock(priv->sync_mutex);
  priv->sync_mode = SYNC_DIE;

  g_cond_signal(priv->sync_cond);
  g_mutex_unlock(priv->sync_mutex);
}

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
        found = comp;
  }

  g_list_free(components);

  return found;
}

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

void
e_cal_sync_incremental_synchronization(ECalBackend* backend)
{
  GError*                   err = NULL;
  ECalBackend3ePrivate*     priv;
  char                     *last_sync_stamp;
  char                      now[256];
  ECalBackend3e*            cb;
  time_t                    now_time = time(NULL);
  struct tm                tm_now;

  T("");
  gmtime_r(&now_time, &tm_now);

  g_return_if_fail(E_IS_CAL_BACKEND_3E(backend));
  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;
  g_return_if_fail(priv->conn != NULL);

  /* find out synchronization time window */
  last_sync_stamp = g_strdup(priv->sync_stamp);

  if (!(strftime(now, sizeof(now), "%F %T", &tm_now)))
    return;

  D("Synchronizing %s: %s - %s", priv->calname, last_sync_stamp ? last_sync_stamp : "", now);

  xr_client_open(priv->conn, priv->server_uri, &err);
  if (err)
  {
    e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb),
                                      "Failed to estabilish connection to the server", err);
    g_clear_error(&err);
    goto err0;
  }

  // FIXME
  ESClient_auth(priv->conn, priv->username, priv->password, &err);
  // ESClient_auth(priv->conn, priv->username, "qwe", &err);
  if (err != NULL)
  {
    e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb),
                                      "Authentication failed", err);
    g_clear_error(&err);
    goto err1;
  }

  D("server to client synchronization %s", priv->calname);
  /* get changes from server */
  if (!e_cal_sync_server_to_client_sync(backend, last_sync_stamp, now))
    goto err1;

  if (!e_cal_sync_client_to_server_sync(cb))
    goto err1;

  xr_client_close(priv->conn);
  e_cal_sync_save_stamp(cb, now);
  g_free(priv->sync_stamp);
  priv->sync_stamp = g_strdup(now);
  g_free(last_sync_stamp);

  D("syncho ended");
  return;


err1:
  g_free(last_sync_stamp);
  xr_client_close(priv->conn);
err0:
  /* FIXME: go to offline mode */
  return;
}

// synchro thread main
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

    e_cal_sync_total_synchronization(cb);

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

/* calendar backend functions */

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

void
e_cal_sync_client_changes_remove(ECalBackend3e* cb, ECalComponent *comp)
{
  ECalBackend3ePrivate        *priv;
  GList                       *node;

  g_return_if_fail(cb != NULL);
  g_return_if_fail(comp != NULL);

  priv = cb->priv;

  node = g_list_find_custom(priv->sync_clients_changes,
                            comp,
                            e_cal_component_compare);

  if (!node)
  {
    g_warning("Searched component was not marked as changed - cannot remove it");
    return;
  }

  priv->sync_clients_changes = g_list_delete_link(priv->sync_clients_changes,
                                                  node);
  // g_object_unref(comp);

  D("unmarking component as changed, now %d changes",
    g_list_length(priv->sync_clients_changes));
}

void
rebuild_clients_changes_list(ECalBackend3e* cb)
{
  ECalBackend3ePrivate        *priv;
  ECalComponentSyncState      state;
  ECalComponent               *ccomp;
  GList                       *cobjs, *citer;

  T("");
  priv = cb->priv;

  if (priv->sync_clients_changes)
  {
    g_list_foreach(priv->sync_clients_changes, (GFunc)g_object_unref, NULL);
    g_list_free(priv->sync_clients_changes);
  }

  priv->sync_clients_changes = NULL;

  cobjs = e_cal_backend_cache_get_components(priv->cache);

  D("rebuild client's changes list: %s", priv->calname);

  for (citer = cobjs; citer; citer = g_list_next(citer))
  {
    ccomp = E_CAL_COMPONENT(citer->data);
    state = e_cal_component_get_sync_state(ccomp);

    if (state != E_CAL_COMPONENT_IN_SYNCH)
      e_cal_sync_client_changes_insert(cb, ccomp);
    else
      g_object_unref(ccomp);
  }

  D("done");

  g_list_free(cobjs);
}

void
e_cal_sync_total_synchronization(ECalBackend3e* cb)
{
  ECalBackend3ePrivate*     priv = cb->priv;
  ECalBackendCache         *bcache = priv->cache;
  GError*                   err = NULL;
  GList                    *cobjs;
  GList                    *citer;
  GList                    *sobjs = NULL;
  GList                    *siter;
  icalcomponent            *scomp;
  icalcomponent            *server_components;
  const char               *uid;	
  const char               *rid;
  char                     *uid_copy;
  ECalComponent            *ccomp = NULL;
  gboolean                 ok;
  ECalComponentSyncState   sync_state;
  icalcomponent_kind       kind;
  icalcomponent_kind       bkind;
  ECalComponent*           escomp;
  char                     now[256];
  time_t                   now_time = time(NULL);
  struct tm                tm_now;
  GHashTable               *uidindex;

  T("");
  D("starting TOTAL synchronization!");

  g_return_if_fail(cb != NULL);
  priv = cb->priv;
  g_return_if_fail(priv->conn != NULL);

  gmtime_r(&now_time, &tm_now);
  if (!(strftime(now, sizeof(now), "%F %T", &tm_now)))
    return;

  if (!e_cal_sync_server_open(cb))
    return;

  uidindex = g_hash_table_new(g_str_hash, g_str_equal);
  cobjs = e_cal_backend_cache_get_components(bcache);
  for (citer = cobjs; citer; citer = g_list_next (citer))
  {
    ccomp = E_CAL_COMPONENT (citer->data);
    sync_state = e_cal_component_get_sync_state(ccomp);
    e_cal_component_get_ids(ccomp, &uid, &rid);

    if (sync_state == E_CAL_COMPONENT_IN_SYNCH)
    {
      if (!e_cal_backend_cache_remove_component(priv->cache, uid, rid))
        g_warning("Could not remove component from cache!");
    }
    else
    {
      uid_copy = g_strdup_printf("UID:%s", uid);
      g_hash_table_insert(uidindex, (gpointer)uid_copy, ccomp);
    }
  }

  server_components = e_cal_sync_query_server_objects(cb, NULL, now);
  if (!server_components)
    goto out1;

  kind  = icalcomponent_isa(server_components);
  bkind = e_cal_backend_get_kind(E_CAL_BACKEND(cb));

  if (kind == ICAL_VCALENDAR_COMPONENT)
  {
    // scomp is subcomponent of queried_comps
    scomp = icalcomponent_get_first_component(server_components, bkind);

    while (scomp)
    {
      uid = icomp_get_uid(scomp);
      uid_copy = g_strstrip(g_strdup(uid));

      if (!icomp_get_deleted_status(scomp) && !g_hash_table_lookup(uidindex, uid_copy))
      {
        escomp = e_cal_component_new();
        if (!e_cal_component_set_icalcomponent(escomp, icalcomponent_new_clone(scomp)))
        {
          g_warning("Cannot parse component queried from server!");
          goto out2;
        }
        e_cal_component_set_sync_state(escomp, E_CAL_COMPONENT_IN_SYNCH);
        D("INSERTING COMPONENT");
        if (!e_cal_backend_cache_put_component(priv->cache, escomp))
          g_warning("Cannot put component into the cache!");
        else
        {
          D("INSERTED OK");
          char* compstr;
          compstr = e_cal_component_get_as_string(escomp);	
          e_cal_backend_notify_object_created(E_CAL_BACKEND(cb), compstr);
          g_free(compstr);
        }
        g_object_unref(escomp);
      }
      scomp = icalcomponent_get_next_component(server_components, bkind);
    }
  }

  if (!e_cal_sync_client_to_server_sync(cb))
    goto out1;

  g_list_foreach(cobjs, (GFunc) g_object_unref, NULL);
  g_list_free(cobjs);

out2:
out1:
  xr_client_close(priv->conn);
}
