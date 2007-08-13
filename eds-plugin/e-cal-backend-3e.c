/**************************************************************************************************
 *  3E plugin for Evolution Data Server                                                           * 
 *                                                                                                *
 *  Copyright (C) 2007 by Zonio                                                                   *
 *  www.zonio.net                                                                                 *
 *  stanislav.slusny@zonio.net                                                                    *
 *                                                                                                *
 **************************************************************************************************/

#include <config.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-util.h>
#include <libedataserver/e-url.h>
#include <libecal/e-cal-time-util.h>
#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-sexp.h>

#include "e-cal-backend-3e.h"
#include "e-cal-backend-3e-utils.h"
#include "e-cal-backend-3e-sync.h"
#include "e-cal-backend-3e-priv.h"
#include "dns-txt-search.h"

extern char *e_passwords_get_password (const char *component, const char *key);

#include "interface/ESClient.xrc.h"

/*
   Returns the capabilities provided by the backend,
   like whether it supports recurrences or not, for instance
   */
static ECalBackendSyncStatus
e_cal_backend_3e_get_static_capabilities (ECalBackendSync * backend,
                                          EDataCal * cal, char **capabilities)
{
  T ("backend=%p, cal=%p, capabilities=%p", backend, cal, capabilities);

  g_return_val_if_fail (capabilities != NULL,
                        GNOME_Evolution_Calendar_OtherError);

  *capabilities = g_strdup ("");

  return GNOME_Evolution_Calendar_Success;
}

/*
 * Returns TRUE if the the passed-in backend is already in a loaded state, otherwise FALSE
 */
static gboolean
e_cal_backend_3e_is_loaded (ECalBackend * backend)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;

  T ("backend=%p", backend);
  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  return priv->is_loaded;
}

/*
 * Sets the current online/offline mode.
 */
static void
e_cal_backend_3e_set_mode (ECalBackend * backend, CalMode mode)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  GNOME_Evolution_Calendar_CalMode                  set_mode;
  GNOME_Evolution_Calendar_CalListener_SetModeStatus status;

  T ("backend=%p, mode=%d", backend, mode);
  g_return_if_fail (backend != NULL);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  if (priv->mode == mode)
  {
    D ("Not changing mode");
    e_cal_backend_notify_mode (backend,
                               GNOME_Evolution_Calendar_CalListener_MODE_SET,
                               cal_mode_to_corba (mode));
    return;
  }

  g_mutex_lock (priv->sync_mutex);

  if (mode == CAL_MODE_REMOTE)
  {
    D ("going ONLINE");
    priv->mode = CAL_MODE_REMOTE;
    priv->sync_mode = SYNC_WORK;
    g_cond_signal (priv->sync_cond);
    set_mode = cal_mode_to_corba (GNOME_Evolution_Calendar_MODE_REMOTE);
    status = GNOME_Evolution_Calendar_CalListener_MODE_SET;
  }
  else if (mode == CAL_MODE_LOCAL)
  {
    D ("going OFFLINE");
    priv->sync_mode = SYNC_SLEEP;
    priv->mode = CAL_MODE_LOCAL;
    set_mode = cal_mode_to_corba (GNOME_Evolution_Calendar_MODE_LOCAL);
    status = GNOME_Evolution_Calendar_CalListener_MODE_SET;
  }
  else
  {
    D ("NOT SUPPORTED");
    status = GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED;
    set_mode = cal_mode_to_corba (mode);
  }

  e_cal_backend_notify_mode (backend, status, set_mode);

  g_mutex_unlock (priv->sync_mutex);
}

static void
source_changed_perm(ESource *source, ECalBackend3e *cb)
{
  /* FIXME
   * We should force a reload of the data when this gets called. Unfortunately,
   * this signal isn't getting through from evolution to the backend
   */
  g_debug("SOURCE CHANGED");
}

/* initilizes priv structure of the cb */
static ECalBackendSyncStatus
e_cal_backend_initialize_privates(ECalBackend3e * cb, const char* username, const char* password)
{
  GError                                           *err = NULL;
  ECalBackend3ePrivate                             *priv;
  ESource                                          *source;
  char                                             *server_hostname;
  const char                                       *cal_name;

  g_return_val_if_fail (cb != NULL, GNOME_Evolution_Calendar_OtherError);
  T("");
  priv = cb->priv;
  source = e_cal_backend_get_source(E_CAL_BACKEND(cb));

  g_free(priv->username);
  g_free(priv->password);
  /* find out username and password before any attempts to initialize cache */
  if (!username || *username == 0)
  {
    priv->username = g_strdup(e_source_get_property (source, "username"));
    priv->password = e_passwords_get_password(EEE_PASSWORD_COMPONENT,
                                              e_source_get_property(source, "auth-key"));
  }
  else
  {
    priv->username = g_strdup(username);
    priv->password = g_strdup(password);
  }

  /* username has to be initialized already, because we look up hostname
   * in dns txt records by username */
  if (!priv->username || *priv->username == 0)
    return GNOME_Evolution_Calendar_OtherError;

  priv->cache = e_cal_backend_cache_new(e_cal_backend_get_uri(E_CAL_BACKEND(cb)),
                                        E_CAL_SOURCE_TYPE_EVENT);
  if (!priv->cache)
  {
    e_cal_backend_notify_error (E_CAL_BACKEND (cb), "Could not create cache file");
    return GNOME_Evolution_Calendar_OtherError;
  }

  /*
   * server_hostname = e_source_get_property(source, "eee-server");
   *
   * This method sometimes does not work (race condition...).
   * So we find out server hostname from dns record by ourselves,
   * we will not ask evolution.
   */

  g_free (priv->server_uri);
  g_free (priv->calname);
  g_free (priv->owner);
  cal_name = e_source_get_property (source, "eee-calname");
  server_hostname = get_eee_server_hostname(priv->username);
  priv->server_uri = g_strdup_printf ("https://%s/ESClient", server_hostname);
  g_free (server_hostname);
  priv->calname = g_strdup (cal_name);
  priv->settings = e_cal_sync_find_settings(cb);
  priv->owner = g_strdup (e_source_get_property (source, "eee-owner"));
  g_return_val_if_fail (priv->owner != NULL, GNOME_Evolution_Calendar_OtherError);
  priv->is_owned = strcmp(priv->username, priv->owner) == 0;
  if (priv->is_owned)
    priv->has_write_permission = TRUE;
  else
    priv->has_write_permission = strcmp(e_source_get_property(source, "eee-perm"), "write") == 0;

  if (priv->source_changed_perm == 0)
    priv->source_changed_perm = g_signal_connect(G_OBJECT(source), "changed",
                                                 G_CALLBACK (source_changed_perm), cb);


  if (server_hostname == NULL || cal_name == NULL)
  {
    /*
     * This sometimes happens ... When starting evolution in offline mode,
     * we do not know this... do not throw error message.
     *
     * */
    /*
       e_cal_backend_notify_error (E_CAL_BACKEND (cb),
       "Invalid calendar source list setup.");
    */

    return GNOME_Evolution_Calendar_OtherError;
  }

  if (priv->conn == NULL)
  {
    priv->conn = xr_client_new (&err);
    if (err != NULL)
    {
      e_cal_backend_notify_gerror_error (E_CAL_BACKEND (cb),
                                         "Failed to initialize XML-RPC client library.", err);
      return GNOME_Evolution_Calendar_OtherError;
    }
  }

  g_free (priv->calspec);
  g_free (priv->sync_stamp);
  priv->calspec = g_strdup_printf ("%s:%s", priv->owner, priv->calname);
  priv->is_open = TRUE;
  e_cal_sync_load_stamp(cb, &priv->sync_stamp);
  priv->is_loaded = TRUE;

  return GNOME_Evolution_Calendar_Success;
}

