#include <config.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include <gconf/gconf-client.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-moniker-util.h>
#include <libedataserver/e-xml-hash-utils.h>
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

extern char       *e_passwords_get_password(const char *component, const char *key);

#include "interface/ESClient.xrc.h"

/** Backend design:
 *
 * - xr_client_conn is not kept only for a short time
 * - synchronization of client cache to server is initiated by the client
 *   - when switching from offline to online mode
 *   - on any modification
 * - synchronization of server to client is initiated
 *   - by the client when switching from offline mode
 *   - periodically in online mode to get changes from the server
 * - synchronization always runs in separate thread, it is possible to perform
 *   it asynchronously or to wait for it to complete
 *
 * - calendar creation:
 *   - calendar is created/subscribed by the GUI part of the code
 *   - backend only logs in to user account and checks if calendar exists
 *   - each calendar and calendar subscription has own backend
 * - calendar removal:
 *   - e_cal_backend_3e_remove is called, it checks whether subscription is
 *   being removed or owned calendar and performs required steps
 *   - gui synchronizes calendar list with the server
 *
 * - object manipulation:
 *   - all operations are performed on local cache and appropriate flags
 *     are set (modified/added/removed)
 *   - client -> server synchronization is then initiated
 *
 */

/* Backend plugin design
 * ~~~~~~~~~~~~~~~~~~~~~
 *
 * Goals:
 * - synchronize between calendar contents on the server and in the cache on
 *   the client.
 * - perform periodical checks for changes in the calendar on the server
 * - handle switches between online and offline mode
 * - synchronize changes on the client when in offline mode to the server on
 *   transition to online mode
 *
 */


/* helper functions */



// returns the capabilities provided by the backend, like whether it supports recurrences or not, for instance
static ECalBackendSyncStatus
e_cal_backend_3e_get_static_capabilities(ECalBackendSync * backend,
                                         EDataCal * cal,
                                         char **capabilities)
{
  T("backend=%p, cal=%p, capabilities=%p", backend, cal, capabilities);

  g_return_val_if_fail(capabilities != NULL,
                       GNOME_Evolution_Calendar_OtherError);

//  *capabilities = g_strdup(
//  CAL_STATIC_CAPABILITY_NO_ALARM_REPEAT "," // Disable automatic repeating of alarms
//  CAL_STATIC_CAPABILITY_NO_AUDIO_ALARMS ","  // Disable particular alarm type
//  CAL_STATIC_CAPABILITY_NO_DISPLAY_ALARMS ","  // Disable particular alarm type
//  CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS ","  // Disable particular alarm type
//  CAL_STATIC_CAPABILITY_NO_PROCEDURE_ALARMS ","  // Disable particular alarm type
//  CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT ","
//  CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
//  CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
//  CAL_STATIC_CAPABILITY_NO_TRANSPARENCY ","
//  CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY "," // Checks if a calendar supports only one alarm per component.
//  CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ATTEND "," // Checks if a calendar forces organizers of meetings to be also attendees.
//  CAL_STATIC_CAPABILITY_ORGANIZER_NOT_EMAIL_ADDRESS ","
//  CAL_STATIC_CAPABILITY_REMOVE_ALARMS ","
//  CAL_STATIC_CAPABILITY_SAVE_SCHEDULES "," // Checks whether the calendar saves schedules.
//  CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK ","
//  CAL_STATIC_CAPABILITY_NO_CONV_TO_RECUR ","
//  CAL_STATIC_CAPABILITY_NO_GEN_OPTIONS ","
//  CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER "," // Checks if the calendar has a master object for recurrences.
//  CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ACCEPT "," // Checks whether a calendar requires organizer to accept their attendance to meetings.
//  CAL_STATIC_CAPABILITY_DELEGATE_SUPPORTED ","
//  CAL_STATIC_CAPABILITY_NO_ORGANIZER ","
//  CAL_STATIC_CAPABILITY_DELEGATE_TO_MANY ","
//  CAL_STATIC_CAPABILITY_HAS_UNACCEPTED_MEETING ","
//  CAL_STATIC_CAPABILITY_REQ_SEND_OPTIONS
//  );
  *capabilities = g_strdup("");

  return GNOME_Evolution_Calendar_Success;
}

