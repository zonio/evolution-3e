/*
 * Authors: Ondrej Jirman <ondrej.jirman@zonio.net>
 *          Stanislav Slusny <stanislav.slusny@zonio.net>
 *
 * Copyright 2007-2008 Zonio, s.r.o.
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
#include <libedataserver/e-xml-hash-utils.h>
#include <gio/gio.h>

#include <glib/gstdio.h>
#define mydebug(args...) do{FILE * fp = g_fopen("/dev/pts/2", "w"); fprintf (fp, args); fclose (fp);}while(0)

#define T(fmt, args...) g_print("TRACE[%p]: %s(backend=%p " fmt ")\n", g_thread_self(), G_STRFUNC, backend, ## args)
//#define T(fmt, args...)

#define BACKEND_METHOD_CHECKED_RETVAL(val, args...) \
    GError * local_err = NULL; \
    ECalBackend3e *cb = (ECalBackend3e *)backend; \
    ECalBackend3ePrivate *priv; \
    g_return_val_if_fail(E_IS_CAL_BACKEND_3E(cb), val); \
    priv = E_CAL_BACKEND_3E_GET_PRIVATE (backend); \
    T(args)

#define BACKEND_METHOD_CHECKED(args...) \
    BACKEND_METHOD_CHECKED_RETVAL(OtherError, ## args)

#define BACKEND_METHOD_CHECKED_NORETVAL(args...) \
    GError * local_err = NULL; \
    ECalBackend3e *cb = (ECalBackend3e *)backend; \
    ECalBackend3ePrivate *priv; \
    g_return_if_fail(E_IS_CAL_BACKEND_3E(cb)); \
    priv = cb->priv; \
    T(args)

/* error wrapers */

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)
#define EDC_ERROR_EX(_code, _msg) e_data_cal_create_error (_code, _msg)

/* cache API wrappers */

#define e_cal_backend_store_get_components(cache) e_cal_backend_3e_store_get_components(cb, cache)
#define e_cal_backend_store_get_components_by_uid(cache, uid) e_cal_backend_3e_store_get_components_by_uid(cb, cache, uid)
#define e_cal_backend_store_get_component(cache, uid, rid) e_cal_backend_3e_store_get_component(cb, cache, uid, rid)
#define e_cal_backend_store_put_component(cache, c) e_cal_backend_3e_store_put_component(cb, cache, c)
#define e_cal_backend_store_remove_component(cache, uid, rid) e_cal_backend_3e_store_remove_component(cb, cache, uid, rid)
#define e_cal_backend_store_get_timezone(cache, tzid) e_cal_backend_3e_store_get_timezone(cb, cache, tzid)
#define e_cal_backend_store_put_timezone(cache, tzobj) e_cal_backend_3e_store_put_timezone(cb, cache, tzobj)

/**
 * Open the calendar backend.
 */
static void e_cal_backend_3e_open(ECalBackendSync *backend, EDataCal *cal,
                                  GCancellable *cancellable,
                                  gboolean only_if_exists, GError ** err)
{
    BACKEND_METHOD_CHECKED_NORETVAL("only_if_exists=%d", only_if_exists);

    if (!priv->is_loaded)
    {
        ECredentials *credentials;
        guint prompt_flags;
        gchar *prompt_flags_str;
        gchar *credkey;
        const gchar * cache_dir;

        /* load calendar info */
        if (!e_cal_backend_3e_calendar_info_load(cb))
        {
            g_propagate_error(err, EDC_ERROR_EX(OtherError,
                "Trying to open non-3e source using 3e backend."));
            e_cal_backend_notify_error(E_CAL_BACKEND(backend),
                "Trying to open non-3e source using 3e backend.");
            return;
        }

        cache_dir = e_cal_backend_get_cache_dir(E_CAL_BACKEND(cb));

        g_mkdir_with_parents (cache_dir, 0700);

        /* open/create cache */
        priv->store = e_cal_backend_file_store_new(cache_dir);

        e_cal_backend_store_load (priv->store);

        if (priv->store == NULL)
        {
            g_propagate_error(err, EDC_ERROR_EX(OtherError,
                              "Failed to open local calendar cache."));
            e_cal_backend_notify_error(E_CAL_BACKEND(cb),
                                       "Failed to open local calendar cache.");
            return;
        }

        e_cal_backend_3e_attachment_store_load(cb);

        priv->is_loaded = TRUE;

        if (cb->priv->credentials)
            credentials = cb->priv->credentials;
        else
            credentials = e_credentials_new ();

        prompt_flags = E_CREDENTIALS_PROMPT_FLAG_REMEMBER_FOREVER
                     | E_CREDENTIALS_PROMPT_FLAG_SECRET
                     | E_CREDENTIALS_PROMPT_FLAG_ONLINE;

        prompt_flags_str = e_credentials_util_prompt_flags_to_string (prompt_flags);
        e_credentials_set (credentials, E_CREDENTIALS_KEY_PROMPT_FLAGS, prompt_flags_str);
        g_free (prompt_flags_str);

        credkey = e_source_get_property (e_backend_get_source (E_BACKEND (backend)), "auth-key");
        e_credentials_set (credentials, E_CREDENTIALS_KEY_PROMPT_KEY, credkey);

        e_cal_backend_notify_auth_required(E_CAL_BACKEND(backend), TRUE, credentials);
        e_credentials_free (credentials);
        return;
    }

    e_cal_backend_3e_periodic_sync_enable(cb);
    e_cal_backend_notify_readonly (E_CAL_BACKEND(backend), FALSE);
    e_cal_backend_notify_opened (E_CAL_BACKEND(backend), NULL);
    e_cal_backend_notify_online (E_CAL_BACKEND(backend), TRUE);
}

