#include <config.h>
#include <string.h>
#include <unistd.h>
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

#define DEBUG 1
#ifdef DEBUG
#include <syslog.h>
#define D(fmt, args...) syslog(LOG_DEBUG, "DEBUG: %s " fmt, G_STRLOC, ## args)
#define T(fmt, args...) syslog(LOG_DEBUG, "TRACE: %s(" fmt ")", G_STRFUNC, ## args)
#else
#define D(fmt, args...)
#define T(fmt, args...)
#endif

#include "interface/ESClient.xrc.h"

struct _ECalBackend3ePrivate {
    /*
     * Remote connection info 
     */
    char *server_uri;
    xr_client_conn *conn;
    gboolean is_open;
    gboolean is_loaded;
    char *username;
    char *password;
    char *calname;

    /*
     * Local/remote mode 
     */
    CalMode mode;

    /*
     * The file cache 
     */
    ECalBackendCache *cache;

    /*
     * The calendar's default timezone, used for resolving DATE and
     * floating DATE-TIME values. 
     */
    icaltimezone *default_zone;
};

/* helper functions */

static void e_cal_backend_notify_gerror_error(ECalBackend * backend, char *message, GError* err)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;

    if (err == NULL)
        return;

    T("backend=%p, message=%s, err=%p", backend, message, err);

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    char *error_message = g_strdup_printf("%s (%d: %s).", message, err->code, err->message);

    e_cal_backend_notify_error(backend, error_message);
    g_free(error_message);
}

/* calendar backend functions */

static ECalBackendSyncStatus e_cal_backend_3e_get_static_capabilities(ECalBackendSync * backend, EDataCal * cal, char **capabilities)
{
    T("backend=%p, cal=%p, capabilities=%p", backend, cal, capabilities);
//    *capabilities = g_strdup(
//    CAL_STATIC_CAPABILITY_NO_ALARM_REPEAT "," // Disable automatic repeating of alarms
//    CAL_STATIC_CAPABILITY_NO_AUDIO_ALARMS ","  // Disable particular alarm type
//    CAL_STATIC_CAPABILITY_NO_DISPLAY_ALARMS ","  // Disable particular alarm type
//    CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS ","  // Disable particular alarm type
//    CAL_STATIC_CAPABILITY_NO_PROCEDURE_ALARMS ","  // Disable particular alarm type
//    CAL_STATIC_CAPABILITY_NO_TASK_ASSIGNMENT ","
//    CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
//    CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
//    CAL_STATIC_CAPABILITY_NO_TRANSPARENCY ","
//    CAL_STATIC_CAPABILITY_ONE_ALARM_ONLY "," // Checks if a calendar supports only one alarm per component.
//    CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ATTEND "," // Checks if a calendar forces organizers of meetings to be also attendees.
//    CAL_STATIC_CAPABILITY_ORGANIZER_NOT_EMAIL_ADDRESS ","
//    CAL_STATIC_CAPABILITY_REMOVE_ALARMS ","
//    CAL_STATIC_CAPABILITY_SAVE_SCHEDULES "," // Checks whether the calendar saves schedules.
//    CAL_STATIC_CAPABILITY_NO_CONV_TO_ASSIGN_TASK ","
//    CAL_STATIC_CAPABILITY_NO_CONV_TO_RECUR ","
//    CAL_STATIC_CAPABILITY_NO_GEN_OPTIONS ","
//    CAL_STATIC_CAPABILITY_RECURRENCES_NO_MASTER "," // Checks if the calendar has a master object for recurrences.
//    CAL_STATIC_CAPABILITY_ORGANIZER_MUST_ACCEPT "," // Checks whether a calendar requires organizer to accept their attendance to meetings.
//    CAL_STATIC_CAPABILITY_DELEGATE_SUPPORTED ","
//    CAL_STATIC_CAPABILITY_NO_ORGANIZER ","
//    CAL_STATIC_CAPABILITY_DELEGATE_TO_MANY ","
//    CAL_STATIC_CAPABILITY_HAS_UNACCEPTED_MEETING ","
//    CAL_STATIC_CAPABILITY_REQ_SEND_OPTIONS
//    );
    *capabilities = g_strdup("");
    return GNOME_Evolution_Calendar_Success;
}