/*
 * opens the calendar
 */
static ECalBackendSyncStatus
e_cal_backend_3e_open (ECalBackendSync * backend,
                       EDataCal * cal,
                       gboolean only_if_exists,
                       const char *username, const char *password)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  ECalBackendSyncStatus                             status;
  ESourceList                                      *eslist;
  GSList                                           *groups_list;
  GSList                                           *iter;
  GSList                                           *p;
  const char                                       *group_name;
  const char                                       *cal_name;
  ESourceGroup                                     *group;
  ESource                                          *source;
  GError                                           *local_err = NULL;

  T ("backend=%p, cal=%p, only_if_exists=%d, username=%s, password=%s",
     backend, cal, only_if_exists, username, password);

  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  g_mutex_lock (priv->sync_mutex);

  status = (priv->is_loaded == TRUE)
    ? GNOME_Evolution_Calendar_Success : e_cal_backend_initialize_privates(cb, username, password);

  if (status != GNOME_Evolution_Calendar_Success)
    goto out;

  if (!priv->calname)
  {
    status = GNOME_Evolution_Calendar_OtherError;
    goto out;
  }

  D ("username  %s", priv->username);
  D ("calname %s", priv->calname);
  D ("password %s", priv->password);
  D ("owner %s", priv->owner);
  D ("server uri %s", priv->server_uri);

  if (status != GNOME_Evolution_Calendar_Success)
    goto out;

  e_cal_sync_rebuild_clients_changes_list(cb);

  if (!priv->sync_thread)
    cb->priv->sync_thread = g_thread_create (e_cal_sync_main_thread, cb, TRUE, NULL);

  if (priv->mode == CAL_MODE_REMOTE)
  {
    /* do the synchronization and wait until it ends */
    if (!e_cal_sync_incremental_synchronization(cb, &local_err))
    {
      if (local_err)
      {
        g_warning("Synchronization failed with message %s", local_err->message);
        g_clear_error(&local_err);
      }
      else
        g_warning("Synchronization failed with no error message");

      status = GNOME_Evolution_Calendar_OtherError;
      goto out;
    }

    /* run synchronization thread */
    priv->sync_mode = SYNC_WORK;
    g_cond_signal(priv->sync_cond);
  }
  else
    priv->sync_mode = SYNC_SLEEP;

  /*
   * solve the default zone: find out, if the default zone is in the cache or not.
   * if it is not, put it there with E_CAL_COMPONENT_LOCALLY_CREATED status.
   */
  if (priv->default_zone)
  {
    char* default_tzid = icaltimezone_get_tzid(priv->default_zone);

    const icaltimezone* cache_zone = e_cal_backend_cache_get_timezone(priv->cache, default_tzid);
    if (!cache_zone)
    {
      /* default zone is not in cache yet. add it there */
      icalcomponent* default_tzcomp = icaltimezone_get_component(priv->default_zone);
      icomp_set_sync_state(default_tzcomp, E_CAL_COMPONENT_LOCALLY_CREATED);
      e_cal_backend_cache_put_default_timezone(priv->cache, priv->default_zone);
      e_cal_backend_cache_put_timezone(priv->cache, priv->default_zone);
    }
  }

out:
  g_mutex_unlock (priv->sync_mutex);
  T("FINISHED");

  return status;
}

/*
 *  returns whether the calendar is read only or not
 */
static ECalBackendSyncStatus
e_cal_backend_3e_is_read_only (ECalBackendSync * backend,
                               EDataCal * cal, gboolean * read_only)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  ESource                                          *source;

  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail (read_only != NULL,
                        GNOME_Evolution_Calendar_OtherError);

  /*
  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;
  *read_only = priv->is_open == FALSE || priv->has_write_permission == FALSE;
  D("READONLY %d", *read_only);
  */
  *read_only = FALSE;

  T ("backend=%p, cal=%p, read_only=%s", backend, cal,
     *read_only ? "true" : "false");

  return GNOME_Evolution_Calendar_Success;
}

/*
 *  returns the current online/offline mode for the backend
 */
static CalMode
e_cal_backend_3e_get_mode (ECalBackend * backend)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;

  T ("backend=%p", backend);
  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  return priv->mode;
}

/*
 *  removes the calendar
 */
static ECalBackendSyncStatus
e_cal_backend_3e_remove (ECalBackendSync * backend, EDataCal * cal)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  gchar                                            *path;
  GError                                           *err;

  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);
  T("");

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  g_mutex_lock (priv->sync_mutex);

  if (priv->is_loaded != TRUE)
  {
    g_mutex_unlock (priv->sync_mutex);
    return GNOME_Evolution_Calendar_Success;
  }

  e_file_cache_remove (E_FILE_CACHE (priv->cache));
  priv->cache = NULL;
  priv->is_loaded = FALSE;
  priv->sync_terminated = TRUE;

  g_mutex_unlock (priv->sync_mutex);

  return GNOME_Evolution_Calendar_Success;
}

/*
 *  returns the email address of the owner of the calendar
 */
static ECalBackendSyncStatus
e_cal_backend_3e_get_cal_address (ECalBackendSync * backend,
                                  EDataCal * cal, char **address)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;

  T ("backend=%p, cal=%p, address=%s", backend, cal, *address);

  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail (address != NULL, GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;
  *address = g_strdup (priv->username);

  return GNOME_Evolution_Calendar_Success;
}

/*
 *  returns the email address to be used for alarms
 */
static ECalBackendSyncStatus
e_cal_backend_3e_get_alarm_email_address (ECalBackendSync * backend,
                                          EDataCal * cal, char **address)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;

  T ("backend=%p, cal=%p, address=%s", backend, cal, *address);

  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail (address != NULL, GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  *address = g_strdup (priv->username);

  return GNOME_Evolution_Calendar_Success;
}

/*
 * returns an empty object with the default values used for the backend
 * called when creating new object, for example
 */
static ECalBackendSyncStatus
e_cal_backend_3e_get_default_object (ECalBackendSync * backend,
                                     EDataCal * cal, char **object)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  icalcomponent                                    *icalcomp;
  icalcomponent_kind                                kind;

  T ("backend=%p, cal=%p, object=%s", backend, cal, *object);

  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail (object != NULL, GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));
  icalcomp = e_cal_util_new_component (kind);
  *object = g_strdup (icalcomponent_as_ical_string (icalcomp));
  icalcomponent_free (icalcomp);

  return GNOME_Evolution_Calendar_Success;
}

/*
 *  returns a list of events/tasks given a set of conditions
 */
  static ECalBackendSyncStatus
e_cal_backend_3e_get_object (ECalBackendSync * backend,
                             EDataCal * cal,
                             const char *uid, const char *rid, char **object)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  ECalComponent                                    *comp = NULL;

  T ("backend=%p, cal=%p, uid=%s, rid=%s", backend, cal, uid, rid);

  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail (object != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  if (!priv->cache)
    return GNOME_Evolution_Calendar_ObjectNotFound;

  g_mutex_lock (priv->sync_mutex);
  comp = e_cal_backend_cache_get_component (priv->cache, uid, rid);
  g_mutex_unlock (priv->sync_mutex);

  if (!comp)
    return GNOME_Evolution_Calendar_ObjectNotFound;

  *object = e_cal_component_get_as_string (comp);
  g_object_unref (comp);

  return GNOME_Evolution_Calendar_Success;
}

/*
 *  returns timezone objects for a given TZID
 */