/**
 * Authenticate user.
 */
static void e_cal_backend_3e_authenticate_user(ECalBackendSync *backend,
                                               GCancellable *cancellable,
                                               ECredentials *credentials,
                                               GError **err)
{
    BACKEND_METHOD_CHECKED_NORETVAL();

    g_free (cb->priv->username);
    g_free (cb->priv->password);
    g_free (cb->priv->server_uri);

    e_credentials_free (priv->credentials);

    if (!credentials || !e_credentials_has_key (credentials, E_CREDENTIALS_KEY_USERNAME))
    {
        g_propagate_error (err, EDC_ERROR (AuthenticationRequired));
        return;
    }

    cb->priv->username = e_credentials_get (credentials, E_CREDENTIALS_KEY_USERNAME);
    cb->priv->password = e_credentials_get (credentials, E_CREDENTIALS_KEY_PASSWORD);
    cb->priv->server_uri = NULL;//get_eee_server_hostname (cb->priv->username);

    priv->credentials = e_credentials_new_clone (credentials);

    /* enable sync */
    if (e_cal_backend_3e_calendar_is_online(cb))
    {
        cb->priv->is_loaded = TRUE;
        e_cal_backend_3e_periodic_sync_enable(cb);
        e_cal_backend_notify_readonly(E_CAL_BACKEND(backend), FALSE);
        e_cal_backend_notify_opened(E_CAL_BACKEND(backend), NULL);
        e_cal_backend_notify_online(E_CAL_BACKEND(backend), TRUE);
    }
}

/** Remove the calendar.
 */
static void e_cal_backend_3e_remove(ECalBackendSync *backend, EDataCal *cal,
                                    GCancellable *cancellable, GError **err)
{
    BACKEND_METHOD_CHECKED_NORETVAL();

    if (priv->is_loaded)
    {
        priv->is_loaded = FALSE;
        e_cal_backend_3e_periodic_sync_stop(cb);
        e_cal_backend_store_remove(priv->store);
        priv->store = NULL;
    }

    return;
}

/** Refresh sync
 */
static void e_cal_backend_3e_refresh(ECalBackendSync *backend, EDataCal *cal,
                                     GCancellable *cancellable, GError **err)
{
    BACKEND_METHOD_CHECKED_NORETVAL();
    
    return;
}

/**
 * Returns a list of events/tasks given a set of conditions.
 */
static void e_cal_backend_3e_get_object_list(ECalBackendSync *backend,
                                             EDataCal *cal,
                                             GCancellable *cancellable,
                                             const gchar *sexp,
                                             GSList **objects,
                                             GError **err)
{
    GSList *all_objects;
    GSList *iter;
    ECalBackendSExp *cbsexp;

    BACKEND_METHOD_CHECKED_NORETVAL("sexp=%s", sexp);

    g_return_if_fail(objects != NULL);
    g_return_if_fail(sexp != NULL);

    cbsexp = e_cal_backend_sexp_new(sexp);
    if (cbsexp == NULL)
    {
        g_propagate_error(err, EDC_ERROR(InvalidQuery));
        return;
    }

    all_objects = e_cal_backend_store_get_components(priv->store);
    for (iter = all_objects; iter != NULL; iter = iter->next)
    {
        ECalComponent *comp = E_CAL_COMPONENT(iter->data);

        if (e_cal_backend_sexp_match_comp(cbsexp, comp, E_CAL_BACKEND(backend)))
        {
            *objects = g_slist_append(*objects, e_cal_component_get_as_string(comp));
        }

        g_object_unref(comp);
    }

    g_slist_free(all_objects);
    g_object_unref(cbsexp);

    return;
}

/**
 * Starts a live query on the backend.
 */