// Returns TRUE if the the passed-in backend is already in a loaded state, otherwise FALSE
static gboolean
e_cal_backend_3e_is_loaded(ECalBackend *backend)
{
  ECalBackend3e *cb;
  ECalBackend3ePrivate *priv;

  T("backend=%p", backend);
  g_return_val_if_fail(backend != NULL,  GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  return priv->is_loaded;
}

// sets the current online/offline mode.
static void
e_cal_backend_3e_set_mode(ECalBackend * backend,
                          CalMode mode)
{
  ECalBackend3e                       *cb;
  ECalBackend3ePrivate                *priv;
  GNOME_Evolution_Calendar_CalMode    set_mode; 
  GNOME_Evolution_Calendar_CalListener_SetModeStatus status;

  T("backend=%p, mode=%d", backend, mode);
  g_return_if_fail(backend != NULL);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  if (priv->mode == mode)
  {
    D("Not changing mode");
    e_cal_backend_notify_mode(backend, GNOME_Evolution_Calendar_CalListener_MODE_SET,
                              cal_mode_to_corba(mode));
    return;
  }

  g_mutex_lock(priv->sync_mutex);

  if (mode == CAL_MODE_REMOTE)
  {
    // going to remote mode
    D("going ONLINE, waking sync thread");
    priv->mode = CAL_MODE_REMOTE;
    priv->sync_mode = SYNC_WORK;
    g_cond_signal(priv->sync_cond);
    set_mode = cal_mode_to_corba(GNOME_Evolution_Calendar_MODE_REMOTE);
    status = GNOME_Evolution_Calendar_CalListener_MODE_SET;
  }
  else if (mode == CAL_MODE_LOCAL)
  {
    // going to local mode
    D("going OFFLINE");
    priv->sync_mode = SYNC_SLEEP;
    priv->mode = CAL_MODE_LOCAL;
    set_mode = cal_mode_to_corba(GNOME_Evolution_Calendar_MODE_LOCAL);
    status = GNOME_Evolution_Calendar_CalListener_MODE_SET;
  }
  else
  {
    D("NOT SUPPORTED");
    status = GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED;
    set_mode = cal_mode_to_corba (mode);
  }

  e_cal_backend_notify_mode(backend, status, set_mode);

  g_mutex_unlock(priv->sync_mutex);
}

static ECalBackendSyncStatus
initialize_backend(ECalBackend3e* cb)
{ 
  GError*                       err = NULL;
  ECalBackend3ePrivate          *priv;
  ESource                       *source;
  const char                    *server_hostname;
  const char                    *cal_name;

  D("INITALIZING BACKEND");

  g_return_val_if_fail(cb != NULL, GNOME_Evolution_Calendar_OtherError);

  priv = cb->priv;
  priv->cache = e_cal_backend_cache_new(e_cal_backend_get_uri(E_CAL_BACKEND(cb)),
                                        E_CAL_SOURCE_TYPE_EVENT);
  if (!priv->cache)
  {
    e_cal_backend_notify_error(E_CAL_BACKEND(cb), "Could not create cache file");
    return GNOME_Evolution_Calendar_OtherError;
  }

  if (priv->default_zone)
    e_cal_backend_cache_put_default_timezone(priv->cache, priv->default_zone);

  source = e_cal_backend_get_source(E_CAL_BACKEND(cb));
  server_hostname = e_source_get_property(source, "eee-server");
  cal_name = e_source_get_property(source, "eee-calendar-name");

  g_free(priv->server_uri);
  g_free(priv->calname);
  priv->server_uri = g_strdup_printf("https://%s/ESClient", server_hostname);
  priv->calname = g_strdup(cal_name);
  priv->settings = e_cal_sync_find_settings(cb);

  if (server_hostname == NULL || cal_name == NULL)
  {
    e_cal_backend_notify_error(E_CAL_BACKEND(cb),
                               "Invalid calendar source list setup.");
    return GNOME_Evolution_Calendar_OtherError;

  }

  D("3es uri=%s\n", priv->server_uri);
  D("3es calname=%s\n", priv->calname);

  if (priv->conn == NULL)
  {
    priv->conn = xr_client_new(&err);
    if (err != NULL)
    {
      e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb),
                                        "Failed to initialize XML-RPC client library.", err);
      return GNOME_Evolution_Calendar_OtherError;
    }
  }

  priv->is_loaded = TRUE;

  return GNOME_Evolution_Calendar_Success;
}