static gboolean e_cal_backend_3e_is_loaded(ECalBackend * backend)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;

    T("backend=%p", backend);

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    return priv->is_loaded;
}

static void e_cal_backend_3e_set_mode(ECalBackend * backend, CalMode mode)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;
    GNOME_Evolution_Calendar_CalMode set_mode;

    T("backend=%p, mode=%d", backend, mode);

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    switch (mode)
    {
       case CAL_MODE_LOCAL:
           priv->mode = mode;
           set_mode = cal_mode_to_corba(mode);
           if (priv->is_loaded && priv->is_open)
           {
               xr_client_close(priv->conn);
               priv->is_open = FALSE;
           }
           break;
       case CAL_MODE_REMOTE:
       case CAL_MODE_ANY:
           priv->mode = CAL_MODE_REMOTE;
           set_mode = GNOME_Evolution_Calendar_MODE_REMOTE;
           if (priv->is_loaded && !priv->is_open)
           {
               //XXX: connect again?
           }
           break;
       default:
           set_mode = GNOME_Evolution_Calendar_MODE_ANY;
           break;
    }

    if (priv->is_loaded)
    {
        if (set_mode == GNOME_Evolution_Calendar_MODE_ANY)
            e_cal_backend_notify_mode(backend, GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED, cal_mode_to_corba(priv->mode));
        else
            e_cal_backend_notify_mode(backend, GNOME_Evolution_Calendar_CalListener_MODE_SET, set_mode);
    }
}

static ECalBackendSyncStatus e_cal_backend_3e_open(ECalBackendSync * backend, EDataCal * cal, gboolean only_if_exists, const char *username, const char *password)
{
    GError* err = NULL;
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;
    int rs;

    T("backend=%p, cal=%p, only_if_exists=%d, username=%s, password=%s", backend, cal, only_if_exists, username, password);

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    if (!priv->is_loaded)
    {
        const char *uri;
        EUri *euri;

        /*
         * parse URI and authentication info 
         */

        uri = e_cal_backend_get_uri(E_CAL_BACKEND(backend));
        euri = e_uri_new(uri);
        g_free(priv->server_uri);
        priv->server_uri = g_strdup_printf("https://%s:4444/ESClient", euri->host);
        g_free(priv->calname);
        priv->calname = (euri->path && strlen(euri->path) > 1) ? g_strdup(euri->path + 1) : NULL;
        g_free(priv->username);
        priv->username = g_strdup(username);
        g_free(priv->password);
        priv->password = g_strdup(password);
        e_uri_free(euri);

        D("3es uri=%s\n", priv->server_uri);
        D("3es calname=%s\n", priv->calname);

        /*
         * create/load cache 
         */

        priv->cache = e_cal_backend_cache_new(e_cal_backend_get_uri(E_CAL_BACKEND(backend)), E_CAL_SOURCE_TYPE_EVENT);
        if (!priv->cache)
        {
            e_cal_backend_notify_error(E_CAL_BACKEND(cb), "Could not create cache file");
            return GNOME_Evolution_Calendar_OtherError;
        }

        if (priv->default_zone)
        {
            e_cal_backend_cache_put_default_timezone(priv->cache, priv->default_zone);
        }

        priv->is_loaded = TRUE;
    }

    /*
     * prepare client connection object 
     */

    if (priv->conn == NULL)
    {
        priv->conn = xr_client_new(&err);
        if (err != NULL)
        {
            e_cal_backend_notify_gerror_error(E_CAL_BACKEND(backend), "Failed to initialize XML-RPC client library.", err);
            return GNOME_Evolution_Calendar_OtherError;
        }
    }

    if (priv->is_open)
    {
        xr_client_close(priv->conn);
        priv->is_open = FALSE;
    }

    if (priv->mode == CAL_MODE_LOCAL)
        return GNOME_Evolution_Calendar_Success;

    /*
     * connect 
     */

    xr_client_open(priv->conn, priv->server_uri, &err);
    if (err != NULL)
    {
        e_cal_backend_notify_gerror_error(E_CAL_BACKEND(backend), "Failed to estabilish connection to the server", err);
        g_clear_error(&err);
        return GNOME_Evolution_Calendar_OtherError;
    }

    priv->is_open = TRUE;

    /*
     * authenticate to the server 
     */

    rs = ESClient_auth(priv->conn, priv->username, priv->password, &err);
    if (err != NULL)
    {
        e_cal_backend_notify_gerror_error(E_CAL_BACKEND(backend), "Authentication failed", err);
        g_clear_error(&err);
        xr_client_close(priv->conn);
        priv->is_open = FALSE;
        return GNOME_Evolution_Calendar_OtherError;
    }

    if (!rs)
    {
        xr_client_close(priv->conn);
        priv->is_open = FALSE;
        e_cal_backend_notify_error(E_CAL_BACKEND(backend), "Authentication failed (invalid password or username)");
        return GNOME_Evolution_Calendar_AuthenticationFailed;
    }

    /*
     * check for calendar and create it if necessary
     */

    rs = ESClient_hasCalendar(priv->conn, priv->calname, &err);
    if (err != NULL)
    {
        e_cal_backend_notify_gerror_error(E_CAL_BACKEND(backend), "Calendar presence check failed", err);
        g_clear_error(&err);
        xr_client_close(priv->conn);
        priv->is_open = FALSE;
        return GNOME_Evolution_Calendar_OtherError;
    }

    if (rs)
    {
        return GNOME_Evolution_Calendar_Success;
    }

    if (only_if_exists)
    {
        e_cal_backend_notify_error(E_CAL_BACKEND(backend), "Calendar does not exist on the server");
        xr_client_close(priv->conn);
        priv->is_open = FALSE;
        return GNOME_Evolution_Calendar_NoSuchCal;
    }

    /*
     * create new calendar on the server 
     */

    ESClient_newCalendar(priv->conn, priv->calname, &err);
    if (err != NULL)
    {
        e_cal_backend_notify_gerror_error(E_CAL_BACKEND(backend), "Calendar creation failed", err);
        g_clear_error(&err);
        xr_client_close(priv->conn);
        priv->is_open = FALSE;
        return GNOME_Evolution_Calendar_OtherError;
    }

    return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus e_cal_backend_3e_is_read_only(ECalBackendSync * backend, EDataCal * cal, gboolean * read_only)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    T("backend=%p, cal=%p, read_only=%p", backend, cal, read_only);

    *read_only = !priv->is_open;
    return GNOME_Evolution_Calendar_Success;
}