static ECalBackendSyncStatus
e_cal_backend_3e_get_timezone (ECalBackendSync * backend,
                               EDataCal * cal,
                               const char *tzid, char **object)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  const icaltimezone                               *zone;
  icalcomponent                                    *icalcomp;

  T ("backend=%p, cal=%p, tzid=%s", backend, cal, tzid);

  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);

  g_return_val_if_fail (tzid != NULL,
                        GNOME_Evolution_Calendar_ObjectNotFound);

  g_return_val_if_fail (object != NULL,
                        GNOME_Evolution_Calendar_ObjectNotFound);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;


  /* first try to get the timezone from the cache */
  g_mutex_lock (priv->sync_mutex);
  zone = e_cal_backend_cache_get_timezone (priv->cache, tzid);
  g_mutex_unlock (priv->sync_mutex);

  if (!zone)
  {
    zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
    if (!zone)
      return GNOME_Evolution_Calendar_ObjectNotFound;
  }

  icalcomp = icaltimezone_get_component ((icaltimezone *) zone);

  if (!icalcomp)
    return GNOME_Evolution_Calendar_InvalidObject;

  *object = g_strdup (icalcomponent_as_ical_string (icalcomp));

  return GNOME_Evolution_Calendar_Success;
}

/*
 *  returns specific LDAP attributes
 */
static ECalBackendSyncStatus
e_cal_backend_3e_get_ldap_attribute (ECalBackendSync * backend,
                                     EDataCal * cal, char **attribute)
{
  T ("backend=%p, cal=%p", backend, cal);

  *attribute = NULL;

  return GNOME_Evolution_Calendar_OtherError;
}

/*
 *  adds a timezone to the backend
 */
static ECalBackendSyncStatus
e_cal_backend_3e_add_timezone (ECalBackendSync * backend,
                               EDataCal * cal, const char *tzobj)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  icalcomponent                                    *tz_comp;
  icaltimezone                                     *tz, *zone;

  T ("backend=%p, cal=%p, tzobj=%s", backend, cal, tzobj);
  cb = (ECalBackend3e *) backend;

  g_return_val_if_fail (E_IS_CAL_BACKEND_3E (cb), GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

  priv = cb->priv;

  tz_comp = icalparser_parse_string (tzobj);
  if (!tz_comp)
    return GNOME_Evolution_Calendar_InvalidObject;

  if (icalcomponent_isa (tz_comp) != ICAL_VTIMEZONE_COMPONENT)
  {
    icalcomponent_free (tz_comp);
    return GNOME_Evolution_Calendar_InvalidObject;
  }

  zone = icaltimezone_new();
  icaltimezone_set_component(zone, tz_comp);
  const char* tzid = icaltimezone_get_tzid(zone);

  g_mutex_lock (priv->sync_mutex);

  const icaltimezone* cache_zone = e_cal_backend_cache_get_timezone(priv->cache, tzid);

  if (!cache_zone)
  {
    icomp_set_sync_state(tz_comp, E_CAL_COMPONENT_LOCALLY_CREATED);
    e_cal_backend_cache_put_timezone (priv->cache, zone);
  }

  g_mutex_unlock (priv->sync_mutex);

  return GNOME_Evolution_Calendar_Success;
}

/*
 *  returns a list of events/tasks given a set of conditions
 */
static ECalBackendSyncStatus
e_cal_backend_3e_get_object_list (ECalBackendSync * backend,
                                  EDataCal * cal,
                                  const char *sexp, GList ** objects)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  GList                                            *comps_in_cache, *l;
  ECalBackendSExp                                  *cbsexp;

  T ("backend=%p, cal=%p, sexp=%s", backend, cal, sexp);

  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  if (!priv->cache)
    return GNOME_Evolution_Calendar_NoSuchCal;

  g_mutex_lock (priv->sync_mutex);

  /* process all components in the cache */
  cbsexp = e_cal_backend_sexp_new (sexp);

  *objects = NULL;
  comps_in_cache = e_cal_backend_cache_get_components (priv->cache);

  for (l = comps_in_cache; l != NULL; l = l->next)
  {
    /*
     *  FIXME: what about locally deleted objects ?
     */

    if (e_cal_backend_sexp_match_comp(cbsexp, E_CAL_COMPONENT (l->data), E_CAL_BACKEND (backend)))
      *objects = g_list_append (*objects, e_cal_component_get_as_string (l->data));
  }

  g_list_foreach(comps_in_cache, (GFunc) g_object_unref, NULL);
  g_list_free(comps_in_cache);
  g_object_unref(cbsexp);

  g_mutex_unlock (priv->sync_mutex);

  return GNOME_Evolution_Calendar_Success;
}

/*
 *  starts a live query on the backend
 */
static void
e_cal_backend_3e_start_query (ECalBackend * backend, EDataCalView * query)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  GList                                            *components, *l, *objects =
      NULL;
  ECalBackendSExp                                  *cbsexp;
  ECalComponent                                    *comp;
  ECalComponentSyncState                            state;

  g_return_if_fail (backend != NULL);
  g_return_if_fail (query != NULL);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  if (!priv->cache)
  {
    e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_NoSuchCal);
    return;
  }

  /* process all components in the cache */
  const char *text = e_data_cal_view_get_text (query);
  cbsexp = e_cal_backend_sexp_new (text);
  objects = NULL;

  g_mutex_lock (priv->sync_mutex);

  components = e_cal_backend_cache_get_components (priv->cache);

  for (l = components; l != NULL; l = l->next)
  {
    comp = E_CAL_COMPONENT (l->data);
    state = e_cal_component_get_sync_state (comp);

    /*
    if (e_cal_backend_sexp_match_comp(cbsexp, comp, E_CAL_BACKEND (backend))
        && state != E_CAL_COMPONENT_LOCALLY_DELETED)
      objects = g_list_append (objects, e_cal_component_get_as_string (l->data));
      */
    if (e_cal_backend_sexp_match_comp(cbsexp, comp, E_CAL_BACKEND (backend))
        && !e_cal_component_has_deleted_status(comp))
      objects = g_list_append (objects, e_cal_component_get_as_string (l->data));

  }

  e_data_cal_view_notify_objects_added (query, (const GList *) objects);

  g_list_foreach (components, (GFunc) g_object_unref, NULL);
  g_list_free (components);

  g_list_foreach (objects, (GFunc) g_free, NULL);
  g_list_free (objects);
  g_object_unref (cbsexp);

  e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_Success);
  g_mutex_unlock (priv->sync_mutex);
}

/*
 * adds object to the server
 */