// opens the calendar
static ECalBackendSyncStatus
e_cal_backend_3e_open(ECalBackendSync* backend,
                      EDataCal* cal,
                      gboolean only_if_exists,
                      const char *username,
                      const char *password)
{
  ECalBackend3e                 *cb;
  ECalBackend3ePrivate          *priv;
  ECalBackendSyncStatus         status;
  ESourceList                   *eslist;
  GSList                        *groups_list;
  GSList                        *iter;
  GSList                        *p;
  const char                    *group_name;
  const char                    *cal_name;
  ESourceGroup                  *group;
  ESource                       *source;

  T("backend=%p, cal=%p, only_if_exists=%d, username=%s, password=%s",
    backend, cal, only_if_exists, username, password);

  g_return_val_if_fail(backend != NULL,  GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  g_mutex_lock (priv->sync_mutex);

  status = (priv->is_loaded == TRUE)
    ? GNOME_Evolution_Calendar_Success
    : initialize_backend (cb);

  g_return_val_if_fail(priv->calname != 0, GNOME_Evolution_Calendar_OtherError);

  g_free(priv->username);
  g_free(priv->password);

  if (!username || *username == 0)
  {
    source = e_cal_backend_get_source(E_CAL_BACKEND(cb));
    priv->username = g_strdup(e_source_get_property(source, "username"));

    if (!priv->username)
    {
      ESourceList* eslist;
      GSList* iter;

      D("WHAT USERNAME for CALENDAR %s", priv->calname);

      eslist = e_source_list_new_for_gconf(priv->gconf, CALENDAR_SOURCES);
      GSList* groups_list = g_slist_copy(e_source_list_peek_groups(eslist));
      for (iter = groups_list; iter; iter = iter->next)
      {
        ESourceGroup* group = E_SOURCE_GROUP(iter->data);
        const char* group_name = e_source_group_peek_name(group);

        // skip non eee groups
        if (strcmp(e_source_group_peek_base_uri(group), EEE_URI_PREFIX))
          continue;

        if (group_name && g_str_has_prefix(group_name, "3E: "))
        {
          GSList *p;
          for (p = e_source_group_peek_sources(group); p != NULL; p = p->next)
          {
            const char* cal_name = e_source_get_property(E_SOURCE(p->data), "eee-calendar-name");
            if (priv->calname && cal_name && !strcmp(cal_name, priv->calname))
              priv->username = g_strdup(e_source_get_property(E_SOURCE(p->data), "username"));
          }
        }

        g_slist_free(groups_list);
      }
    }
    else
      priv->password = e_passwords_get_password(EEE_PASSWORD_COMPONENT, priv->username);
  }
  else
  {
    priv->username = g_strdup(username);
    priv->password = g_strdup(password);
  }

  g_return_val_if_fail(priv->username && *priv->username != 0, GNOME_Evolution_Calendar_OtherError);

  D("USERNAME: %s", priv->username);

  if (status != GNOME_Evolution_Calendar_Success)
    goto out;

  g_free(priv->calspec);
  g_free(priv->sync_stamp);
  priv->calspec = g_strdup_printf("%s:%s", priv->username, priv->calname);
  priv->is_open = TRUE;
  e_cal_sync_load_stamp(cb, &priv->sync_stamp);
  rebuild_clients_changes_list(cb);

  if (priv->mode == CAL_MODE_REMOTE)
  {
    priv->sync_mode = SYNC_WORK;
    D("waking sync thread");
    g_cond_signal(priv->sync_cond);
  }
  else
    priv->sync_mode = SYNC_SLEEP;

out:
  g_mutex_unlock (priv->sync_mutex);

  return status;
}

// returns whether the calendar is read only or not
static ECalBackendSyncStatus
e_cal_backend_3e_is_read_only(ECalBackendSync* backend,
                              EDataCal * cal,
                              gboolean * read_only)
{
  ECalBackend3e                 *cb;
  ECalBackend3ePrivate          *priv;

  g_return_val_if_fail(backend != NULL,  GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(read_only != NULL,  GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;
  *read_only = !priv->is_open;

  T("backend=%p, cal=%p, read_only=%s", backend, cal, *read_only ? "true" : "false");

  return GNOME_Evolution_Calendar_Success;
}

// returns the current online/offline mode for the backend
static CalMode
e_cal_backend_3e_get_mode(ECalBackend * backend)
{
  ECalBackend3e                 *cb;
  ECalBackend3ePrivate          *priv;

  T("backend=%p", backend);
  g_return_val_if_fail(backend != NULL,
                       GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  return priv->mode;
}

// sets the timezone to be used as the default
static ECalBackendSyncStatus
e_cal_backend_3e_set_default_zone(ECalBackendSync *backend,
                                  EDataCal *cal,
                                  const char *tzobj)
{
  icalcomponent                   *tz_comp;
  ECalBackend3e                   *cb;
  ECalBackend3ePrivate            *priv;
  icaltimezone                    *zone;
  ECalBackendSyncStatus           status = GNOME_Evolution_Calendar_OtherError;

  // FIXME: do we need mutexes here? propably not...
  T("backend=%p, cal=%p, tzobj=%s", backend, cal, tzobj);

  g_return_val_if_fail(backend != NULL,
                       GNOME_Evolution_Calendar_OtherError);

  cb = (ECalBackend3e *)backend;

  g_return_val_if_fail(E_IS_CAL_BACKEND_3E(cb), GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

  priv = cb->priv;
  g_mutex_lock(priv->sync_mutex);

  if (!(tz_comp = icalparser_parse_string(tzobj)))
  {
    status = GNOME_Evolution_Calendar_InvalidObject;
    goto out;
  }

  zone = icaltimezone_new();
  icaltimezone_set_component(zone, tz_comp);

  if (priv->default_zone)
    icaltimezone_free(priv->default_zone, 1);

  /*
   * Set the default timezone to it. 
   */
  priv->default_zone = zone;
  status = GNOME_Evolution_Calendar_Success;

out:
  g_mutex_unlock(priv->sync_mutex);

  return status;
}

// removes the calendar
static ECalBackendSyncStatus
e_cal_backend_3e_remove(ECalBackendSync* backend,
                        EDataCal * cal)
{
  ECalBackend3e                   *cb;
  ECalBackend3ePrivate            *priv;
  gchar                           *path;
  GError                          *err;

  g_return_val_if_fail(backend != NULL,
                       GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  g_mutex_lock (priv->sync_mutex);

  if (priv->is_loaded != TRUE)
  {
    g_mutex_unlock (priv->sync_mutex);
    return GNOME_Evolution_Calendar_Success;
  }

  e_file_cache_remove(E_FILE_CACHE(priv->cache));
  priv->cache  = NULL;
  priv->is_loaded = FALSE;	
  priv->sync_mode = SYNC_DIE;

  /*
   * FIXME: remove synchronization marks
  D("unsetting gconf value");
  path = g_strdup_printf("%s/%s:%s",
                         CALENDAR_STAMPS,
                         priv->username, priv->calname);

  g_free(path);
  */

  g_mutex_unlock (priv->sync_mutex);

  return GNOME_Evolution_Calendar_Success;
}

// returns the email address of the owner of the calendar
static ECalBackendSyncStatus
e_cal_backend_3e_get_cal_address(ECalBackendSync* backend,
                                 EDataCal* cal,
                                 char** address)
{
  ECalBackend3e                   *cb;
  ECalBackend3ePrivate            *priv;

  T("backend=%p, cal=%p, address=%s", backend, cal, *address);

  g_return_val_if_fail(backend != NULL,
                       GNOME_Evolution_Calendar_OtherError);

  g_return_val_if_fail(address != NULL,
                       GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;
  *address = g_strdup(priv->username);

  return GNOME_Evolution_Calendar_Success;
}

// returns the email address to be used for alarms
static ECalBackendSyncStatus
e_cal_backend_3e_get_alarm_email_address(ECalBackendSync *backend,
                                         EDataCal *cal,
                                         char **address)
{
  ECalBackend3e               *cb;
  ECalBackend3ePrivate        *priv;

  T("backend=%p, cal=%p, address=%s", backend, cal, *address);

  g_return_val_if_fail(backend != NULL,
                       GNOME_Evolution_Calendar_OtherError);

  g_return_val_if_fail(address != NULL,
                       GNOME_Evolution_Calendar_OtherError);
  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  *address = g_strdup(priv->username);

  return GNOME_Evolution_Calendar_Success;
}

// returns an empty object with the default values used for the backend
// called when creating new object, for example
static ECalBackendSyncStatus
e_cal_backend_3e_get_default_object(ECalBackendSync *backend,
                                    EDataCal *cal,
                                    char **object)
{
  ECalBackend3e             *cb;
  ECalBackend3ePrivate      *priv;
  icalcomponent             *icalcomp;
  icalcomponent_kind        kind;

  T("backend=%p, cal=%p, object=%s", backend, cal, *object);

  g_return_val_if_fail(backend != NULL,
                       GNOME_Evolution_Calendar_OtherError);

  g_return_val_if_fail(object != NULL,
                       GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  kind = e_cal_backend_get_kind(E_CAL_BACKEND(backend));
  icalcomp = e_cal_util_new_component(kind);
  *object = g_strdup(icalcomponent_as_ical_string(icalcomp));
  icalcomponent_free(icalcomp);

  return GNOME_Evolution_Calendar_Success;
}

// returns a list of events/tasks given a set of conditions
static ECalBackendSyncStatus e_cal_backend_3e_get_object(ECalBackendSync * backend,
                                                         EDataCal * cal,
                                                         const char *uid,
                                                         const char *rid,
                                                         char **object)
{
  ECalBackend3e               *cb;
  ECalBackend3ePrivate        *priv;
  ECalComponent               *comp = NULL;

  T("backend=%p, cal=%p, uid=%s, rid=%s", backend, cal, uid, rid);

  g_return_val_if_fail(backend != NULL,
                       GNOME_Evolution_Calendar_OtherError);

  g_return_val_if_fail(object != NULL,
                       GNOME_Evolution_Calendar_OtherError);

  g_return_val_if_fail(uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  if (!priv->cache)
    return GNOME_Evolution_Calendar_ObjectNotFound;

  g_mutex_lock(priv->sync_mutex);
  comp = e_cal_backend_cache_get_component(priv->cache, uid, rid);
  g_mutex_unlock(priv->sync_mutex);

  if (!comp)
    return GNOME_Evolution_Calendar_ObjectNotFound;

  *object = e_cal_component_get_as_string(comp);
  g_object_unref(comp);

  return GNOME_Evolution_Calendar_Success;
}

// returns timezone objects for a given TZID
static ECalBackendSyncStatus
e_cal_backend_3e_get_timezone(ECalBackendSync * backend,
                              EDataCal * cal,
                              const char *tzid,
                              char **object)
{
  ECalBackend3e               *cb;
  ECalBackend3ePrivate        *priv;
  const icaltimezone          *zone;
  icalcomponent               *icalcomp;

  T("backend=%p, cal=%p, tzid=%s", backend, cal, tzid);

  g_return_val_if_fail(backend != NULL,
                       GNOME_Evolution_Calendar_OtherError);

  g_return_val_if_fail(tzid != NULL,
                       GNOME_Evolution_Calendar_ObjectNotFound);

  g_return_val_if_fail(object != NULL,
                       GNOME_Evolution_Calendar_ObjectNotFound);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;


  /* first try to get the timezone from the cache */
  g_mutex_lock(priv->sync_mutex);
  zone = e_cal_backend_cache_get_timezone(priv->cache, tzid);
  g_mutex_unlock(priv->sync_mutex);

  if (!zone)
  {
    zone = icaltimezone_get_builtin_timezone_from_tzid(tzid);
    if (!zone)
      return GNOME_Evolution_Calendar_ObjectNotFound;
  }

  icalcomp = icaltimezone_get_component((icaltimezone *) zone);

  if (!icalcomp)
    return GNOME_Evolution_Calendar_InvalidObject;

  *object = g_strdup(icalcomponent_as_ical_string(icalcomp));

  return GNOME_Evolution_Calendar_Success;
}

// returns specific LDAP attributes
static ECalBackendSyncStatus
e_cal_backend_3e_get_ldap_attribute(ECalBackendSync * backend,
                                    EDataCal * cal,
                                    char **attribute)
{
  T("backend=%p, cal=%p", backend, cal);

  *attribute = NULL;

  return GNOME_Evolution_Calendar_OtherError;
}

// adds a timezone to the backend
static ECalBackendSyncStatus e_cal_backend_3e_add_timezone(ECalBackendSync * backend,
                                                           EDataCal * cal,
                                                           const char *tzobj)
{
  ECalBackend3e                 *cb;
  ECalBackend3ePrivate          *priv;
  icalcomponent                 *tz_comp;
  icaltimezone                  *zone;

  T("backend=%p, cal=%p, tzobj=%s", backend, cal, tzobj);

  cb = (ECalBackend3e *)backend;

  g_return_val_if_fail(E_IS_CAL_BACKEND_3E(cb), GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

  priv = cb->priv;

  tz_comp = icalparser_parse_string(tzobj);
  if (!tz_comp)
    return GNOME_Evolution_Calendar_InvalidObject;

  if (icalcomponent_isa(tz_comp) != ICAL_VTIMEZONE_COMPONENT)
  {
    icalcomponent_free(tz_comp);
    return GNOME_Evolution_Calendar_InvalidObject;
  }

  zone = icaltimezone_new();
  icaltimezone_set_component(zone, tz_comp);

  g_mutex_lock(priv->sync_mutex);
  e_cal_backend_cache_put_timezone(priv->cache, zone);
  g_mutex_unlock(priv->sync_mutex);

  return GNOME_Evolution_Calendar_Success;
}

// returns a list of events/tasks given a set of conditions
static ECalBackendSyncStatus e_cal_backend_3e_get_object_list(ECalBackendSync *backend,
                                                              EDataCal *cal,
                                                              const char *sexp,
                                                              GList **objects)
{
  ECalBackend3e                   *cb;
  ECalBackend3ePrivate            *priv;
  GList                           *components, *l;
  ECalBackendSExp                 *cbsexp;

  T("backend=%p, cal=%p, sexp=%s", backend, cal, sexp);

  g_return_val_if_fail(backend != NULL,
                       GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  if (!priv->cache)
    return GNOME_Evolution_Calendar_NoSuchCal;

  g_mutex_lock(priv->sync_mutex);

  /* process all components in the cache */
  cbsexp = e_cal_backend_sexp_new(sexp);

  *objects = NULL;
  components = e_cal_backend_cache_get_components(priv->cache);

  for (l = components; l != NULL; l = l->next)
  {
    // FIXME: what about locally deleted objects ?
    
    if (e_cal_backend_sexp_match_comp(cbsexp, E_CAL_COMPONENT(l->data), E_CAL_BACKEND(backend)))
      *objects = g_list_append(*objects, e_cal_component_get_as_string(l->data));
  }

  g_list_foreach(components, (GFunc) g_object_unref, NULL);
  g_list_free(components);
  g_object_unref(cbsexp);

  g_mutex_unlock(priv->sync_mutex);

  return GNOME_Evolution_Calendar_Success;
}

// starts a live query on the backend
static void
e_cal_backend_3e_start_query(ECalBackend * backend,
                             EDataCalView * query)
{
  ECalBackend3e               *cb;
  ECalBackend3ePrivate        *priv;
  GList                       *components, *l, *objects = NULL;
  ECalBackendSExp             *cbsexp;
  ECalComponent               *comp;
  ECalComponentSyncState      state;

  // T("backend=%p", backend);

  g_return_if_fail(backend != NULL);
  g_return_if_fail(query != NULL);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  if (!priv->cache)
  {
    e_data_cal_view_notify_done(query, GNOME_Evolution_Calendar_NoSuchCal);
    return;
  }

  /* process all components in the cache */
  const char* text = e_data_cal_view_get_text(query);
  cbsexp = e_cal_backend_sexp_new(text);
  objects = NULL;

  g_mutex_lock(priv->sync_mutex);

  components = e_cal_backend_cache_get_components(priv->cache);

  for (l = components; l != NULL; l = l->next)
  {
    comp = E_CAL_COMPONENT(l->data);
    state = e_cal_component_get_sync_state(comp);

    if (e_cal_backend_sexp_match_comp(cbsexp, comp, E_CAL_BACKEND(backend)) 
                                      && state != E_CAL_COMPONENT_LOCALLY_DELETED)
      objects = g_list_append(objects, e_cal_component_get_as_string(l->data));

  }

  e_data_cal_view_notify_objects_added(query, (const GList *) objects);

  g_list_foreach(components, (GFunc) g_object_unref, NULL);
  g_list_free(components);
  g_list_foreach(objects, (GFunc) g_free, NULL);
  g_list_free(objects);
  g_object_unref(cbsexp);

  e_data_cal_view_notify_done(query, GNOME_Evolution_Calendar_Success);
  g_mutex_unlock(priv->sync_mutex);
}

// creates a new event/task in the calendar
static ECalBackendSyncStatus
e_cal_backend_3e_create_object(ECalBackendSync * backend,
                               EDataCal * cal,
                               char **calobj,
                               char **uid)
{
  ECalBackend3e *cb;
  ECalBackend3ePrivate *priv;
  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;
  ECalComponent* comp;
  ECalBackendSyncStatus status = GNOME_Evolution_Calendar_Success;

  T("backend=%p, cal=%p, calobj=%s, uid=%s", backend, cal, *calobj, *uid);

  g_return_val_if_fail(backend != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(calobj != NULL, GNOME_Evolution_Calendar_OtherError);

  g_mutex_lock(priv->sync_mutex);

  if (!(comp = e_cal_component_new_from_string(*calobj)))
  {
    status = GNOME_Evolution_Calendar_InvalidObject;
    goto out;
  }
  
  if (priv->mode == CAL_MODE_REMOTE)
  {
    if (!e_cal_sync_server_object_add(cb, comp, FALSE))
    {
      g_warning("Could not add object to server");
      e_cal_component_set_sync_state(comp, E_CAL_COMPONENT_LOCALLY_CREATED);
      e_cal_sync_client_changes_insert(cb, comp);
    }
    else
      e_cal_component_set_sync_state(comp, E_CAL_COMPONENT_IN_SYNCH);
  }
  else
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
    *calobj = e_cal_component_get_as_string(comp);

out:
  g_mutex_unlock(priv->sync_mutex);
  
  return status;
}

// modifies an existing event/task
static ECalBackendSyncStatus
e_cal_backend_3e_modify_object(ECalBackendSync * backend,
                               EDataCal * cal,
                               const char *calobj,
                               CalObjModType mod,
                               char **old_object,
                               char **new_object)
{
  ECalBackend3e            *cb;
  ECalBackend3ePrivate     *priv;
	ECalComponent            *updated_comp;
	ECalComponent            *cache_comp;
	gboolean                  online;
	const char		            *uid = NULL;
  ECalBackendSyncStatus     status = GNOME_Evolution_Calendar_Success;

  T("Modify object");

  g_return_val_if_fail(calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);
  g_return_val_if_fail(backend != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(old_object != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(new_object != NULL, GNOME_Evolution_Calendar_OtherError);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  g_mutex_lock(priv->sync_mutex);

  updated_comp = e_cal_component_new_from_string(calobj);
	if (updated_comp == NULL)
  {
    status = GNOME_Evolution_Calendar_InvalidObject;
    goto out;
  }
	
	e_cal_component_get_uid(updated_comp, &uid);
	cache_comp = e_cal_backend_cache_get_component(priv->cache, uid, NULL);

	if (cache_comp == NULL)
  {
		status = GNOME_Evolution_Calendar_ObjectNotFound;
    goto out;
  }

  if (priv->mode == CAL_MODE_REMOTE)
  {
    if (!e_cal_sync_server_object_update(cb, cache_comp, FALSE))
    {
      g_warning("Could not update component on server");
      e_cal_component_set_sync_state(updated_comp, E_CAL_COMPONENT_LOCALLY_MODIFIED);
      e_cal_sync_client_changes_insert(cb, cache_comp);
    }
  }
  else
    if (e_cal_component_get_sync_state(cache_comp) == E_CAL_COMPONENT_IN_SYNCH)
    {
      /* mark component as out of synch */
      e_cal_component_set_sync_state(updated_comp, E_CAL_COMPONENT_LOCALLY_MODIFIED);
      e_cal_sync_client_changes_insert(cb, updated_comp);
    }

  *old_object = e_cal_component_get_as_string(cache_comp);
  g_object_unref(cache_comp);
  e_cal_backend_cache_put_component(priv->cache, updated_comp);
  *new_object = e_cal_component_get_as_string(updated_comp);
  
out:
  g_mutex_unlock(priv->sync_mutex);
	
	return status;
}

// removes an object from the calendar
static ECalBackendSyncStatus e_cal_backend_3e_remove_object(ECalBackendSync * backend,
                                                            EDataCal * cal,
                                                            const char *uid,
                                                            const char *rid,
                                                            CalObjModType mod,
                                                            char **old_object,
                                                            char **object)
{
  ECalBackend3e                           *cb;
  ECalBackend3ePrivate                    *priv;
  ECalComponent                           *cache_comp;
	ECalBackendSyncStatus                   status = GNOME_Evolution_Calendar_Success;
  ECalComponentSyncState                  state;

  T("backend=%p, cal=%p, rid=%s, uid=%s", backend, cal, rid, uid);
  g_return_val_if_fail(backend != NULL, GNOME_Evolution_Calendar_OtherError);
  g_return_val_if_fail(uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;
  *old_object = *object = NULL;

  g_mutex_lock(priv->sync_mutex);

  cache_comp = e_cal_backend_cache_get_component(priv->cache, uid, rid);
	if (cache_comp == NULL)
  {
    status = GNOME_Evolution_Calendar_ObjectNotFound;
    goto out;
  }

  state = e_cal_component_get_sync_state(cache_comp);
  switch (state)
  {
    case E_CAL_COMPONENT_IN_SYNCH:
    case E_CAL_COMPONENT_LOCALLY_MODIFIED:

      if (priv->mode == CAL_MODE_REMOTE)
      {
        if (!e_cal_sync_server_object_delete(cb, cache_comp, FALSE))
        {
          g_warning("Could not delete component!");
          e_cal_component_set_sync_state(cache_comp,
                                         E_CAL_COMPONENT_IN_SYNCH);
          status = GNOME_Evolution_Calendar_OtherError;
          break;
        }
      }
      else
      {
        e_cal_component_set_sync_state(cache_comp,
                                       E_CAL_COMPONENT_LOCALLY_DELETED);
        if (state == E_CAL_COMPONENT_IN_SYNCH)
          e_cal_sync_client_changes_insert(cb, cache_comp);
      }

      if (!e_cal_backend_cache_put_component(priv->cache, cache_comp))
      {
        g_warning("Error when removing component, cannot put new"
                  "component component into the cache!");
        status = GNOME_Evolution_Calendar_OtherError;
        break;
      }

      break;

    case E_CAL_COMPONENT_LOCALLY_CREATED:
      // not on server yet... delete from cache
      if (!e_cal_backend_cache_remove_component(priv->cache, uid, rid))
      {
        g_warning("Cannot remove component from cache!");
        status = GNOME_Evolution_Calendar_OtherError;
        // do not send it to server - remove object from list of changes
      }
      e_cal_sync_client_changes_remove(cb, cache_comp);
      break;

    case E_CAL_COMPONENT_LOCALLY_DELETED:
      // nothing to do...
      g_warning("Deleting component already marked as deleted");
      status = GNOME_Evolution_Calendar_OtherError;
      break;
  }

  if (status == GNOME_Evolution_Calendar_Success)
    *old_object = e_cal_component_get_as_string(cache_comp);

out:
  g_mutex_unlock(priv->sync_mutex);

  return status;
}

/* not yet implemented functions */

// returns F/B information for a list of users
static ECalBackendSyncStatus
e_cal_backend_3e_get_free_busy(ECalBackendSync * backend,
                               EDataCal * cal,
                               GList * users,
                               time_t start,
                               time_t end,
                               GList ** freebusy)
{
  ECalBackend3e *cb;
  ECalBackend3ePrivate *priv;
  gchar *address, *name;
  icalcomponent *vfb;
  char *calobj;
  
  // FIXME: what to do here ?

  T("");

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  g_return_val_if_fail(start != -1 && end != -1, GNOME_Evolution_Calendar_InvalidRange);
  g_return_val_if_fail(start <= end, GNOME_Evolution_Calendar_InvalidRange);

  if (!priv->cache)
    return GNOME_Evolution_Calendar_NoSuchCal;

  return GNOME_Evolution_Calendar_Success;
}

// returns a list of changes made since last check
static ECalBackendSyncStatus e_cal_backend_3e_get_changes(ECalBackendSync * backend,
                                                          EDataCal * cal,
                                                          const char *change_id,
                                                          GList ** adds,
                                                          GList ** modifies,
                                                          GList ** deletes)
{
  // FIXME: what to do here ?
  ECalBackend3e *cb;
  ECalBackend3ePrivate *priv;

  T("");

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  g_return_val_if_fail(change_id != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

  return GNOME_Evolution_Calendar_Success;
}

// discards an alarm (removes it or marks it as already displayed to the user)
static ECalBackendSyncStatus
e_cal_backend_3e_discard_alarm(ECalBackendSync * backend,
                               EDataCal * cal,
                               const char *uid,
                               const char *auid)
{
  ECalBackend3e *cb;
  ECalBackend3ePrivate *priv;

  T("");

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;
  
  // FIXME: what to do here ?

  return GNOME_Evolution_Calendar_Success;
}

// import a set of events/tasks in one go
static ECalBackendSyncStatus
e_cal_backend_3e_receive_objects(ECalBackendSync * backend,
                                 EDataCal * cal,
                                 const char *calobj)
{
  // FIXME: what to do here ?
  ECalBackend3e *cb;
  ECalBackend3ePrivate *priv;

  T("");

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  g_return_val_if_fail(calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

  return GNOME_Evolution_Calendar_PermissionDenied;
}

// send a set of meetings in one go, which means, for backends that do support it,
// sending information about the meeting to all attendees
static ECalBackendSyncStatus
e_cal_backend_3e_send_objects(ECalBackendSync * backend,
                              EDataCal * cal,
                              const char *calobj,
                              GList ** users,
                              char **modified_calobj)
{
  // FIXME: what to do here ?
  ECalBackend3e *cb;
  ECalBackend3ePrivate *priv;

  T("");

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  *users = NULL;
  *modified_calobj = NULL;

  return GNOME_Evolution_Calendar_PermissionDenied;
}

// returns the default timezone.
static icaltimezone *
e_cal_backend_3e_internal_get_default_timezone(ECalBackend * backend)
{
  return icaltimezone_get_utc_timezone();
}

// returns a given timezone
static icaltimezone *
e_cal_backend_3e_internal_get_timezone(ECalBackend * backend,
                                       const char *tzid)
{
  // T("");

  return strcmp(tzid, "UTC") ? icaltimezone_get_builtin_timezone_from_tzid(tzid)
    : icaltimezone_get_utc_timezone();
}

/* GObject foo */

static ECalBackendSyncClass *parent_class;

static void e_cal_backend_3e_init(ECalBackend3e * cb, ECalBackend3eClass * klass)
{
  T("cb=%p, klass=%p", cb, klass);

  if (!g_thread_supported())
    g_thread_init(NULL);

  cb->priv = g_new0(ECalBackend3ePrivate, 1);
  cb->priv->sync_cond = g_cond_new();
  cb->priv->sync_mutex = g_mutex_new();
  cb->priv->sync_thread = g_thread_create(e_cal_sync_main_thread, cb, TRUE, NULL);
  cb->priv->gconf = gconf_client_get_default();

  e_cal_backend_sync_set_lock(E_CAL_BACKEND_SYNC(cb), TRUE);
}

static void e_cal_backend_3e_dispose(GObject * object)
{
  T("object=%p", object);

  if (G_OBJECT_CLASS(parent_class)->dispose)
    G_OBJECT_CLASS(parent_class)->dispose(object);
}

static void e_cal_backend_3e_finalize(GObject * object)
{
  ECalBackend3e *cb;
  ECalBackend3ePrivate *priv;

  T("object=%p", object);

  g_return_if_fail(object != NULL);
  g_return_if_fail(E_IS_CAL_BACKEND_3E(object));

  cb = E_CAL_BACKEND_3E(object);
  priv = cb->priv;

  server_sync_signal(cb, TRUE);
  g_thread_join(priv->sync_thread);
  g_cond_free(priv->sync_cond);
  g_mutex_free(priv->sync_mutex);

  //xr_client_free(priv->conn);
  priv->conn = NULL;

  g_object_unref(priv->cache);
  priv->cache = NULL;

  g_free(priv->server_uri);
  priv->server_uri = NULL;

  g_free(priv->calname);
  priv->calname = NULL;

  g_free(priv->username);
  priv->username = NULL;

  g_free(priv->calspec);
  priv->calspec = NULL;

  g_free(priv->password);
  priv->password = NULL;

  g_free(priv->sync_stamp);
  priv->sync_stamp = NULL;

  if (priv->default_zone)
  {
    icaltimezone_free(priv->default_zone, 1);
    priv->default_zone = NULL;
  }

  g_object_unref(priv->gconf);
  priv->gconf = NULL;

  g_list_foreach(priv->sync_clients_changes, (GFunc)g_object_unref, NULL);
  g_list_free(priv->sync_clients_changes);

  g_free(priv);
  cb->priv = NULL;

  G_OBJECT_CLASS(parent_class)->finalize(object);
}

static void e_cal_backend_3e_class_init(ECalBackend3eClass * class)
{
  GObjectClass *object_class;
  ECalBackendClass *backend_class;
  ECalBackendSyncClass *sync_class;

  object_class = (GObjectClass *) class;
  backend_class = (ECalBackendClass *) class;
  sync_class = (ECalBackendSyncClass *) class;

  parent_class = (ECalBackendSyncClass *) g_type_class_peek_parent(class);

  object_class->dispose = e_cal_backend_3e_dispose;
  object_class->finalize = e_cal_backend_3e_finalize;

  sync_class->is_read_only_sync = e_cal_backend_3e_is_read_only;
  sync_class->get_cal_address_sync = e_cal_backend_3e_get_cal_address;
  sync_class->get_alarm_email_address_sync = e_cal_backend_3e_get_alarm_email_address;
  sync_class->get_ldap_attribute_sync = e_cal_backend_3e_get_ldap_attribute;
  sync_class->get_static_capabilities_sync = e_cal_backend_3e_get_static_capabilities;
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

GType e_cal_backend_3e_get_type(void)
{
  static GType e_cal_backend_3e_type = 0;

  if (!e_cal_backend_3e_type)
  {
    static GTypeInfo info = {
      sizeof(ECalBackend3eClass),
      (GBaseInitFunc) NULL,
      (GBaseFinalizeFunc) NULL,
      (GClassInitFunc) e_cal_backend_3e_class_init,
      NULL, NULL,
      sizeof(ECalBackend3e),
      0,
      (GInstanceInitFunc) e_cal_backend_3e_init,
      NULL
    };
    e_cal_backend_3e_type = g_type_register_static(E_TYPE_CAL_BACKEND_SYNC, "ECalBackend3e", &info, 0);
  }

  return e_cal_backend_3e_type;
}