static void e_cal_backend_3e_start_view(ECalBackend *backend,
                                        EDataCalView *view)
{
    EDataCalCallStatus status;
    GSList *objects = NULL;
    GError **err = NULL;

    BACKEND_METHOD_CHECKED_NORETVAL("view=%p", view);

    e_cal_backend_3e_get_object_list(E_CAL_BACKEND_SYNC(backend),
                                              NULL,
                                              NULL,
                                              e_data_cal_view_get_text(view),
                                              &objects,
                                              &local_err);

    if (!local_err)
    {
        e_data_cal_view_notify_objects_added(view, objects);
        g_slist_foreach(objects, (GFunc)g_free, NULL);
        g_slist_free(objects);
    }

    e_data_cal_view_notify_complete(view, NULL);
}

/**
 * Returns a list of events/tasks given a set of conditions.
*/
static void e_cal_backend_3e_get_object(ECalBackendSync *backend,
                                        EDataCal *cal,
                                        GCancellable *cancellable,
                                        const gchar *uid,
                                        const gchar *rid,
                                        gchar **object,
                                        GError **err)
{
    BACKEND_METHOD_CHECKED_NORETVAL("uid=%s  rid=%s", uid, rid);

    //g_return_if_fail(*object != NULL);
    g_return_if_fail(uid != NULL);

    if (rid && *rid)
    {
        /* get single detached instance */
        ECalComponent *dinst = e_cal_backend_store_get_component(priv->store, uid, rid);

        if (!dinst)
        {
            g_propagate_error(err, EDC_ERROR(ObjectNotFound));
            return;
        }

        //*object = e_cal_component_get_as_string(dinst);
        *object = g_strdup(icalcomponent_as_ical_string(e_cal_component_get_icalcomponent(dinst)));
        g_object_unref(dinst);
    }
    else
    {
        /* get master object with detached instances */
        ECalComponent *master = e_cal_backend_store_get_component(priv->store, uid, rid);
        if (!master)
        {
            g_propagate_error(err, EDC_ERROR(ObjectNotFound));
            return;
        }

        if (!e_cal_component_has_recurrences(master))
        {
            /* normal non-recurring object */
            //*object = e_cal_component_get_as_string(master);
            *object = g_strdup(icalcomponent_as_ical_string(e_cal_component_get_icalcomponent(master)));
        }
        else
        {
            /* recurring object, return it in VCALENDAR with detached instances */
            icalcomponent *icalcomp = e_cal_util_new_top_level();
            GSList *dinst_list;
            GSList *iter;

            dinst_list = e_cal_backend_store_get_components_by_uid(priv->store, uid);
            for (iter = dinst_list; iter; iter = iter->next)
            {
	        ECalComponent *dinst = E_CAL_COMPONENT(iter->data);
	        icalcomponent_add_component(icalcomp, icalcomponent_new_clone(e_cal_component_get_icalcomponent(dinst)));
                g_object_unref(dinst);
            }
            g_slist_free(dinst_list);

            *object = g_strdup(icalcomponent_as_ical_string(icalcomp));
            icalcomponent_free(icalcomp);
        }

        g_object_unref(master);
    }

    return;
}

/** Creates a new event/task in the calendar.
*/
static void e_cal_backend_3e_create_object(ECalBackendSync *backend,
                                           EDataCal *cal,
                                           GCancellable *cancellable,
                                           const gchar *calobj,
                                           gchar **uid,
                                           ECalComponent **new_object,
                                           GError **err)
{
    ECalComponent *comp;

    BACKEND_METHOD_CHECKED_NORETVAL("calobj=\n%suid=%s",
                                    calobj, *uid);

    g_return_if_fail(calobj != NULL);

    if (!e_cal_backend_3e_calendar_has_perm(cb, "write"))
    {
        g_propagate_error(err, EDC_ERROR(PermissionDenied));
        return;
    }

    comp = e_cal_component_new_from_string(calobj);

    if (comp == NULL)
    {
        g_propagate_error(err, EDC_ERROR(InvalidObject));
        return;
    }

    e_cal_component_set_outofsync (comp, TRUE);

    calobj = e_cal_component_get_as_string(comp);

    if (!e_cal_backend_store_put_component(priv->store, comp))
    {
        g_object_unref(comp);
        return; // OtherError;
    }

    e_cal_backend_3e_do_immediate_sync(cb);

    g_object_unref(comp);
    
    return; // Success;
}

/** Modifies an existing event/task.
 *
 * @todo be smarter about 'modify all instances' sitaution
 * @todo add error checking for cache operations
 */