static ECalBackendSyncStatus
e_cal_backend_3e_server_object_add(ECalBackend3e* cb, ECalComponent* comp, char** new_object,
                                   GError** err)
{
  ECalBackend3ePrivate                             *priv;
  ECalBackendSyncStatus                             status = GNOME_Evolution_Calendar_Success;
  GError                                           *local_err = NULL;
  gboolean                                          mark_as_changed = FALSE;

  g_return_val_if_fail(cb != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(comp != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(new_object != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(comp), GNOME_Evolution_Calendar_InvalidObject);

  T("");
  priv = cb->priv;
  status = GNOME_Evolution_Calendar_Success;
  *new_object = NULL;

  if (priv->mode == CAL_MODE_REMOTE)
  {
    /* send to server */
    if (!e_cal_sync_rpc_addObject(cb, comp, FALSE, &local_err))
    {
      if (local_err)
        g_propagate_error(err, local_err);
      /* could not send change to server, component always contains changes */
      mark_as_changed = TRUE;
    }
    else
      /* clear changed flag */
      e_cal_component_set_sync_state (comp, E_CAL_COMPONENT_IN_SYNCH);
  }
  else
    /* in local mode just mark as changed */
    mark_as_changed = TRUE;

  if (mark_as_changed)
  {
    e_cal_component_set_sync_state(comp, E_CAL_COMPONENT_LOCALLY_CREATED);
    e_cal_sync_client_changes_insert(cb, comp);
  }

  if (!e_cal_backend_cache_put_component(priv->cache, comp))
  {
    g_warning("Could not put component to the cache, when creating object.");
    status = GNOME_Evolution_Calendar_InvalidObject;
  }
  else
  {
    /* notify change */
    *new_object = e_cal_component_get_as_string (comp);
    e_cal_backend_notify_object_created(E_CAL_BACKEND(cb), *new_object);
  }

  return status;
}

/*
 *  sets the timezone to be used as the default
 *  called before opening connection, before creating cache...
 */
  static ECalBackendSyncStatus
e_cal_backend_3e_set_default_zone (ECalBackendSync * backend,
                                   EDataCal * cal, const char *tzobj)
{
  icalcomponent                                    *tz_comp;
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  icaltimezone                                     *zone;
  const icaltimezone                               *cache_zone;
  ECalComponent                                    *ecomp;
  char                                             *new_object;
  GError                                           *local_error = NULL;
  ECalBackendSyncStatus                            status = GNOME_Evolution_Calendar_OtherError;
  char                                             *tzid;

  T ("SETTING DEFAULT ZONE: tzobj=%s", tzobj);

  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);

  cb = (ECalBackend3e *) backend;

  g_return_val_if_fail (E_IS_CAL_BACKEND_3E (cb),
                        GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);


  priv = cb->priv;

  if (!(tz_comp = icalparser_parse_string (tzobj)))
    return GNOME_Evolution_Calendar_InvalidObject;

  zone = icaltimezone_new();
  icaltimezone_set_component(zone, tz_comp);

  g_mutex_lock(priv->sync_mutex);
  if (priv->default_zone)
    icaltimezone_free (priv->default_zone, 1);
  priv->default_zone = zone;
  g_mutex_unlock(priv->sync_mutex);

  return GNOME_Evolution_Calendar_Success;
}


/*
 *  creates a new event/task in the calendar
 */
static ECalBackendSyncStatus
e_cal_backend_3e_create_object (ECalBackendSync * backend,
                                EDataCal * cal, char **calobj, char **uid)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  ECalComponent                                    *comp;
  ECalBackendSyncStatus                             status = GNOME_Evolution_Calendar_Success;
  GError                                           *local_err = NULL;

  T ("backend=%p, cal=%p, calobj=%s, uid=%s", backend, cal, *calobj, *uid);

  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  g_mutex_lock (priv->sync_mutex);

  if (!priv->has_write_permission)
  {
    status = GNOME_Evolution_Calendar_PermissionDenied;
    goto out;
  }

  if (!(comp = e_cal_component_new_from_string (*calobj)))
  {
    status = GNOME_Evolution_Calendar_InvalidObject;
    goto out;
  }

  status = e_cal_backend_3e_server_object_add(cb, comp, calobj, &local_err);
  if (local_err)
  {
    e_cal_sync_error_message(E_CAL_BACKEND(cb), comp, local_err);
    g_error_free(local_err);
  }
  
  g_object_unref(comp);

out:
  g_mutex_unlock (priv->sync_mutex);

  return status;
}

/*
 * Updates component. In remote mode, tries to send component to the server.
 * In local mode, just markes component as changed.
 * If in remote and xml-rpc operation failes, component  is marked as changed
 * similary as in local mode.
 */