static CalMode e_cal_backend_3e_get_mode(ECalBackend * backend)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;

    T("backend=%p", backend);

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    return priv->mode;
}

static ECalBackendSyncStatus e_cal_backend_3e_set_default_zone(ECalBackendSync * backend, EDataCal * cal, const char *tzobj)
{
    icalcomponent *tz_comp;
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;
    icaltimezone *zone;

    T("backend=%p, cal=%p, tzobj=%p", backend, cal, tzobj);

    cb = (ECalBackend3e *) backend;

    g_return_val_if_fail(E_IS_CAL_BACKEND_3E(cb), GNOME_Evolution_Calendar_OtherError);
    g_return_val_if_fail(tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

    priv = cb->priv;

    tz_comp = icalparser_parse_string(tzobj);
    if (!tz_comp)
        return GNOME_Evolution_Calendar_InvalidObject;

    zone = icaltimezone_new();
    icaltimezone_set_component(zone, tz_comp);

    if (priv->default_zone)
        icaltimezone_free(priv->default_zone, 1);

    /*
     * Set the default timezone to it. 
     */
    priv->default_zone = zone;

    return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus e_cal_backend_3e_remove(ECalBackendSync * backend, EDataCal * cal)
{
    GError* err = NULL;
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;

    T("backend=%p, cal=%p", backend, cal);

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    if (!priv->cache)
        return GNOME_Evolution_Calendar_OtherError;

    if (priv->is_open)
    {
        ESClient_deleteCalendar(priv->conn, priv->calname, &err);
        if (err != NULL)
        {
            e_cal_backend_notify_gerror_error(E_CAL_BACKEND(backend), "Calendar removal failed", err);
            g_clear_error(&err);
            return GNOME_Evolution_Calendar_OtherError;
        }
    }

    e_file_cache_remove(E_FILE_CACHE(priv->cache));
    return GNOME_Evolution_Calendar_Success;
}

/* not yet implemented functions */

static ECalBackendSyncStatus e_cal_backend_3e_get_cal_address(ECalBackendSync * backend, EDataCal * cal, char **address)
{
    *address = NULL;

    T("");

    return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus e_cal_backend_3e_get_ldap_attribute(ECalBackendSync * backend, EDataCal * cal, char **attribute)
{
    *attribute = NULL;

    T("");

    return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus e_cal_backend_3e_get_alarm_email_address(ECalBackendSync * backend, EDataCal * cal, char **address)
{
    *address = NULL;
    T("");

    return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus e_cal_backend_3e_get_default_object(ECalBackendSync * backend, EDataCal * cal, char **object)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;
    icalcomponent *icalcomp;
    icalcomponent_kind kind;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    kind = e_cal_backend_get_kind(E_CAL_BACKEND(backend));
    icalcomp = e_cal_util_new_component(kind);
    *object = g_strdup(icalcomponent_as_ical_string(icalcomp));
    icalcomponent_free(icalcomp);

    return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus e_cal_backend_3e_get_object(ECalBackendSync * backend, EDataCal * cal, const char *uid, const char *rid, char **object)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;
    ECalComponent *comp = NULL;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    g_return_val_if_fail(uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

    if (!priv->cache)
        return GNOME_Evolution_Calendar_ObjectNotFound;

    comp = e_cal_backend_cache_get_component(priv->cache, uid, rid);
    if (!comp)
        return GNOME_Evolution_Calendar_ObjectNotFound;

    *object = e_cal_component_get_as_string(comp);
    g_object_unref(comp);

    return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus e_cal_backend_3e_get_timezone(ECalBackendSync * backend, EDataCal * cal, const char *tzid, char **object)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;
    const icaltimezone *zone;
    icalcomponent *icalcomp;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    g_return_val_if_fail(tzid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

    /*
     * first try to get the timezone from the cache 
     */
    zone = e_cal_backend_cache_get_timezone(priv->cache, tzid);
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

static ECalBackendSyncStatus e_cal_backend_3e_add_timezone(ECalBackendSync * backend, EDataCal * cal, const char *tzobj)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;
    icalcomponent *tz_comp;
    icaltimezone *zone;

    T("");

    cb = (ECalBackend3e *) backend;

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
    e_cal_backend_cache_put_timezone(priv->cache, zone);

    return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus e_cal_backend_3e_get_object_list(ECalBackendSync * backend, EDataCal * cal, const char *sexp, GList ** objects)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;
    GList *components, *l;
    ECalBackendSExp *cbsexp;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    if (!priv->cache)
        return GNOME_Evolution_Calendar_NoSuchCal;

    /*
     * process all components in the cache 
     */
    cbsexp = e_cal_backend_sexp_new(sexp);

    *objects = NULL;
    components = e_cal_backend_cache_get_components(priv->cache);
    for (l = components; l != NULL; l = l->next)
    {
        if (e_cal_backend_sexp_match_comp(cbsexp, E_CAL_COMPONENT(l->data), E_CAL_BACKEND(backend)))
        {
            *objects = g_list_append(*objects, e_cal_component_get_as_string(l->data));
        }
    }

    g_list_foreach(components, (GFunc) g_object_unref, NULL);
    g_list_free(components);
    g_object_unref(cbsexp);

    return GNOME_Evolution_Calendar_Success;
}

static void e_cal_backend_3e_start_query(ECalBackend * backend, EDataCalView * query)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;
    GList *components, *l, *objects = NULL;
    ECalBackendSExp *cbsexp;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    e_data_cal_view_notify_done(query, GNOME_Evolution_Calendar_NoSuchCal);

#if 0
    if (!priv->cache)
    {
        e_data_cal_view_notify_done(query, GNOME_Evolution_Calendar_NoSuchCal);
        return;
    }

    /*
     * process all components in the cache 
     */
    cbsexp = e_cal_backend_sexp_new(e_data_cal_view_get_text(query));

    objects = NULL;
    components = e_cal_backend_cache_get_components(priv->cache);
    for (l = components; l != NULL; l = l->next)
    {
        if (e_cal_backend_sexp_match_comp(cbsexp, E_CAL_COMPONENT(l->data), E_CAL_BACKEND(backend)))
        {
            objects = g_list_append(objects, e_cal_component_get_as_string(l->data));
        }
    }

    e_data_cal_view_notify_objects_added(query, (const GList *) objects);

    g_list_foreach(components, (GFunc) g_object_unref, NULL);
    g_list_free(components);
    g_list_foreach(objects, (GFunc) g_free, NULL);
    g_list_free(objects);
    g_object_unref(cbsexp);

    e_data_cal_view_notify_done(query, GNOME_Evolution_Calendar_Success);
#endif
}

static ECalBackendSyncStatus e_cal_backend_3e_get_free_busy(ECalBackendSync * backend, EDataCal * cal, GList * users, time_t start, time_t end, GList ** freebusy)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;
    gchar *address, *name;
    icalcomponent *vfb;
    char *calobj;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    g_return_val_if_fail(start != -1 && end != -1, GNOME_Evolution_Calendar_InvalidRange);
    g_return_val_if_fail(start <= end, GNOME_Evolution_Calendar_InvalidRange);

    if (!priv->cache)
        return GNOME_Evolution_Calendar_NoSuchCal;

    return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus e_cal_backend_3e_get_changes(ECalBackendSync * backend, EDataCal * cal, const char *change_id, GList ** adds, GList ** modifies, GList ** deletes)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    g_return_val_if_fail(change_id != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

    /*
     * FIXME 
     */
    return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus e_cal_backend_3e_discard_alarm(ECalBackendSync * backend, EDataCal * cal, const char *uid, const char *auid)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    /*
     * FIXME 
     */
    return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus e_cal_backend_3e_create_object(ECalBackendSync * backend, EDataCal * cal, char **calobj, char **uid)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    return GNOME_Evolution_Calendar_PermissionDenied;
}

static ECalBackendSyncStatus e_cal_backend_3e_modify_object(ECalBackendSync * backend, EDataCal * cal, const char *calobj, CalObjModType mod, char **old_object, char **new_object)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    g_return_val_if_fail(calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

    return GNOME_Evolution_Calendar_PermissionDenied;
}

static ECalBackendSyncStatus e_cal_backend_3e_remove_object(ECalBackendSync * backend, EDataCal * cal, const char *uid, const char *rid, CalObjModType mod, char **old_object, char **object)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    g_return_val_if_fail(uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

    *old_object = *object = NULL;

    return GNOME_Evolution_Calendar_PermissionDenied;
}

static ECalBackendSyncStatus e_cal_backend_3e_receive_objects(ECalBackendSync * backend, EDataCal * cal, const char *calobj)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    g_return_val_if_fail(calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

    return GNOME_Evolution_Calendar_PermissionDenied;
}

static ECalBackendSyncStatus e_cal_backend_3e_send_objects(ECalBackendSync * backend, EDataCal * cal, const char *calobj, GList ** users, char **modified_calobj)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    *users = NULL;
    *modified_calobj = NULL;

    return GNOME_Evolution_Calendar_PermissionDenied;
}

static icaltimezone *e_cal_backend_3e_internal_get_default_timezone(ECalBackend * backend)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    if (!priv->cache)
        return NULL;

    return NULL;
}

static icaltimezone *e_cal_backend_3e_internal_get_timezone(ECalBackend * backend, const char *tzid)
{
    ECalBackend3e *cb;
    ECalBackend3ePrivate *priv;
    icaltimezone *zone;

    T("");

    cb = E_CAL_BACKEND_3E(backend);
    priv = cb->priv;

    if (!strcmp(tzid, "UTC"))
        zone = icaltimezone_get_utc_timezone();
    else
    {
        zone = icaltimezone_get_builtin_timezone_from_tzid(tzid);
    }

    return zone;
}

/* GObject foo */

static ECalBackendSyncClass *parent_class;

static void e_cal_backend_3e_init(ECalBackend3e * cb, ECalBackend3eClass * klass)
{
    T("cb=%p, klass=%p", cb, klass);

    cb->priv = g_new0(ECalBackend3ePrivate, 1);

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

    xr_client_free(priv->conn);
    priv->conn = NULL;

    g_object_unref(priv->cache);
    priv->cache = NULL;

    g_free(priv->server_uri);
    priv->server_uri = NULL;

    g_free(priv->calname);
    priv->calname = NULL;

    g_free(priv->username);
    priv->username = NULL;

    g_free(priv->password);
    priv->password = NULL;

    if (priv->default_zone)
    {
        icaltimezone_free(priv->default_zone, 1);
        priv->default_zone = NULL;
    }

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