static void e_cal_backend_3e_modify_object(ECalBackendSync *backend,
                                           EDataCal *cal,
                                           GCancellable *cancellable,
                                           const gchar *calobj,
                                           CalObjModType mod,
                                           ECalComponent **old_object,
                                           ECalComponent **new_object,
                                           GError **err)
{
    ECalComponent *cache_comp;
    ECalComponent *new_comp;
    ECalComponentId *new_id;

    BACKEND_METHOD_CHECKED_NORETVAL("calobj=%s mod=%d", calobj, mod);

    g_return_if_fail(calobj != NULL);
    g_return_if_fail(old_object != NULL);
    g_return_if_fail(new_object != NULL);

    if (!e_cal_backend_3e_calendar_has_perm(cb, "write"))
    {
        return; // PermissionDenied;
    }

    /* parse new object */
    new_comp = e_cal_component_new_from_string(calobj);
    if (new_comp == NULL)
    {
        return; // InvalidObject;
    }

    new_id = e_cal_component_get_id(new_comp);

    if ((e_cal_component_has_recurrences(new_comp) || e_cal_component_is_instance(new_comp))
        && mod == CALOBJ_MOD_ALL)
    {
        /* remove detached instances if evolution requested to modify all recurrences and
           update master object */
        GSList *comp_list = e_cal_backend_store_get_components_by_uid(priv->store, new_id->uid);
        GSList *iter;

        for (iter = comp_list; iter; iter = iter->next)
        {
            ECalComponent *inst = E_CAL_COMPONENT(iter->data);
            char *inst_str = e_cal_component_get_as_string(inst);
            ECalComponentId *inst_id = e_cal_component_get_id(inst);

            if (!e_cal_component_has_recurrences(inst) && e_cal_component_is_instance(inst))
            {
                e_cal_backend_store_remove_component(priv->store, inst_id->uid, inst_id->rid);
                e_cal_backend_notify_object_removed(E_CAL_BACKEND(backend), inst_id, inst_str, NULL);
            }
            else
            {
                ECalComponentDateTime m_s, m_e;

                //XXX: what exactly should we do here when user choose to modify all
                // instances?
                e_cal_component_get_dtstart(new_comp, &m_s);
                e_cal_component_get_dtend(new_comp, &m_e);

                e_cal_component_set_dtstart(inst, &m_s);
                e_cal_component_set_dtend(inst, &m_e);

                e_cal_component_set_recurid(inst, NULL);
                e_cal_component_set_exdate_list(inst, NULL);
                e_cal_component_commit_sequence(inst);
                e_cal_backend_store_put_component(priv->store, inst);

                e_cal_component_set_outofsync (inst, TRUE);
                char *new_inst_str = e_cal_component_get_as_string(inst);
                e_cal_backend_notify_object_modified(E_CAL_BACKEND(backend), inst_str, new_inst_str);
                g_free(new_inst_str);
            }

            e_cal_component_free_id(inst_id);
            g_free(inst_str);
            g_object_unref(inst);
        }

        g_slist_free(comp_list);
    }
    else
    {
        /* find object we are trying to modify in cache and update it */
        cache_comp = e_cal_backend_store_get_component(priv->store, new_id->uid, new_id->rid);
        if (cache_comp)
        {
            *old_object = cache_comp;
        }

        e_cal_component_set_outofsync (new_comp, TRUE);
        *new_object = g_object_ref(new_comp);
        e_cal_backend_store_put_component(priv->store, new_comp);
    }

    e_cal_backend_3e_do_immediate_sync(cb);

    g_object_unref(new_comp);
    e_cal_component_free_id(new_id);

    return; // Success;
}

/** Removes an object from the calendar.
 */