static ECalBackendSyncStatus
e_cal_backend_3e_server_object_update(ECalBackend3e* cb, ECalComponent* cache_comp,
                                      ECalComponent* updated_comp,
                                      char** old_object,
                                      char** new_object,
                                      GError** err)
{
  ECalBackend3ePrivate                *priv;
  GError                              *local_err = NULL;
  ECalComponentSyncState              cache_comp_state;
  ECalBackendSyncStatus               status = GNOME_Evolution_Calendar_Success;
  gboolean                            mark_as_changed = FALSE;

  priv = cb->priv;
  T("");

  g_return_val_if_fail(cb != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(cache_comp != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(cache_comp), GNOME_Evolution_Calendar_InvalidObject);
  g_return_val_if_fail(updated_comp != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(updated_comp), GNOME_Evolution_Calendar_InvalidObject);
  g_return_val_if_fail(old_object != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(new_object != NULL, GNOME_Evolution_Calendar_OtherError);

  cache_comp_state = e_cal_component_get_sync_state(cache_comp);

  if (priv->mode == CAL_MODE_REMOTE)
  {
    switch (cache_comp_state)
    {
      case E_CAL_COMPONENT_LOCALLY_CREATED:
        if (!e_cal_sync_rpc_addObject(cb, updated_comp, FALSE, &local_err))
        {
          if (local_err)
            g_propagate_error(err, local_err);
          mark_as_changed = TRUE;
        }
        break;
      default:
        if (!e_cal_sync_rpc_updateObject(cb, updated_comp, FALSE, &local_err))
        {
          if (local_err)
            g_propagate_error(err, local_err);
          mark_as_changed = TRUE;
        }
        break;
    }
  }
  else
    mark_as_changed = TRUE;

  if (mark_as_changed)
  {
    if (cache_comp_state != E_CAL_COMPONENT_IN_SYNCH)
    {
      /*
       * original version of component (cache_comp) was out of synch already,
       * we have to remove it from changes list and insert the new updated version
       */
      e_cal_sync_client_changes_remove(cb, cache_comp);
    }

    /* 
     * (When updating component in state E_CAL_COMPONENT_LOCALLY_CREATED, we do not change the
     * of the component - component is not on server yet.)
     * */
    if (cache_comp_state  == E_CAL_COMPONENT_LOCALLY_CREATED)
      e_cal_component_set_sync_state (updated_comp, E_CAL_COMPONENT_LOCALLY_CREATED);
    else
      e_cal_component_set_sync_state (updated_comp, E_CAL_COMPONENT_LOCALLY_MODIFIED);

    e_cal_sync_client_changes_insert(cb, updated_comp);
  }

  if (!e_cal_backend_cache_put_component(priv->cache, updated_comp))
  {
    g_warning ("Error when removing component, cannot put new"
               "component component into the cache!");

    status = GNOME_Evolution_Calendar_OtherError;
  }
  else
  {
    *old_object = e_cal_component_get_as_string(cache_comp);
    *new_object = e_cal_component_get_as_string(updated_comp);
    e_cal_backend_notify_object_modified(E_CAL_BACKEND(cb), *old_object, *new_object);
  }

  return status;
}

/*
 *  modifies an existing event/task
 */
static ECalBackendSyncStatus
e_cal_backend_3e_modify_object (ECalBackendSync * backend,
                                EDataCal * cal,
                                const char *calobj,
                                CalObjModType mod,
                                char **old_object, char **new_object)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  ECalComponent                                    *updated_comp;
  ECalComponent                                    *cache_comp;
  gboolean                                          online;
  const char                                       *uid = NULL;
  ECalBackendSyncStatus                             status = GNOME_Evolution_Calendar_Success;
  GError                                           *local_err = NULL;

  T ("backend=%p, cal=%p, calobj=%s", backend, cal, calobj);

  g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);
  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail (old_object != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail (new_object != NULL, GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  g_mutex_lock (priv->sync_mutex);

  if (!priv->has_write_permission)
  {
    // status = GNOME_Evolution_Calendar_PermissionDenied;
     status = GNOME_Evolution_Calendar_OtherError;
    goto out;
  }

  if (!(updated_comp = e_cal_component_new_from_string(calobj)))
  {
    status = GNOME_Evolution_Calendar_InvalidObject;
    goto out;
  }

  e_cal_component_get_uid(updated_comp, &uid);
  e_cal_component_unset_local_state(E_CAL_BACKEND(backend), updated_comp);
  
  if (!(cache_comp = e_cal_backend_cache_get_component(priv->cache, uid, NULL)))
  {
    status = GNOME_Evolution_Calendar_ObjectNotFound;
    goto out1;
  }

  status = e_cal_backend_3e_server_object_update(cb, cache_comp, updated_comp, old_object,
                                                 new_object, &local_err);

  if (status != GNOME_Evolution_Calendar_Success && local_err)
  {
    e_cal_sync_error_message(E_CAL_BACKEND(cb), cache_comp, local_err);
    g_error_free(local_err);
  }

  g_object_unref(cache_comp);

out1:
  g_object_unref(updated_comp);
out:
  g_mutex_unlock (priv->sync_mutex);

  return status;
}

static ECalBackendSyncStatus
e_cal_backend_3e_server_object_remove(ECalBackend3e* cb,
                                      EDataCal* cal,
                                      ECalComponent* cache_comp,
                                      const gchar* uid,
                                      const gchar* rid,
                                      char** old_object,
                                      GError** err)
{
  ECalBackend3ePrivate                             *priv;
  ECalBackendSyncStatus                             status = GNOME_Evolution_Calendar_Success;
  ECalComponentSyncState                            state;
  GError                                            *local_err = NULL;
  ECalComponentId                                   *id;
  gboolean                                          mark_as_changed = FALSE;

  g_return_val_if_fail(cb != NULL, GNOME_Evolution_Calendar_ObjectNotFound);
  g_return_val_if_fail(cal != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(cache_comp != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(cache_comp), GNOME_Evolution_Calendar_InvalidObject);
  g_return_val_if_fail(uid != NULL, GNOME_Evolution_Calendar_OtherError);
  /* rid can be NULL */
  g_return_val_if_fail(old_object != NULL, GNOME_Evolution_Calendar_OtherError);

  priv = cb->priv;
  state = e_cal_component_get_sync_state(cache_comp);
  *old_object = e_cal_component_get_as_string(cache_comp);

  switch (state)
  {
    case E_CAL_COMPONENT_IN_SYNCH:
    case E_CAL_COMPONENT_LOCALLY_MODIFIED: /* already on server, remove it from server */

      if (priv->mode == CAL_MODE_REMOTE)
      {
        if (!e_cal_sync_rpc_deleteObject(cb, cache_comp, FALSE, &local_err))
        {
          e_cal_sync_error_message(E_CAL_BACKEND(cb), cache_comp, local_err);
          if (local_err)
            g_propagate_error(err, local_err);
          mark_as_changed = TRUE;
        }
      }
      else
        /* in local mode only mark as changed */
        mark_as_changed = TRUE;

      if (mark_as_changed)
      {
        e_cal_component_set_sync_state(cache_comp, E_CAL_COMPONENT_LOCALLY_DELETED);
        if (state == E_CAL_COMPONENT_IN_SYNCH)
          e_cal_sync_client_changes_insert(cb, cache_comp);
      }

      if (!e_cal_backend_cache_put_component (priv->cache, cache_comp))
      {
        g_warning ("Error when removing component, cannot put new"
                   "component component into the cache!");
        status = GNOME_Evolution_Calendar_OtherError;
      }

      break;

    case E_CAL_COMPONENT_LOCALLY_CREATED: /* not on server yet... delete from cache */
      if (!e_cal_backend_cache_remove_component(priv->cache, uid, rid))
      {
        g_warning ("Cannot remove component from cache!");
        status = GNOME_Evolution_Calendar_OtherError;
      }
      else
        /* do not send it to server - remove object from list of changes */
        e_cal_sync_client_changes_remove(cb, cache_comp);
      break;

    case E_CAL_COMPONENT_LOCALLY_DELETED: /* nothing to do...  */
      g_warning ("Deleting component already marked as deleted");
      status = GNOME_Evolution_Calendar_OtherError;
      break;
  }

  if (status == GNOME_Evolution_Calendar_Success)
  {
    id = e_cal_component_get_id(cache_comp);
    e_cal_backend_notify_object_removed(E_CAL_BACKEND(cb), id, *old_object, NULL);
    e_cal_component_free_id(id);
  }
  else
    *old_object = NULL;

  return status;
}

/*
 *  removes an object from the calendar
 */
static ECalBackendSyncStatus
e_cal_backend_3e_remove_object (ECalBackendSync * backend,
                                EDataCal * cal,
                                const char *uid,
                                const char *rid,
                                CalObjModType mod,
                                char **old_object, char **object)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  ECalComponent                                    *cache_comp;
  ECalBackendSyncStatus                             status = GNOME_Evolution_Calendar_Success;
  ECalComponentSyncState                            state;
  GError                                            *local_err = NULL;

  T ("backend=%p, cal=%p, rid=%s, uid=%s", backend, cal, rid, uid);

  g_return_val_if_fail (backend != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  *old_object = *object = NULL;

  g_mutex_lock (priv->sync_mutex);

  if (!priv->has_write_permission)
  {
    status = GNOME_Evolution_Calendar_PermissionDenied;
    goto out;
  }

  cache_comp = e_cal_backend_cache_get_component (priv->cache, uid, rid);
  if (cache_comp == NULL)
  {
    status = GNOME_Evolution_Calendar_ObjectNotFound;
    goto out;
  }

  status = e_cal_backend_3e_server_object_remove(cb, cal, cache_comp, uid, rid, old_object,
                                                 &local_err);

  g_object_unref(cache_comp);

  if (local_err)
  {
    e_cal_sync_error_message(E_CAL_BACKEND(cb), cache_comp, local_err);
    g_error_free(local_err);
  }

out:
  g_mutex_unlock (priv->sync_mutex);

  return status;
}

static char *
create_user_free_busy (ECalBackend3e * cb, const char *address,
                       time_t start, time_t end)
{
  ECalBackend3ePrivate                             *priv;
  char                                             *retval;
  char                                              from_date[256];
  char                                              to_date[256];
  struct tm                                         tm;
  GError                                            *local_err = NULL;
  char                                              *message;

  g_return_val_if_fail(cb != NULL, NULL);
  g_return_val_if_fail(address != NULL, NULL);

  T("");

  priv = cb->priv;
  g_return_val_if_fail (priv->conn != NULL, NULL);

  gmtime_r (&start, &tm);
  if (!(strftime (from_date, sizeof (from_date), "%F %T", &tm)))
    return NULL;

  gmtime_r (&end, &tm);
  if (!(strftime (to_date, sizeof (to_date), "%F %T", &tm)))
    return NULL;

  if (!e_cal_sync_server_open (cb, &local_err))
    goto error;

  icalcomponent* comp = icaltimezone_get_component(priv->default_zone);
  char * default_zone = icalcomponent_as_ical_string(comp);
  retval = ESClient_freeBusy (priv->conn, g_strdup (address), from_date, to_date, default_zone,
                              &local_err);

  if (local_err)
    goto error;

  xr_client_close (priv->conn);

  return retval;

error:
  message = g_strdup_printf("Error: %s", local_err->message);
  g_warning(message);
  e_cal_backend_notify_error(E_CAL_BACKEND(cb), message);
  g_clear_error(&local_err);
  g_free(message);

  return NULL;
}

/*
 *  returns F/B information for a list of users
 */
static ECalBackendSyncStatus
e_cal_backend_3e_get_free_busy (ECalBackendSync * backend,
                                EDataCal * cal, GList * users,
                                time_t start, time_t end, GList ** freebusy)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  gchar                                            *address, *name = NULL;
  char                                             *calobj;
  GList                                            *l;
  GError                                           *local_err = NULL;
  gboolean                                         error = FALSE;

  T ("");

  g_return_val_if_fail(backend != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(cal != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(freebusy != NULL, GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  g_return_val_if_fail (start != -1 && end != -1, GNOME_Evolution_Calendar_InvalidRange);
  g_return_val_if_fail (start <= end, GNOME_Evolution_Calendar_InvalidRange);

  if (!priv->cache)
    return GNOME_Evolution_Calendar_NoSuchCal;

  if (priv->mode == CAL_MODE_LOCAL)
    return GNOME_Evolution_Calendar_RepositoryOffline;

  *freebusy = NULL;

  g_mutex_lock (priv->sync_mutex);

  if (users == NULL)
  {
    if (e_cal_backend_mail_account_get_default(&address, &name))
    {
      calobj = create_user_free_busy(cb, address, start, end);

      if (!calobj)
        error = TRUE;
      else
      {
        *freebusy = g_list_append(*freebusy, g_strdup (calobj));
        g_free(calobj);
      }
      g_free(address);
    }
  }
  else
  {
    for (l = users; !error && l != NULL; l = l->next)
    {
      address = l->data;
      calobj = create_user_free_busy(cb, address, start, end);

      if (!calobj)
        error = TRUE;
      else
      {
        g_debug("F/B: %s", calobj);

        *freebusy = g_list_append (*freebusy, g_strdup (calobj));
        g_free (calobj);
      }
    }
  }

  g_mutex_unlock (priv->sync_mutex);

  return error ? GNOME_Evolution_Calendar_OtherError : GNOME_Evolution_Calendar_Success;
}

/*
 *  returns a list of changes made since last check
 */
static ECalBackendSyncStatus
e_cal_backend_3e_get_changes (ECalBackendSync * backend,
                              EDataCal * cal,
                              const char *change_id,
                              GList ** adds,
                              GList ** modifies, GList ** deletes)
{
  /* FIXME: what to do here ? */
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;

  T ("");

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  g_return_val_if_fail (change_id != NULL,
                        GNOME_Evolution_Calendar_ObjectNotFound);

  return GNOME_Evolution_Calendar_Success;
}

/*
 *  discards an alarm (removes it or marks it as already displayed to the user)
 */
static ECalBackendSyncStatus
e_cal_backend_3e_discard_alarm (ECalBackendSync * backend,
                                EDataCal * cal,
                                const char *uid, const char *auid)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;

  T ("");

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  /* FIXME: what to do here ? */

  return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_3e_receive_object(ECalBackend3e *cb, EDataCal *cal, icalcomponent *icalcomp,
                                icalproperty_method toplevel_method, GError** err)
{
  ECalBackend3ePrivate                             *priv;
  ECalComponent                                    *comp;
  GError                                           *local_err = NULL;
	ECalBackendSyncStatus                            status = GNOME_Evolution_Calendar_Success;
  const gchar                                      *uid;
  const gchar                                      *rid;
  ECalComponent                                    *cache_comp;
  icalproperty_method                              method;
  struct icaltimetype                              current;
  gchar                                            *old_object, *new_object;

  T("");
  g_return_val_if_fail(cb != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(cal != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(icalcomp != NULL, GNOME_Evolution_Calendar_OtherError);

  priv = cb->priv;

  /* Create the cal component */
  comp = e_cal_component_new ();
  e_cal_component_set_icalcomponent(comp, icalcomp);

  /* Set the created and last modified times on the component */
  current = icaltime_from_timet (time (NULL), 0);
  e_cal_component_set_created (comp, &current);
  e_cal_component_set_last_modified (comp, &current);

  e_cal_component_get_uid (comp, &uid);
  rid = e_cal_component_get_recurid_as_string (comp);

  if (icalcomponent_get_first_property (icalcomp, ICAL_METHOD_PROPERTY))
    method = icalcomponent_get_method (icalcomp);
  else
    method = toplevel_method;

  cache_comp = e_cal_backend_cache_get_component(priv->cache, uid, rid);
  if (!cache_comp)
    goto out;

  /* update the cache */
  switch (method)
  {
    case ICAL_METHOD_PUBLISH:
    case ICAL_METHOD_REQUEST:
    case ICAL_METHOD_REPLY:
      /* handle attachments */
      status = cache_comp
        ? e_cal_backend_3e_server_object_update(cb, cache_comp, comp, &old_object, &new_object,
                                                &local_err)
        : e_cal_backend_3e_server_object_add(cb, comp, &new_object, &local_err);
      break;

    case ICAL_METHOD_CANCEL:
      if (cache_comp == NULL)
        status = GNOME_Evolution_Calendar_ObjectNotFound;
      else
        status = e_cal_backend_3e_server_object_remove(cb, cal, cache_comp, uid, rid, &old_object,
                                                       &local_err);
      break;

    default:
      /* hmmm */
      g_warning("Unsupported method!");
      status = GNOME_Evolution_Calendar_UnsupportedMethod;
      break;
  }

  g_object_unref(cache_comp);

  if (local_err)
  {
    g_propagate_error(err, local_err);
  }

out:
  g_object_unref(comp);
  return status;
}

typedef struct {
	GHashTable *zones;
	
	gboolean found;
} ECalBackend3eTzidData;

static void
check_tzids (icalparameter *param, void *data)
{
	ECalBackend3eTzidData *tzdata = data;
	const char *tzid;
	
	tzid = icalparameter_get_tzid (param);
	if (!tzid || g_hash_table_lookup (tzdata->zones, tzid))
		tzdata->found = FALSE;
}

/*
 *  import a set of events/tasks in one go
 */
static ECalBackendSyncStatus
e_cal_backend_3e_receive_objects (ECalBackendSync * backend,
                                  EDataCal * cal, const char *calobj)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
	icalcomponent                                    *icalcomp = NULL, *subcomp;
	icalcomponent_kind                               kind;
	ECalBackendSyncStatus                            status = GNOME_Evolution_Calendar_Success;
  icalproperty_method                              tmethod;
  icalcomponent                                    *toplevel_comp;
 	icalproperty_method                              toplevel_method, method;
  GList                                            *comps, *del_comps, *l;
  ECalBackend3eTzidData                            tzdata;
  ECalComponent                                    *comp;
  GError                                           *local_err = NULL;

  g_return_val_if_fail(backend != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(cal != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

  T ("");
  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  g_mutex_lock(priv->sync_mutex);

  /* mostly by file backend: */
  toplevel_comp = icalparser_parse_string((char *)calobj);
	kind = icalcomponent_isa(toplevel_comp);
	if (kind != ICAL_VCALENDAR_COMPONENT)
  {
    /* If its not a VCALENDAR, make it one to simplify below */
    icalcomp = toplevel_comp;
    toplevel_comp = e_cal_util_new_top_level ();
    if (icalcomponent_get_method (icalcomp) == ICAL_METHOD_CANCEL)
      icalcomponent_set_method (toplevel_comp, ICAL_METHOD_CANCEL);
    else
      icalcomponent_set_method (toplevel_comp, ICAL_METHOD_PUBLISH);
    icalcomponent_add_component (toplevel_comp, icalcomp);
  }
  else
  {
		if (!icalcomponent_get_first_property (toplevel_comp, ICAL_METHOD_PROPERTY))
			icalcomponent_set_method (toplevel_comp, ICAL_METHOD_PUBLISH);
	}

	toplevel_method = icalcomponent_get_method (toplevel_comp);

	/* Build a list of timezones so we can make sure all the objects have valid info */
	tzdata.zones = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	subcomp = icalcomponent_get_first_component (toplevel_comp, ICAL_VTIMEZONE_COMPONENT);
	while (subcomp)
  {
		icaltimezone *zone;
		
		zone = icaltimezone_new ();
		if (icaltimezone_set_component (zone, subcomp))
			g_hash_table_insert (tzdata.zones, g_strdup (icaltimezone_get_tzid (zone)), NULL);
		
		subcomp = icalcomponent_get_next_component (toplevel_comp, ICAL_VTIMEZONE_COMPONENT);
	}	

  /* First we make sure all the components are usuable */
	comps = del_comps = NULL;
	kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));

	subcomp = icalcomponent_get_first_component (toplevel_comp, ICAL_ANY_COMPONENT);
  while (subcomp)
  {
    icalcomponent_kind child_kind = icalcomponent_isa (subcomp);

    if (child_kind != kind)
    {
      /* remove the component from the toplevel VCALENDAR */
      if (child_kind != ICAL_VTIMEZONE_COMPONENT)
        del_comps = g_list_prepend (del_comps, subcomp);

      subcomp = icalcomponent_get_next_component (toplevel_comp, ICAL_ANY_COMPONENT);
      continue;
    }

    tzdata.found = TRUE;
    icalcomponent_foreach_tzid (subcomp, check_tzids, &tzdata);

    if (!tzdata.found)
    {
      status = GNOME_Evolution_Calendar_InvalidObject;
      goto error;
    }

    if (!icalcomponent_get_uid (subcomp))
    {
      if (toplevel_method == ICAL_METHOD_PUBLISH)
      {

        char *new_uid = NULL;

        new_uid = e_cal_component_gen_uid ();
        icalcomponent_set_uid (subcomp, new_uid);
        g_free (new_uid);
      }
      else
      {
        status = GNOME_Evolution_Calendar_InvalidObject;
        goto error;
      }

    }

    comps = g_list_prepend (comps, subcomp);
    subcomp = icalcomponent_get_next_component (toplevel_comp, ICAL_ANY_COMPONENT);
  }

  for (l = comps; l && status == GNOME_Evolution_Calendar_Success; l = l->next)
  {
    subcomp = l->data;
    status = e_cal_backend_3e_receive_object(cb, cal, subcomp, toplevel_method, &local_err);
  }

  if (local_err)
  {
    e_cal_backend_notify_error(E_CAL_BACKEND(backend), local_err->message);
    g_error_free(local_err);
    status = GNOME_Evolution_Calendar_OtherError;
  }

  g_list_free (comps);

  /* Now we remove the components we don't care about */
  for (l = del_comps; l; l = l->next) {
    subcomp = l->data;

    icalcomponent_remove_component (toplevel_comp, subcomp);
    icalcomponent_free (subcomp);		
  }

  g_list_free (del_comps);

error:
  g_mutex_unlock(priv->sync_mutex);
  return status;
}

void
e_cal_backend_3e_append_attendees(ECalBackend3e* cb, GSList** users, icalcomponent* icomp)
{
  ECalComponent                                     *comp;
  GSList                                            *attendee_list = NULL, *tmp;
  ECalComponentAttendee                             *attendee;
  ECalBackend3ePrivate                              *priv;

  g_return_if_fail(users != NULL);
  g_return_if_fail(icomp != NULL);
  g_return_if_fail(cb != NULL);

  priv = cb->priv;
  comp = e_cal_component_new();
  *users = NULL;

  if (e_cal_component_set_icalcomponent(comp, icalcomponent_new_clone(icomp)))
  {
    e_cal_component_get_attendee_list(comp, &attendee_list);
    /* convert this into GSList */
    for (tmp = attendee_list; tmp; tmp = g_slist_next(tmp))
    {
      attendee = (ECalComponentAttendee *)tmp->data;
      /*
       * priv->username is the organizer - mail sender, do not send him invitation */
      if (attendee && strcmp(priv->username, attendee->value + 7) != 0)
      {
        *users = g_slist_append(*users, g_strdup(attendee->value + 7));
      }
    }
  }

  g_object_unref (comp);	
}

/*
 * send a set of meetings in one go, which means, for backends that do support it,
 * sending information about the meeting to all attendees
 */
static ECalBackendSyncStatus
e_cal_backend_3e_send_objects (ECalBackendSync * backend,
                               EDataCal * cal,
                               const char *calobj,
                               GList ** users, char **modified_calobj)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;
  icalcomponent                                    *icalcomp, *subcomp;
	icalcomponent_kind                                kind;
  ECalBackendSyncStatus                             status = GNOME_Evolution_Calendar_Success;
  GSList                                            *attendees_slist, *iter;
  GError                                            *local_err = NULL;

  T ("calobj=%s", calobj);

  cb = E_CAL_BACKEND_3E (backend);
  priv = cb->priv;

  *users = NULL;
  *modified_calobj = NULL;

	if (!(icalcomp = icalparser_parse_string (calobj)))
  {
		status = GNOME_Evolution_Calendar_InvalidObject;
    goto out;
  }

	kind = icalcomponent_isa(icalcomp);
	if (kind == ICAL_VCALENDAR_COMPONENT)
  {
		subcomp = icalcomponent_get_first_component(icalcomp,
                                                e_cal_backend_get_kind(E_CAL_BACKEND (backend)));
		while (subcomp)
    {
      e_cal_backend_3e_append_attendees(cb, &attendees_slist, subcomp);

      subcomp = icalcomponent_get_next_component(icalcomp,
                                                 e_cal_backend_get_kind(E_CAL_BACKEND(backend)));
		}
	}
  else if (kind == e_cal_backend_get_kind(E_CAL_BACKEND (backend)))
    e_cal_backend_3e_append_attendees(cb, &attendees_slist, icalcomp);
  else
  {
    status = GNOME_Evolution_Calendar_InvalidObject;
    goto out;
  }
  
  if (!e_cal_sync_server_open(cb, &local_err))
  {
    g_warning("Cannot open connection to server: %s", local_err->message);
    status = GNOME_Evolution_Calendar_MODE_LOCAL;
    goto out;
  }

  if (!ESClient_sendMessage(priv->conn, priv->calspec, attendees_slist, g_strdup(calobj),
                            &local_err))
  {
    g_warning("Cannot send messages to the server: %s", local_err->message);
    goto out1;
  }

  for (iter = attendees_slist; iter; iter = g_slist_next(iter))
    *users = g_list_append(*users, iter->data);
  *modified_calobj = g_strdup(calobj);

out1:
  xr_client_close(priv->conn);
out:
	icalcomponent_free (icalcomp);

	return status;
}

/*
 *  returns the default timezone.
 */
static icaltimezone *
e_cal_backend_3e_internal_get_default_timezone (ECalBackend * backend)
{
  return icaltimezone_get_utc_timezone ();
}

/*
 *  returns a given timezone
 */
static icaltimezone *
e_cal_backend_3e_internal_get_timezone (ECalBackend * backend,
                                        const char *tzid)
{
  return strcmp (tzid, "UTC") ? icaltimezone_get_builtin_timezone_from_tzid (tzid) :
    icaltimezone_get_utc_timezone ();
}

/* GObject foo */

static ECalBackendSyncClass *parent_class;


static void
e_cal_backend_3e_init (ECalBackend3e * cb, ECalBackend3eClass * klass)
{
  T ("cb=%p, klass=%p", cb, klass);

  if (!g_thread_supported ())
    g_thread_init (NULL);

  cb->priv = g_new0 (ECalBackend3ePrivate, 1);
  cb->priv->sync_terminated = FALSE;
  cb->priv->sync_mode = SYNC_SLEEP;
  cb->priv->sync_cond = g_cond_new ();
  cb->priv->sync_mutex = g_mutex_new ();
  cb->priv->sync_thread = NULL;
  cb->priv->gconf = gconf_client_get_default ();
  cb->priv->source_changed_perm = 0;

  e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC (cb), TRUE);
}

static void
e_cal_backend_3e_dispose (GObject * object)
{
  T ("object=%p", object);

  if (G_OBJECT_CLASS (parent_class)->dispose)
    G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_cal_backend_3e_finalize (GObject * object)
{
  ECalBackend3e                                    *cb;
  ECalBackend3ePrivate                             *priv;

  T ("object=%p", object);

  g_return_if_fail (object != NULL);
  g_return_if_fail (E_IS_CAL_BACKEND_3E (object));

  cb = E_CAL_BACKEND_3E (object);
  priv = cb->priv;

  server_sync_signal (cb);
  if (priv->sync_thread)
    g_thread_join (priv->sync_thread);
  g_cond_free (priv->sync_cond);
  g_mutex_free (priv->sync_mutex);

  priv->conn = NULL;

  g_object_unref (priv->cache);
  priv->cache = NULL;
  g_free (priv->server_uri);
  priv->server_uri = NULL;
  g_free (priv->calname);
  priv->calname = NULL;
  g_free (priv->owner);
  priv->owner = NULL;
  g_free (priv->username);
  priv->username = NULL;
  g_free (priv->calspec);
  priv->calspec = NULL;
  g_free (priv->password);
  priv->password = NULL;
  g_free (priv->sync_stamp);
  priv->sync_stamp = NULL;

  if (priv->default_zone)
  {
    icaltimezone_free (priv->default_zone, 1);
    priv->default_zone = NULL;
  }

  if (priv->settings)
    g_object_unref(priv->settings);
  g_object_unref (priv->gconf);
  priv->gconf = NULL;

  g_list_foreach (priv->sync_clients_changes, (GFunc) g_object_unref, NULL);
  g_list_free (priv->sync_clients_changes);

  g_free (priv);
  cb->priv = NULL;

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
e_cal_backend_3e_class_init (ECalBackend3eClass * class)
{
  GObjectClass                                     *object_class;
  ECalBackendClass                                 *backend_class;
  ECalBackendSyncClass                             *sync_class;

  object_class = (GObjectClass *) class;
  backend_class = (ECalBackendClass *) class;
  sync_class = (ECalBackendSyncClass *) class;

  parent_class = (ECalBackendSyncClass *) g_type_class_peek_parent (class);

  object_class->dispose = e_cal_backend_3e_dispose;
  object_class->finalize = e_cal_backend_3e_finalize;

  sync_class->is_read_only_sync = e_cal_backend_3e_is_read_only;
  sync_class->get_cal_address_sync = e_cal_backend_3e_get_cal_address;
  sync_class->get_alarm_email_address_sync =
    e_cal_backend_3e_get_alarm_email_address;
  sync_class->get_ldap_attribute_sync = e_cal_backend_3e_get_ldap_attribute;
  sync_class->get_static_capabilities_sync =
    e_cal_backend_3e_get_static_capabilities;
  sync_class->open_sync = e_cal_backend_3e_open;
  sync_class->remove_sync = e_cal_backend_3e_remove;
  sync_class->create_object_sync = e_cal_backend_3e_create_object;
  sync_class->modify_object_sync = e_cal_backend_3e_modify_object;
  sync_class->remove_object_sync = e_cal_backend_3e_remove_object;
  sync_class->discard_alarm_sync = e_cal_backend_3e_discard_alarm;
  sync_class->receive_objects_sync = e_cal_backend_3e_receive_objects;
  sync_class->send_objects_sync = e_cal_backend_3e_send_objects;
  sync_class->get_default_object_sync = e_cal_backend_3e_get_default_object;
  sync_class->get_object_sync = e_cal_backend_3e_get_object;
  sync_class->get_object_list_sync = e_cal_backend_3e_get_object_list;
  sync_class->get_timezone_sync = e_cal_backend_3e_get_timezone;
  sync_class->add_timezone_sync = e_cal_backend_3e_add_timezone;
  sync_class->set_default_zone_sync = e_cal_backend_3e_set_default_zone;
  sync_class->get_freebusy_sync = e_cal_backend_3e_get_free_busy;
  sync_class->get_changes_sync = e_cal_backend_3e_get_changes;

  backend_class->is_loaded = e_cal_backend_3e_is_loaded;
  backend_class->start_query = e_cal_backend_3e_start_query;
  backend_class->get_mode = e_cal_backend_3e_get_mode;
  backend_class->set_mode = e_cal_backend_3e_set_mode;

  backend_class->internal_get_default_timezone = e_cal_backend_3e_internal_get_default_timezone;
  backend_class->internal_get_timezone = e_cal_backend_3e_internal_get_timezone;
}

GType
e_cal_backend_3e_get_type (void)
{
  static GType                                      e_cal_backend_3e_type = 0;

  if (!e_cal_backend_3e_type)
  {
    static GTypeInfo info = {
      sizeof (ECalBackend3eClass),
      (GBaseInitFunc) NULL,
      (GBaseFinalizeFunc) NULL,
      (GClassInitFunc) e_cal_backend_3e_class_init,
      NULL, NULL,
      sizeof (ECalBackend3e),
      0,
      (GInstanceInitFunc) e_cal_backend_3e_init,
      NULL
    };
    e_cal_backend_3e_type =
      g_type_register_static (E_TYPE_CAL_BACKEND_SYNC, "ECalBackend3e",
                              &info, 0);
  }

  return e_cal_backend_3e_type;
}

  /*
   * get_static_capabilities_sync:
   *capabilities = g_strdup(
   CAL_STATIC_CAPABILITY_NO_ALARM_REPEAT "," // Disable automatic repeating of alarms
   CAL_STATIC_CAPABILITY_NO_AUDIO_ALARMS ","  // Disable particular alarm type
   CAL_STATIC_CAPABILITY_NO_DISPLAY_ALARMS ","  // Disable particular alarm type
   CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS ","  // Disable particular alarm type
   CAL_STATIC_CAPABILITY_NO_PROCEDURE_ALARMS ","  // Disable particular alarm type
   CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT ","
   CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
   CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
   CAL_STATIC_CAPABILITY_NO_TRANSPARENCY ","
   CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY "," // Checks if a calendar supports only one alarm per component.
   CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ATTEND "," // Checks if a calendar forces organizers of meetings to be also attendees.
   CAL_STATIC_CAPABILITY_ORGANIZER_NOT_EMAIL_ADDRESS ","
   CAL_STATIC_CAPABILITY_REMOVE_ALARMS ","
   CAL_STATIC_CAPABILITY_SAVE_SCHEDULES "," // Checks whether the calendar saves schedules.
   CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK ","
   CAL_STATIC_CAPABILITY_NO_CONV_TO_RECUR ","
   CAL_STATIC_CAPABILITY_NO_GEN_OPTIONS ","
   CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER "," // Checks if the calendar has a master object for recurrences.
   CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ACCEPT "," // Checks whether a calendar requires organizer to accept their attendance to meetings.
   CAL_STATIC_CAPABILITY_DELEGATE_SUPPORTED ","
   CAL_STATIC_CAPABILITY_NO_ORGANIZER ","
   CAL_STATIC_CAPABILITY_DELEGATE_TO_MANY ","
   CAL_STATIC_CAPABILITY_HAS_UNACCEPTED_MEETING ","
   CAL_STATIC_CAPABILITY_REQ_SEND_OPTIONS
   );
   */