static void e_cal_backend_3e_remove_object(ECalBackendSync *backend,
                                           EDataCal *cal,
                                           GCancellable *cancellable,
                                           const gchar *uid,
                                           const gchar *rid,
                                           CalObjModType mod,
                                           ECalComponent **old_object,
                                           ECalComponent **object,
                                           GError **err)
{
    BACKEND_METHOD_CHECKED_NORETVAL("uid=%s rid=%s mod=%d", uid, rid, mod);

    g_return_if_fail(uid != NULL);
    g_return_if_fail(old_object != NULL);
    g_return_if_fail(object != NULL);

    if (!e_cal_backend_3e_calendar_has_perm(cb, "write"))
    {
        g_propagate_error(err, EDC_ERROR(PermissionDenied));
        return;
    }

    if (mod == CALOBJ_MOD_THIS)
    {
        if (rid && *rid)
        {
            /* remove detached or virtual (EXDATE) instance */
            ECalComponent *master;
            ECalComponent *dinst;
            char *old_master, *new_master;

            master = e_cal_backend_store_get_component(priv->store, uid, NULL);
            if (master == NULL)
            {
                g_propagate_error(err, EDC_ERROR(ObjectNotFound));
                return;
            }

            /* was a detached instance? remove it and set old_object */
            dinst = e_cal_backend_store_get_component(priv->store, uid, rid);
            if (dinst)
            {
                e_cal_backend_store_remove_component(priv->store, uid, rid);
                *old_object = dinst;
            }

            /* add EXDATE to the master and notify clients */
            old_master = e_cal_component_get_as_string(master);
            e_cal_util_remove_instances(
                e_cal_component_get_icalcomponent(master),
                icaltime_from_string(rid),
                CALOBJ_MOD_THIS);

            new_master = e_cal_component_get_as_string(master);
            e_cal_backend_store_put_component(priv->store, master);
            e_cal_backend_notify_object_modified(E_CAL_BACKEND(backend),
                                                 old_master,
                                                 new_master);
            g_object_unref(master);
            g_free(old_master);
            g_free(new_master);
        }
        else
        {
            /* remove one object (this will be non-recurring) */
            ECalComponent *cache_comp;

            cache_comp = e_cal_backend_store_get_component(priv->store,
                                                           uid, rid);
            if (cache_comp == NULL)
            {
                g_propagate_error(err, EDC_ERROR(ObjectNotFound));
                return;
            }

            *old_object = cache_comp;
            e_cal_backend_store_remove_component(priv->store, uid, rid);
        }
    }
    else if (mod == CALOBJ_MOD_ALL)
    {
        /* remove all instances of recurring object */
        ECalComponent *cache_comp;
        GSList *comp_list;
        GSList *iter;

        /* this will be master object */
        cache_comp = e_cal_backend_store_get_component(priv->store, uid, rid);
        if (cache_comp == NULL)
        {
            g_propagate_error(err, EDC_ERROR(ObjectNotFound));
            return;
        }

        comp_list = e_cal_backend_store_get_components_by_uid(priv->store,
                                                              uid);
        e_cal_backend_store_remove_component(priv->store, uid, rid);

        for (iter = comp_list; iter; iter = iter->next)
        {
            ECalComponent *comp = E_CAL_COMPONENT(iter->data);
            ECalComponentId *id = e_cal_component_get_id(comp);

            if (e_cal_backend_store_remove_component(priv->store, id->uid,
                                                     id->rid))
            {
                e_cal_backend_notify_object_removed(E_CAL_BACKEND(backend),
                    id, e_cal_component_get_as_string(comp), NULL);
            }

            e_cal_component_free_id(id);
            g_object_unref(comp);
        }
        g_slist_free(comp_list);

        *old_object = cache_comp;
    }
    else
    {
        g_propagate_error(err, EDC_ERROR(UnsupportedMethod));
        return;
    }

    e_cal_backend_3e_do_immediate_sync(cb);

    return; // Success;

}

/**
 * Adds a timezone to the backend.
 */
static void e_cal_backend_3e_add_timezone(ECalBackendSync *backend,
                                          EDataCal *cal,
                                          GCancellable *cancellable,
                                          const gchar *tzobj,
                                          GError **err)
{
    icalcomponent *icalcomp;
    icaltimezone *zone;

    BACKEND_METHOD_CHECKED_NORETVAL("tzobj=%s", tzobj);

    g_return_if_fail(tzobj != NULL);

    icalcomp = icalparser_parse_string(tzobj);

    if (icalcomp == NULL)
    {
        g_propagate_error(err, EDC_ERROR(InvalidObject));
        return;
    }

    if (icalcomponent_isa(icalcomp) != ICAL_VTIMEZONE_COMPONENT)
    {
        icalcomponent_free(icalcomp);
        g_propagate_error(err, EDC_ERROR(InvalidObject));
        return;
    }

    zone = icaltimezone_new();
    icaltimezone_set_component(zone, icalcomp);
    const gchar *tzid = icaltimezone_get_tzid(zone);

    T("tzid_new = %s", tzid);

    if (!e_cal_backend_store_get_timezone(priv->store, tzid))
    {
        T("Pushing timezone");
        e_cal_backend_store_put_timezone(priv->store, zone);
    }

    icaltimezone_free(zone, TRUE);

    e_cal_backend_3e_do_immediate_sync(cb);

    return;
}

/** Returns a given timezone
 */
static icaltimezone *e_cal_backend_3e_internal_get_timezone(ECalBackend *backend, const char *tzid)
{
    icaltimezone *zone;
    //icaltimezone *def_zone = icaltimezone_new();

    BACKEND_METHOD_CHECKED_RETVAL(NULL, "tzid=%s", tzid);

    zone = icaltimezone_get_builtin_timezone_from_tzid(tzid);
    //zone = (icaltimezone *)e_cal_backend_store_get_timezone(priv->store, tzid);

    if (!zone)
    {
        return icaltimezone_get_utc_timezone();
    }
    
    priv->default_zone = zone;

    return zone;
}

/** Convert UTC time to ISO string format.
 */
static char *gmtime_to_iso(time_t t)
{
    char buf[128];
    struct tm tm;

    gmtime_r(&t, &tm);
    strftime(buf, sizeof(buf), "%F %T", &tm);
    return g_strdup(buf);
}

/** Returns F/B information for a list of users.
 */
static void e_cal_backend_3e_get_free_busy(ECalBackendSync *backend,
                                           EDataCal *cal,
                                           GCancellable *cancellable,
                                           const GSList *users,
                                           time_t start, time_t end,
                                           GSList **freebusy,
                                           GError **err)
{
    char *zone;
    GSList *iter;

    BACKEND_METHOD_CHECKED_NORETVAL("users=%p start=%d end=%d",
                                    users, (int)start, (int)end);


    g_return_if_fail(users != NULL);
    g_return_if_fail(start != -1 && end != -1);
    g_return_if_fail(start <= end);
    g_return_if_fail(freebusy != NULL);

    if (!e_cal_backend_3e_calendar_is_online(cb))
    {
        return; // RepositoryOffline;
    }

    if (!e_cal_backend_3e_open_connection(cb, &local_err))
    {
        e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb),
            "Can't connect to the server to get freebusy.", local_err);
        g_error_free(local_err);

        return; // OtherError;
    }

    zone = icalcomponent_as_ical_string(icaltimezone_get_component(priv->default_zone));

g_print("Timezona = %s\n", zone);

    for (iter = (GSList *) users; iter; iter = iter->next)
    {
        char *username = iter->data;
        char *iso_start = gmtime_to_iso(start);
        char *iso_end = gmtime_to_iso(end);
        char *vfb;

        vfb = ESClient_freeBusy(priv->conn, username, iso_start, iso_end, zone, &local_err);
        if (local_err)
        {
            g_clear_error(&local_err);
        }

        if (vfb)
        {
            *freebusy = g_slist_append(*freebusy, vfb);
        }

        g_free(iso_start);
        g_free(iso_end);
    }

    e_cal_backend_3e_close_connection(cb);
}

static void fetch_attachments(ECalBackend3e *cb, ECalComponent *comp)
{
    GFile *source, *target, *target_store;
    char *source_filename, *target_filename;
    const char *uid;
    GSList *attach_list = NULL, *new_attach_list = NULL;
    GSList *iter;

    target_store = g_file_new_for_path(e_cal_backend_3e_get_cache_path(cb));

    e_cal_component_get_attachment_list(comp, &attach_list);
    e_cal_component_get_uid(comp, &uid);

    for (iter = attach_list; iter; iter = iter->next)
    {
        char *orig_uri = iter->data;

        source = g_file_new_for_path(orig_uri);
        source_filename = g_file_get_basename(source);
        target_filename = g_strdup_printf("%s-%s", uid, source_filename);
        target = g_file_get_child(target_store, target_filename);
        g_free(source_filename);
        g_free(target_filename);

        if (g_file_copy(source, target, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, NULL))
        {
            new_attach_list = g_slist_append(new_attach_list, g_file_get_uri(target));
        }

        g_object_unref(source);
        g_object_unref(target);
    }

    e_cal_component_set_attachment_list(comp, new_attach_list);

    g_slist_foreach(new_attach_list, (GFunc)g_free, NULL);
    g_slist_free(new_attach_list);
    g_slist_free(attach_list);
    g_object_unref(target_store);
}

static EDataCalCallStatus e_cal_backend_3e_receive_object(ECalBackendSync *backend, icalcomponent *icalcomp, icalproperty_method method)
{
    EDataCalCallStatus status = Success;
    ECalComponent *new_comp;
    ECalComponentId *new_id;
    ECalComponent *cache_comp;
    struct icaltimetype current;

    BACKEND_METHOD_CHECKED();

    new_comp = e_cal_component_new();
    e_cal_component_set_icalcomponent(new_comp, icalcomponent_new_clone(icalcomp));

    current = icaltime_from_timet(time(NULL), 0);
    e_cal_component_set_created(new_comp, &current);
    e_cal_component_set_last_modified(new_comp, &current);
    if (e_cal_component_has_attachments(new_comp))
    {
        fetch_attachments(cb, new_comp);
    }

    new_id = e_cal_component_get_id(new_comp);
    cache_comp = e_cal_backend_store_get_component(priv->store, new_id->uid, new_id->rid);

    /* process received components */
    switch (method)
    {
    case ICAL_METHOD_PUBLISH:
    case ICAL_METHOD_REQUEST:
    case ICAL_METHOD_REPLY:
        if (cache_comp)
        {
            char *old_object = e_cal_component_get_as_string(cache_comp);
            char *new_object = e_cal_component_get_as_string(new_comp);
            e_cal_backend_store_put_component(priv->store, new_comp);
            e_cal_backend_notify_object_modified(E_CAL_BACKEND(backend), old_object, new_object);
            g_free(old_object);
            g_free(new_object);
        }
        else
        {
            char *new_object = e_cal_component_get_as_string(new_comp);
            e_cal_backend_store_put_component(priv->store, new_comp);
            e_cal_backend_notify_object_created(E_CAL_BACKEND(backend), new_object);
            g_free(new_object);
        }
        break;

    case ICAL_METHOD_CANCEL:
        if (cache_comp)
        {
            char *old_object = e_cal_component_get_as_string(cache_comp);
            e_cal_backend_store_remove_component(priv->store, new_id->uid, new_id->rid);
            e_cal_backend_notify_object_removed(E_CAL_BACKEND(backend), new_id, old_object, NULL);
            g_free(old_object);
        }
        else
        {
            status = ObjectNotFound;

        }
        break;

    default:
        status = UnsupportedMethod;
        break;
    }

    e_cal_component_free_id(new_id);
    g_object_unref(new_comp);
    g_object_unref(cache_comp);

    return status;
}

/** Import a set of events/tasks in one go.
 * @todo needs testing
 */
static void e_cal_backend_3e_receive_objects(ECalBackendSync *backend,
                                             EDataCal *cal,
                                             GCancellable *cancellable,
                                             const gchar *calobj,
                                             GError **err)
{
    EDataCalCallStatus status = Success;
    icalcomponent *vtop = NULL;

    BACKEND_METHOD_CHECKED_NORETVAL("calobj=%s", calobj);

    g_return_if_fail(cal != NULL);
    g_return_if_fail(calobj != NULL);

    /* parse calobj */
    vtop = icalparser_parse_string(calobj);
    if (icalcomponent_isa(vtop) == ICAL_VCALENDAR_COMPONENT)
    {
        icalproperty_method method;
        icalcomponent *vevent;

        // store unknown timezones from the iTip
        for (vevent = icalcomponent_get_first_component(vtop, ICAL_VTIMEZONE_COMPONENT);
             vevent; vevent = icalcomponent_get_next_component(vtop, ICAL_VTIMEZONE_COMPONENT))
        {
            icaltimezone *zone = icaltimezone_new();
            icaltimezone_set_component(zone, vevent);
            const char *tzid = icaltimezone_get_tzid(zone);
            if (!e_cal_backend_store_get_timezone(priv->store, tzid))
            {
                e_cal_backend_store_put_timezone(priv->store, zone);
            }
            icaltimezone_free(zone, TRUE);
        }

        // events
        for (vevent = icalcomponent_get_first_component(vtop, ICAL_VEVENT_COMPONENT);
             vevent; vevent = icalcomponent_get_next_component(vtop, ICAL_VEVENT_COMPONENT))
        {
            if (icalcomponent_get_first_property(vevent, ICAL_METHOD_PROPERTY))
            {
                method = icalcomponent_get_method(vevent);
            }
            else
            {
                method = icalcomponent_get_method(vtop);
            }

            status = e_cal_backend_3e_receive_object(backend, vevent, method);
            if (status != Success)
            {
                break;
            }
        }
    }
    else if (icalcomponent_isa(vtop) == ICAL_VEVENT_COMPONENT)
    {
        status = e_cal_backend_3e_receive_object(backend, vtop, icalcomponent_get_method(vtop));
    }
    else
    {
        status = InvalidObject;
    }

    e_cal_backend_3e_do_immediate_sync(cb);

    icalcomponent_free(vtop);

    return; // status;
}

/** Send a set of meetings in one go, which means, for backends that
 * do support it, sending information about the meeting to all attendees.
 */
static void e_cal_backend_3e_send_objects(ECalBackendSync *backend,
                                          EDataCal *cal,
                                          GCancellable *cancellable,
                                          const gchar *calobj,
                                          GSList **users,
                                          gchar **modified_calobj,
                                          GError **err)
{
    icalcomponent *comp;
    GSList *recipients = NULL, *iter;

    BACKEND_METHOD_CHECKED_NORETVAL("calobj=%s", calobj);

    g_return_if_fail(cal != NULL);
    g_return_if_fail(calobj != NULL);

    /* parse calobj */
    comp = icalparser_parse_string(calobj);
    if (comp == NULL)
    {
        return; // InvalidObject;
    }

    icalcomponent_collect_recipients(comp, priv->username, &recipients);
    icalcomponent_free(comp);

    e_cal_backend_3e_do_immediate_sync(cb);

    /* this tells evolution that it should not send emails (iMIPs) by itself */
    for (iter = recipients; iter; iter = iter->next)
    {
        //BUG: there is a bug in itip-utils.c:509, name requires MAILTO: for now
        *users = g_slist_append(*users, g_strdup_printf("MAILTO:%s", (char *)iter->data));
    }

    g_slist_foreach(recipients, (GFunc)g_free, NULL);
    g_slist_free(recipients);

    *modified_calobj = g_strdup(calobj);

    return; // Success;
}

struct _removals_data
{
    ECalBackend3e *cb;
    GList *deletes;
    EXmlHash *ehash;
};

/** @todo this will not handle detached recurring instances very vell
 */
static void calculate_removals(const char *key, const char *value, gpointer data)
{
    struct _removals_data *r = data;
    ECalBackend3e *cb = r->cb;

    if (!e_cal_backend_store_get_component(cb->priv->store, key, NULL))
    {
        ECalComponent *comp;

        comp = e_cal_component_new();
        e_cal_component_set_new_vtype(comp, E_CAL_COMPONENT_EVENT);
        e_cal_component_set_uid(comp, key);

        r->deletes = g_list_prepend(r->deletes, e_cal_component_get_as_string(comp));

        e_xmlhash_remove(r->ehash, key);
        g_object_unref(comp);
    }
}

static ECalBackendSyncClass *parent_class = NULL;

G_DEFINE_TYPE(ECalBackend3e, e_cal_backend_3e, E_TYPE_CAL_BACKEND_SYNC)

static void e_cal_backend_3e_init(ECalBackend3e *cb)
{
    cb->priv = E_CAL_BACKEND_3E_GET_PRIVATE (cb);

    g_static_rw_lock_init(&cb->priv->cache_lock);
    g_static_rec_mutex_init(&cb->priv->conn_mutex);
    cb->priv->sync_mutex = g_mutex_new();

    e_cal_backend_sync_set_lock(E_CAL_BACKEND_SYNC(cb), TRUE);
}

static void e_cal_backend_3e_finalize(GObject *backend)
{
    BACKEND_METHOD_CHECKED_NORETVAL();

    e_cal_backend_3e_periodic_sync_stop(cb);
    e_cal_backend_3e_free_connection(cb);
    e_cal_backend_3e_attachment_store_free(cb);

    g_static_rw_lock_free(&priv->cache_lock);
    g_static_rec_mutex_free(&priv->conn_mutex);
    g_mutex_free(priv->sync_mutex);

    /* calinfo */
    g_free(priv->calname);
    priv->calname = NULL;
    g_free(priv->owner);
    priv->owner = NULL;
    g_free(priv->calspec);
    priv->calspec = NULL;
    g_free(priv->perm);
    priv->perm = NULL;

    /* backend data */
    if (priv->store)
    {
        g_object_unref(priv->store);
        priv->store = NULL;
    }
    g_free(priv->cache_path);

    if (priv->default_zone)
    {
        icaltimezone_free(priv->default_zone, TRUE);
        priv->default_zone = NULL;
    }

//    g_free(priv);
//    cb->priv = NULL;

    G_OBJECT_CLASS (parent_class)->finalize(backend);
}

static void e_cal_backend_3e_class_init(ECalBackend3eClass *class )
{
    GObjectClass                                     *object_class;
    ECalBackendClass                                 *backend_class;
    ECalBackendSyncClass                             *sync_class;

    object_class = (GObjectClass *)class;
    backend_class = (ECalBackendClass *)class;
    sync_class = (ECalBackendSyncClass *)class;

    parent_class = (ECalBackendSyncClass *) g_type_class_peek_parent (class);
    g_type_class_add_private (class, sizeof (ECalBackend3ePrivate));

    object_class->finalize = e_cal_backend_3e_finalize;

// Remade methods
    sync_class->open_sync = e_cal_backend_3e_open;
    sync_class->authenticate_user_sync = e_cal_backend_3e_authenticate_user;
    sync_class->refresh_sync = e_cal_backend_3e_refresh;
    sync_class->remove_sync = e_cal_backend_3e_remove;

    sync_class->create_object_sync = e_cal_backend_3e_create_object;
    sync_class->modify_object_sync = e_cal_backend_3e_modify_object;
    sync_class->remove_object_sync = e_cal_backend_3e_remove_object;


    sync_class->receive_objects_sync = e_cal_backend_3e_receive_objects;
    sync_class->send_objects_sync = e_cal_backend_3e_send_objects;
    sync_class->get_object_sync = e_cal_backend_3e_get_object;
    sync_class->get_object_list_sync = e_cal_backend_3e_get_object_list;
    sync_class->add_timezone_sync = e_cal_backend_3e_add_timezone;
    sync_class->get_free_busy_sync = e_cal_backend_3e_get_free_busy;

    backend_class->start_view = e_cal_backend_3e_start_view;

    backend_class->internal_get_timezone = e_cal_backend_3e_internal_get_timezone;
}
