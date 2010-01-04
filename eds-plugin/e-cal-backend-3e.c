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

// {{{ Backend Method GLUE Macros

#include "e-cal-backend-3e-priv.h"
#include <libedataserver/e-xml-hash-utils.h>
#include <gio/gio.h>

//#define T(fmt, args...) g_print("TRACE[%p]: %s(backend=%p " fmt ")\n", g_thread_self(), G_STRFUNC, backend, ## args)
#define T(fmt, args...)

#define BACKEND_METHOD_CHECKED_RETVAL(val, args...) \
    GError * local_err = NULL; \
    ECalBackend3e *cb = (ECalBackend3e *)backend; \
    ECalBackend3ePrivate *priv; \
    g_return_val_if_fail(E_IS_CAL_BACKEND_3E(cb), val); \
    priv = cb->priv; \
    T(args)

#define BACKEND_METHOD_CHECKED(args...) \
    BACKEND_METHOD_CHECKED_RETVAL(GNOME_Evolution_Calendar_OtherError, ## args)

#define BACKEND_METHOD_CHECKED_NORETVAL(args...) \
    GError * local_err = NULL; \
    ECalBackend3e *cb = (ECalBackend3e *)backend; \
    ECalBackend3ePrivate *priv; \
    g_return_if_fail(E_IS_CAL_BACKEND_3E(cb)); \
    priv = cb->priv; \
    T(args)

/* cache API wrappers */

#define e_cal_backend_cache_get_components(cache) e_cal_backend_3e_cache_get_components(cb, cache)
#define e_cal_backend_cache_get_components_by_uid(cache, uid) e_cal_backend_3e_cache_get_components_by_uid(cb, cache, uid)
#define e_cal_backend_cache_get_component(cache, uid, rid) e_cal_backend_3e_cache_get_component(cb, cache, uid, rid)
#define e_cal_backend_cache_put_component(cache, c) e_cal_backend_3e_cache_put_component(cb, cache, c)
#define e_cal_backend_cache_remove_component(cache, uid, rid) e_cal_backend_3e_cache_remove_component(cb, cache, uid, rid)
#define e_cal_backend_cache_get_timezone(cache, tzid) e_cal_backend_3e_cache_get_timezone(cb, cache, tzid)
#define e_cal_backend_cache_put_timezone(cache, tzobj) e_cal_backend_3e_cache_put_timezone(cb, cache, tzobj)

// }}}

/** @addtogroup eds_back */
/** @{ */

// {{{ Calendar manipulation

/** Open the calendar.
 */
static ECalBackendSyncStatus e_cal_backend_3e_open(ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists, const char *username, const char *password)
{
    BACKEND_METHOD_CHECKED("only_if_exists=%d, username=%s, password=%s", only_if_exists, username, password);

    if (!priv->is_loaded)
    {
        /* load calendar info */
        if (!e_cal_backend_3e_calendar_info_load(cb))
        {
            e_cal_backend_notify_error(E_CAL_BACKEND(cb), "Trying to open non-3E source using 3E backend.");
            return GNOME_Evolution_Calendar_OtherError;
        }

        /* setup connection info */
        e_cal_backend_3e_setup_connection(cb, username, password);

        /* open/create cache */
        priv->cache = e_cal_backend_cache_new(e_cal_backend_get_uri(E_CAL_BACKEND(cb)), E_CAL_SOURCE_TYPE_EVENT);
        if (priv->cache == NULL)
        {
            e_cal_backend_notify_error(E_CAL_BACKEND(cb), "Failed to open local calendar cache.");
            return GNOME_Evolution_Calendar_OtherError;
        }

        e_cal_backend_3e_messages_queue_load(cb);
        e_cal_backend_3e_attachment_store_load(cb);

        priv->is_loaded = TRUE;
    }

    /* enable sync */
    if (e_cal_backend_3e_calendar_is_online(cb))
    {
        e_cal_backend_3e_periodic_sync_enable(cb);
    }

    return GNOME_Evolution_Calendar_Success;
}

/** Remove the calendar.
 */
static ECalBackendSyncStatus e_cal_backend_3e_remove(ECalBackendSync *backend, EDataCal *cal)
{
    BACKEND_METHOD_CHECKED();

    if (priv->is_loaded)
    {
        priv->is_loaded = FALSE;
        e_cal_backend_3e_periodic_sync_stop(cb);
        e_file_cache_remove(E_FILE_CACHE(priv->cache));
        g_object_unref(priv->cache);
        priv->cache = NULL;
    }

    return GNOME_Evolution_Calendar_Success;
}

// }}}
// {{{ Calendar metadata extraction

/** Returns the capabilities provided by the backend, like whether it supports
 * recurrences or not, for instance
 * @todo figure out what caps are needed
 */
static ECalBackendSyncStatus e_cal_backend_3e_get_static_capabilities(ECalBackendSync *backend, EDataCal *cal, char * *capabilities)
{
    BACKEND_METHOD_CHECKED();
    g_return_val_if_fail(capabilities != NULL, GNOME_Evolution_Calendar_OtherError);

    *capabilities = g_strdup(
        CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS ","
        CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
        CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
        CAL_STATIC_CAPABILITY_NO_CONV_TO_RECUR ","
        CAL_STATIC_CAPABILITY_NO_SEND_IMIP
        );

    return GNOME_Evolution_Calendar_Success;
}

/** Returns whether the calendar is read only or not.
 *
 * The problem with this method is that it is not called as often as we would like to
 * (not by every object manipulation). Therefore, when evolution is running and permission
 * is changed on the server, we cannot reflect this situation by this method.
 */
static ECalBackendSyncStatus e_cal_backend_3e_is_read_only(ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
    BACKEND_METHOD_CHECKED();
    g_return_val_if_fail(read_only != NULL, GNOME_Evolution_Calendar_OtherError);

    *read_only = !e_cal_backend_3e_calendar_has_perm(cb, "write");

    return GNOME_Evolution_Calendar_Success;
}

/** Returns the email address of the owner of the calendar.
 *
 * If owner differs from username, ORGANIZER;SENT-BY=xxx will be set by the
 * event-page.c:event_page_fill_component() code.
 */
static ECalBackendSyncStatus e_cal_backend_3e_get_cal_address(ECalBackendSync *backend, EDataCal *cal, char * *address)
{
    BACKEND_METHOD_CHECKED();
    g_return_val_if_fail(address != NULL, GNOME_Evolution_Calendar_OtherError);

    *address = g_strdup(priv->owner);

    return GNOME_Evolution_Calendar_Success;
}

/** Returns the email address to be used for alarms.
 */
static ECalBackendSyncStatus e_cal_backend_3e_get_alarm_email_address(ECalBackendSync *backend, EDataCal *cal, char * *address)
{
    BACKEND_METHOD_CHECKED();
    g_return_val_if_fail(address != NULL, GNOME_Evolution_Calendar_OtherError);

    *address = g_strdup(priv->username);

    return GNOME_Evolution_Calendar_Success;
}

/** Returns specific LDAP attributes.
 * @todo huh?
 */
static ECalBackendSyncStatus e_cal_backend_3e_get_ldap_attribute(ECalBackendSync *backend, EDataCal *cal, char * *attribute)
{
    BACKEND_METHOD_CHECKED();

    *attribute = NULL;

    return GNOME_Evolution_Calendar_UnsupportedMethod;
}

/** Returns TRUE if the the passed-in backend is already in a loaded state,
 * otherwise FALSE
 *
 * @todo priv->is_loaded may need to be protected
 */
static gboolean e_cal_backend_3e_is_loaded(ECalBackend *backend)
{
    BACKEND_METHOD_CHECKED_RETVAL(FALSE);

    return priv->is_loaded;
}

// }}}
// {{{ Mode switching (online/offline)

/** Returns the current online/offline mode for the backend.
 */
static CalMode e_cal_backend_3e_get_mode(ECalBackend *backend)
{
    BACKEND_METHOD_CHECKED_RETVAL(CAL_MODE_INVALID);

    return priv->mode;
}

/** Sets the current online/offline mode.
 * @todo handle sync on transitions between online/offline mode
 */
static void e_cal_backend_3e_set_mode(ECalBackend *backend, CalMode mode)
{
    BACKEND_METHOD_CHECKED_NORETVAL("mode=%d", mode);

    if (priv->mode != mode)
    {
        priv->mode = mode;

        if (mode == CAL_MODE_REMOTE)
        {
            /* mode changed to remote */
            if (priv->is_loaded)
            {
                e_cal_backend_3e_periodic_sync_enable(cb);
            }
        }
        else if (mode == CAL_MODE_LOCAL)
        {
            /* mode changed to local */
            e_cal_backend_3e_periodic_sync_disable(cb);
        }
        else
        {
            /* some bug */
            e_cal_backend_notify_mode(backend, GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED, cal_mode_to_corba(priv->mode));
            return;
        }
    }

    e_cal_backend_notify_mode(backend, GNOME_Evolution_Calendar_CalListener_MODE_SET, cal_mode_to_corba(priv->mode));
}

// }}}
// {{{ Objects manipulation

/** Returns an empty object with the default values used for the backend called
 * when creating new object, for example.
 */
static ECalBackendSyncStatus e_cal_backend_3e_get_default_object(ECalBackendSync *backend, EDataCal *cal, char * *object)
{
    icalcomponent *icalcomp;
    icalcomponent_kind kind;

    BACKEND_METHOD_CHECKED("object=%s", *object);
    g_return_val_if_fail(object != NULL, GNOME_Evolution_Calendar_OtherError);

    kind = e_cal_backend_get_kind(E_CAL_BACKEND(backend));
    icalcomp = e_cal_util_new_component(kind);
    *object = g_strdup(icalcomponent_as_ical_string(icalcomp));
    icalcomponent_free(icalcomp);

    return GNOME_Evolution_Calendar_Success;
}

/** Returns a list of events/tasks given a set of conditions.
 */
static ECalBackendSyncStatus e_cal_backend_3e_get_object_list(ECalBackendSync *backend, EDataCal *cal, const char *sexp, GList * *objects)
{
    GList *all_objects;
    GList *iter;
    ECalBackendSExp *cbsexp;

    BACKEND_METHOD_CHECKED("sexp=%s", sexp);
    g_return_val_if_fail(objects != NULL, GNOME_Evolution_Calendar_OtherError);
    g_return_val_if_fail(sexp != NULL, GNOME_Evolution_Calendar_OtherError);

    cbsexp = e_cal_backend_sexp_new(sexp);
    if (cbsexp == NULL)
    {
        return GNOME_Evolution_Calendar_InvalidQuery;
    }

    all_objects = e_cal_backend_cache_get_components(priv->cache);
    for (iter = all_objects; iter != NULL; iter = iter->next)
    {
        ECalComponent *comp = E_CAL_COMPONENT(iter->data);

        if (e_cal_backend_sexp_match_comp(cbsexp, comp, E_CAL_BACKEND(backend)))
        {
            *objects = g_list_append(*objects, e_cal_component_get_as_string(comp));
        }

        g_object_unref(comp);
    }

    g_list_free(all_objects);
    g_object_unref(cbsexp);

    return GNOME_Evolution_Calendar_Success;
}

/** Starts a live query on the backend.
 */
static void e_cal_backend_3e_start_query(ECalBackend *backend, EDataCalView *query)
{
    ECalBackendSyncStatus status;
    GList *objects = NULL;

    BACKEND_METHOD_CHECKED_NORETVAL();

    status = e_cal_backend_3e_get_object_list(E_CAL_BACKEND_SYNC(backend), NULL, e_data_cal_view_get_text(query), &objects);
    if (status == GNOME_Evolution_Calendar_Success)
    {
        e_data_cal_view_notify_objects_added(query, objects);
        g_list_foreach(objects, (GFunc)g_free, NULL);
        g_list_free(objects);
    }

    e_data_cal_view_notify_done(query, status);
}

/** Returns a list of events/tasks given a set of conditions.
 */
static ECalBackendSyncStatus e_cal_backend_3e_get_object(ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, char * *object)
{
    BACKEND_METHOD_CHECKED("uid=%s  rid=%s", uid, rid);
    g_return_val_if_fail(object != NULL, GNOME_Evolution_Calendar_OtherError);
    g_return_val_if_fail(uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

    if (rid && *rid)
    {
        /* get single detached instance */
        ECalComponent *dinst = e_cal_backend_cache_get_component(priv->cache, uid, rid);
        if (!dinst)
        {
            return GNOME_Evolution_Calendar_ObjectNotFound;
        }

        *object = e_cal_component_get_as_string(dinst);
        g_object_unref(dinst);
    }
    else
    {
        /* get master object with detached instances */
        ECalComponent *master = e_cal_backend_cache_get_component(priv->cache, uid, rid);
        if (!master)
        {
            return GNOME_Evolution_Calendar_ObjectNotFound;
        }

        if (!e_cal_component_has_recurrences(master))
        {
            /* normal non-recurring object */
            *object = e_cal_component_get_as_string(master);
        }
        else
        {
            /* recurring object, return it in VCALENDAR with detached instances */
            icalcomponent *icalcomp = e_cal_util_new_top_level();
            GSList *dinst_list;
            GSList *iter;

            dinst_list = e_cal_backend_cache_get_components_by_uid(priv->cache, uid);
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

    return GNOME_Evolution_Calendar_Success;
}

/** Creates a new event/task in the calendar.
 */
static ECalBackendSyncStatus e_cal_backend_3e_create_object(ECalBackendSync *backend, EDataCal *cal, char * *calobj, char * *uid)
{
    ECalComponent *comp;

    BACKEND_METHOD_CHECKED("calobj=%s, uid=%s", *calobj, *uid);
    g_return_val_if_fail(calobj != NULL && *calobj != NULL, GNOME_Evolution_Calendar_OtherError);

    if (!e_cal_backend_3e_calendar_has_perm(cb, "write"))
    {
        return GNOME_Evolution_Calendar_PermissionDenied;
    }

    comp = e_cal_component_new_from_string(*calobj);
    if (comp == NULL)
    {
        return GNOME_Evolution_Calendar_InvalidObject;
    }

    e_cal_component_set_x_property(comp, "X-EVOLUTION-STATUS", "outofsync");
    *calobj = e_cal_component_get_as_string(comp);

    if (!e_cal_backend_cache_put_component(priv->cache, comp))
    {
        g_object_unref(comp);
        return GNOME_Evolution_Calendar_OtherError;
    }

    e_cal_backend_3e_do_immediate_sync(cb);

    g_object_unref(comp);
    return GNOME_Evolution_Calendar_Success;
}

/** Modifies an existing event/task.
 *
 * @todo be smarter about 'modify all instances' sitaution
 * @todo add error checking for cache operations
 */
static ECalBackendSyncStatus e_cal_backend_3e_modify_object(ECalBackendSync *backend, EDataCal *cal, const char *calobj, CalObjModType mod, char * *old_object, char * *new_object)
{
    ECalComponent *cache_comp;
    ECalComponent *new_comp;
    ECalComponentId *new_id;

    BACKEND_METHOD_CHECKED("calobj=%s mod=%d", calobj, mod);
    g_return_val_if_fail(calobj != NULL, GNOME_Evolution_Calendar_OtherError);
    g_return_val_if_fail(old_object != NULL, GNOME_Evolution_Calendar_OtherError);
    g_return_val_if_fail(new_object != NULL, GNOME_Evolution_Calendar_OtherError);

    if (!e_cal_backend_3e_calendar_has_perm(cb, "write"))
    {
        return GNOME_Evolution_Calendar_PermissionDenied;
    }

    /* parse new object */
    new_comp = e_cal_component_new_from_string(calobj);
    if (new_comp == NULL)
    {
        return GNOME_Evolution_Calendar_InvalidObject;
    }

    new_id = e_cal_component_get_id(new_comp);

    if ((e_cal_component_has_recurrences(new_comp) || e_cal_component_is_instance(new_comp))
        && mod == CALOBJ_MOD_ALL)
    {
        /* remove detached instances if evolution requested to modify all recurrences and
           update master object */
        GSList *comp_list = e_cal_backend_cache_get_components_by_uid(priv->cache, new_id->uid);
        GSList *iter;

        for (iter = comp_list; iter; iter = iter->next)
        {
            ECalComponent *inst = E_CAL_COMPONENT(iter->data);
            char *inst_str = e_cal_component_get_as_string(inst);
            ECalComponentId *inst_id = e_cal_component_get_id(inst);

            if (!e_cal_component_has_recurrences(inst) && e_cal_component_is_instance(inst))
            {
                e_cal_backend_cache_remove_component(priv->cache, inst_id->uid, inst_id->rid);
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
                e_cal_backend_cache_put_component(priv->cache, inst);

                e_cal_component_set_x_property(inst, "X-EVOLUTION-STATUS", "outofsync");
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
        cache_comp = e_cal_backend_cache_get_component(priv->cache, new_id->uid, new_id->rid);
        if (cache_comp)
        {
            *old_object = e_cal_component_get_as_string(cache_comp);
            g_object_unref(cache_comp);
        }

        e_cal_component_set_x_property(new_comp, "X-EVOLUTION-STATUS", "outofsync");
        *new_object = e_cal_component_get_as_string(new_comp);
        e_cal_backend_cache_put_component(priv->cache, new_comp);
    }

    e_cal_backend_3e_do_immediate_sync(cb);

    g_object_unref(new_comp);
    e_cal_component_free_id(new_id);

    return GNOME_Evolution_Calendar_Success;
}

/** Removes an object from the calendar.
 */
static ECalBackendSyncStatus e_cal_backend_3e_remove_object(ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, CalObjModType mod, char * *old_object, char * *object)
{
    BACKEND_METHOD_CHECKED("uid=%s rid=%s mod=%d", uid, rid, mod);
    g_return_val_if_fail(uid != NULL, GNOME_Evolution_Calendar_OtherError);
    g_return_val_if_fail(old_object != NULL, GNOME_Evolution_Calendar_OtherError);
    g_return_val_if_fail(object != NULL, GNOME_Evolution_Calendar_OtherError);

    if (!e_cal_backend_3e_calendar_has_perm(cb, "write"))
    {
        return GNOME_Evolution_Calendar_PermissionDenied;
    }

    if (mod == CALOBJ_MOD_THIS)
    {
        if (rid && *rid)
        {
            /* remove detached or virtual (EXDATE) instance */
            ECalComponent *master;
            ECalComponent *dinst;
            char *old_master, *new_master;

            master = e_cal_backend_cache_get_component(priv->cache, uid, NULL);
            if (master == NULL)
            {
                return GNOME_Evolution_Calendar_ObjectNotFound;
            }

            /* was a detached instance? remove it and set old_object */
            dinst = e_cal_backend_cache_get_component(priv->cache, uid, rid);
            if (dinst)
            {
                e_cal_backend_cache_remove_component(priv->cache, uid, rid);
                *old_object = e_cal_component_get_as_string(dinst);
                g_object_unref(dinst);
            }

            /* add EXDATE to the master and notify clients */
            old_master = e_cal_component_get_as_string(master);
            e_cal_util_remove_instances(e_cal_component_get_icalcomponent(master), icaltime_from_string(rid), CALOBJ_MOD_THIS);
            new_master = e_cal_component_get_as_string(master);
            e_cal_backend_cache_put_component(priv->cache, master);
            e_cal_backend_notify_object_modified(E_CAL_BACKEND(backend), old_master, new_master);
            g_object_unref(master);
            g_free(old_master);
            g_free(new_master);
        }
        else
        {
            /* remove one object (this will be non-recurring) */
            ECalComponent *cache_comp;

            cache_comp = e_cal_backend_cache_get_component(priv->cache, uid, rid);
            if (cache_comp == NULL)
            {
                return GNOME_Evolution_Calendar_ObjectNotFound;
            }

            *old_object = e_cal_component_get_as_string(cache_comp);
            e_cal_backend_cache_remove_component(priv->cache, uid, rid);
            g_object_unref(cache_comp);
        }
    }
    else if (mod == CALOBJ_MOD_ALL)
    {
        /* remove all instances of recurring object */
        ECalComponent *cache_comp;
        GSList *comp_list;
        GSList *iter;

        /* this will be master object */
        cache_comp = e_cal_backend_cache_get_component(priv->cache, uid, rid);
        if (cache_comp == NULL)
        {
            return GNOME_Evolution_Calendar_ObjectNotFound;
        }

        comp_list = e_cal_backend_cache_get_components_by_uid(priv->cache, uid);
        e_cal_backend_cache_remove_component(priv->cache, uid, rid);

        for (iter = comp_list; iter; iter = iter->next)
        {
            ECalComponent *comp = E_CAL_COMPONENT(iter->data);
            ECalComponentId *id = e_cal_component_get_id(comp);

            if (e_cal_backend_cache_remove_component(priv->cache, id->uid, id->rid))
            {
                e_cal_backend_notify_object_removed(E_CAL_BACKEND(backend), id, e_cal_component_get_as_string(comp), NULL);
            }

            e_cal_component_free_id(id);
            g_object_unref(comp);
        }
        g_slist_free(comp_list);

        *old_object = e_cal_component_get_as_string(cache_comp);
        g_object_unref(cache_comp);
    }
    else
    {
        return GNOME_Evolution_Calendar_UnsupportedMethod;
    }

    e_cal_backend_3e_do_immediate_sync(cb);

    return GNOME_Evolution_Calendar_Success;
}

// }}}
// {{{ Timezone manipulation

/** Returns timezone objects for a given TZID.
 * @todo free timezone data somehow?
 */
static ECalBackendSyncStatus e_cal_backend_3e_get_timezone(ECalBackendSync *backend, EDataCal *cal, const char *tzid, char * *object)
{
    icaltimezone *zone;
    icalcomponent *icalcomp;

    BACKEND_METHOD_CHECKED("tzid=%s", tzid);
    g_return_val_if_fail(tzid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);
    g_return_val_if_fail(object != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

    zone = (icaltimezone *)e_cal_backend_cache_get_timezone(priv->cache, tzid);
    if (zone == NULL)
    {
        zone = icaltimezone_get_builtin_timezone_from_tzid(tzid);
        if (zone == NULL)
        {
            return GNOME_Evolution_Calendar_ObjectNotFound;
        }

        /* if zone was not found in cache but was found in builtin table, add it to
           cache, and it will be synced to the 3E server */
        e_cal_backend_cache_put_timezone(priv->cache, zone);

        e_cal_backend_3e_do_immediate_sync(cb);
    }

    icalcomp = icaltimezone_get_component(zone);
    if (!icalcomp)
    {
        return GNOME_Evolution_Calendar_InvalidObject;
    }

    *object = g_strdup(icalcomponent_as_ical_string(icalcomp));

    return GNOME_Evolution_Calendar_Success;
}

/** Adds a timezone to the backend.
 */
static ECalBackendSyncStatus e_cal_backend_3e_add_timezone(ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
    icalcomponent *icalcomp;
    icaltimezone *zone;

    BACKEND_METHOD_CHECKED("tzobj=%s", tzobj);
    g_return_val_if_fail(tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

    icalcomp = icalparser_parse_string(tzobj);
    if (icalcomp == NULL)
    {
        return GNOME_Evolution_Calendar_InvalidObject;
    }

    if (icalcomponent_isa(icalcomp) != ICAL_VTIMEZONE_COMPONENT)
    {
        icalcomponent_free(icalcomp);
        return GNOME_Evolution_Calendar_InvalidObject;
    }

    zone = icaltimezone_new();
    icaltimezone_set_component(zone, icalcomp);
    const char *tzid = icaltimezone_get_tzid(zone);
    if (!e_cal_backend_cache_get_timezone(priv->cache, tzid))
    {
        e_cal_backend_cache_put_timezone(priv->cache, zone);
    }
    icaltimezone_free(zone, TRUE);

    e_cal_backend_3e_do_immediate_sync(cb);

    return GNOME_Evolution_Calendar_Success;
}

/** Sets the timezone to be used as the default. It is called before opening
 * connection, before creating cache.
 */
static ECalBackendSyncStatus e_cal_backend_3e_set_default_zone(ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
    icalcomponent *icalcomp;
    icaltimezone *zone;

    BACKEND_METHOD_CHECKED("tzobj=%s", tzobj);
    g_return_val_if_fail(tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

    icalcomp = icalparser_parse_string(tzobj);
    if (icalcomp == NULL)
    {
        return GNOME_Evolution_Calendar_InvalidObject;
    }

    if (icalcomponent_isa(icalcomp) != ICAL_VTIMEZONE_COMPONENT)
    {
        icalcomponent_free(icalcomp);
        return GNOME_Evolution_Calendar_InvalidObject;
    }

    zone = icaltimezone_new();
    icaltimezone_set_component(zone, icalcomp);

    if (priv->default_zone)
    {
        icaltimezone_free(priv->default_zone, TRUE);
    }
    priv->default_zone = zone;

    return GNOME_Evolution_Calendar_Success;
}

/** Returns the default timezone.
 */
static icaltimezone *e_cal_backend_3e_internal_get_default_timezone(ECalBackend *backend)
{
    BACKEND_METHOD_CHECKED_RETVAL(icaltimezone_get_utc_timezone());

    return icaltimezone_get_utc_timezone();
}

/** Returns a given timezone
 */
static icaltimezone *e_cal_backend_3e_internal_get_timezone(ECalBackend *backend, const char *tzid)
{
    icaltimezone *zone;

    BACKEND_METHOD_CHECKED_RETVAL(NULL, "tzid=%s", tzid);

    zone = icaltimezone_get_builtin_timezone_from_tzid(tzid);
    if (!zone)
    {
        return icaltimezone_get_utc_timezone();
    }

    return zone;
}

// }}}
// {{{ Free/busy

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
static ECalBackendSyncStatus e_cal_backend_3e_get_free_busy(ECalBackendSync *backend, EDataCal *cal, GList *users, time_t start, time_t end, GList * *freebusy)
{
    char *zone;
    GList *iter;

    BACKEND_METHOD_CHECKED("users=%p start=%d end=%d", users, (int)start, (int)end);
    g_return_val_if_fail(users != NULL, GNOME_Evolution_Calendar_OtherError);
    g_return_val_if_fail(start != -1 && end != -1, GNOME_Evolution_Calendar_InvalidRange);
    g_return_val_if_fail(start <= end, GNOME_Evolution_Calendar_InvalidRange);
    g_return_val_if_fail(freebusy != NULL, GNOME_Evolution_Calendar_OtherError);

    if (!e_cal_backend_3e_calendar_is_online(cb))
    {
        return GNOME_Evolution_Calendar_RepositoryOffline;
    }

    if (!e_cal_backend_3e_open_connection(cb, &local_err))
    {
        e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb), "Can't connect to the server to get freebusy.", local_err);
        g_error_free(local_err);
        return GNOME_Evolution_Calendar_OtherError;
    }

    zone = icalcomponent_as_ical_string(icaltimezone_get_component(priv->default_zone));

    for (iter = users; iter; iter = iter->next)
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
            *freebusy = g_list_append(*freebusy, vfb);
        }

        g_free(iso_start);
        g_free(iso_end);
    }

    e_cal_backend_3e_close_connection(cb);

    return GNOME_Evolution_Calendar_Success;
}

// }}}
// {{{ Receiving iTIPs

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

static ECalBackendSyncStatus e_cal_backend_3e_receive_object(ECalBackendSync *backend, icalcomponent *icalcomp, icalproperty_method method)
{
    ECalBackendSyncStatus status = GNOME_Evolution_Calendar_Success;
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
    cache_comp = e_cal_backend_cache_get_component(priv->cache, new_id->uid, new_id->rid);

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
            e_cal_backend_cache_put_component(priv->cache, new_comp);
            e_cal_backend_notify_object_modified(E_CAL_BACKEND(backend), old_object, new_object);
            g_free(old_object);
            g_free(new_object);
        }
        else
        {
            char *new_object = e_cal_component_get_as_string(new_comp);
            e_cal_backend_cache_put_component(priv->cache, new_comp);
            e_cal_backend_notify_object_created(E_CAL_BACKEND(backend), new_object);
            g_free(new_object);
        }
        break;

    case ICAL_METHOD_CANCEL:
        if (cache_comp)
        {
            char *old_object = e_cal_component_get_as_string(cache_comp);
            e_cal_backend_cache_remove_component(priv->cache, new_id->uid, new_id->rid);
            e_cal_backend_notify_object_removed(E_CAL_BACKEND(backend), new_id, old_object, NULL);
            g_free(old_object);
        }
        else
        {
            status = GNOME_Evolution_Calendar_ObjectNotFound;
        }
        break;

    default:
        status = GNOME_Evolution_Calendar_UnsupportedMethod;
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
static ECalBackendSyncStatus e_cal_backend_3e_receive_objects(ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
    icalcomponent *vtop = NULL;
    ECalBackendSyncStatus status = GNOME_Evolution_Calendar_Success;

    BACKEND_METHOD_CHECKED("calobj=%s", calobj);
    g_return_val_if_fail(cal != NULL, GNOME_Evolution_Calendar_OtherError);
    g_return_val_if_fail(calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

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
            if (!e_cal_backend_cache_get_timezone(priv->cache, tzid))
            {
                e_cal_backend_cache_put_timezone(priv->cache, zone);
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
            if (status != GNOME_Evolution_Calendar_Success)
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
        status = GNOME_Evolution_Calendar_InvalidObject;
    }

    e_cal_backend_3e_do_immediate_sync(cb);

    icalcomponent_free(vtop);

    return status;
}

// }}}
// {{{ Sending iTIPs

/** Send a set of meetings in one go, which means, for backends that do support
 * it, sending information about the meeting to all attendees.
 */
static ECalBackendSyncStatus e_cal_backend_3e_send_objects(ECalBackendSync *backend, EDataCal *cal, const char *calobj, GList * *users, char * *modified_calobj)
{
    icalcomponent *comp;
    GSList *recipients = NULL, *iter;

    BACKEND_METHOD_CHECKED("calobj=%s", calobj);
    g_return_val_if_fail(cal != NULL, GNOME_Evolution_Calendar_OtherError);
    g_return_val_if_fail(calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

    /* parse calobj */
    comp = icalparser_parse_string(calobj);
    if (comp == NULL)
    {
        return GNOME_Evolution_Calendar_InvalidObject;
    }

    icalcomponent_collect_recipients(comp, priv->username, &recipients);
    icalcomponent_free(comp);

    e_cal_backend_3e_push_message(E_CAL_BACKEND_3E(backend), calobj);

    e_cal_backend_3e_do_immediate_sync(cb);

    /* this tells evolution that it should not send emails (iMIPs) by itself */
    for (iter = recipients; iter; iter = iter->next)
    {
        //BUG: there is a bug in itip-utils.c:509, name requires MAILTO: for now
        *users = g_list_append(*users, g_strdup_printf("MAILTO:%s", (char *)iter->data));
    }

    g_slist_foreach(recipients, (GFunc)g_free, NULL);
    g_slist_free(recipients);

    *modified_calobj = g_strdup(calobj);

    return GNOME_Evolution_Calendar_Success;
}

// }}}
// {{{ Calendar synchronization with external devices

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

    if (!e_cal_backend_cache_get_component(cb->priv->cache, key, NULL))
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

/** Returns a list of changes made since last check.
 * @todo fix recurring detached instances
 */
static ECalBackendSyncStatus e_cal_backend_3e_get_changes(ECalBackendSync *backend, EDataCal *cal, const char *change_id, GList * *adds, GList * *modifies, GList * *deletes)
{
    char *filename;
    char *path;
    EXmlHash *ehash;
    GList *iter;
    GList *list = NULL;
    struct _removals_data r;
    ECalBackendSyncStatus status;

    BACKEND_METHOD_CHECKED();
    g_return_val_if_fail(change_id != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

    filename = g_strdup_printf("snapshot-%s.db", change_id);
    path = g_build_filename(g_get_home_dir(), ".evolution/cache/calendar", priv->calname, filename, NULL);
    ehash = e_xmlhash_new(path);
    g_free(filename);
    g_free(path);

    status = e_cal_backend_3e_get_object_list(backend, NULL, "#t", &list);
    if (status != GNOME_Evolution_Calendar_Success)
    {
        e_xmlhash_destroy(ehash);
        return status;
    }

    /* calculate adds and modifies */
    for (iter = list; iter != NULL; iter = iter->next)
    {
        const char *uid;
        char *calobj;

        e_cal_component_get_uid(iter->data, &uid);
        calobj = e_cal_component_get_as_string(iter->data);

        /* check what type of change has occurred, if any */
        switch (e_xmlhash_compare(ehash, uid, calobj))
        {
        case E_XMLHASH_STATUS_SAME:
            break;

        case E_XMLHASH_STATUS_NOT_FOUND:
            *adds = g_list_prepend(*adds, g_strdup(calobj));
            e_xmlhash_add(ehash, uid, calobj);
            break;

        case E_XMLHASH_STATUS_DIFFERENT:
            *modifies = g_list_prepend(*modifies, g_strdup(calobj));
            e_xmlhash_add(ehash, uid, calobj);
            break;
        }

        g_free(calobj);
    }

    /* calculate deletions */
    r.cb = cb;
    r.deletes = NULL;
    r.ehash = ehash;
    e_xmlhash_foreach_key(ehash, calculate_removals, &r);

    *deletes = r.deletes;

    e_xmlhash_write(ehash);
    e_xmlhash_destroy(ehash);

    return GNOME_Evolution_Calendar_Success;
}

// }}}
// {{{ Garbage

/** Get list of attachments.
 *
 * XXX: This backend method is not used at all by evolution.
 */
static ECalBackendSyncStatus e_cal_backend_3e_get_attachment_list(ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, GSList * *list)
{
    BACKEND_METHOD_CHECKED();

    return GNOME_Evolution_Calendar_Success;
}

/** Discards an alarm (removes it or marks it as already displayed to the user).
 *
 * XXX: This method is probably not necessary.
 */
static ECalBackendSyncStatus e_cal_backend_3e_discard_alarm(ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid)
{
    BACKEND_METHOD_CHECKED();

    return GNOME_Evolution_Calendar_UnsupportedMethod;
}

// }}}
// {{{ GObject foo

G_DEFINE_TYPE(ECalBackend3e, e_cal_backend_3e, E_TYPE_CAL_BACKEND_SYNC)

static void e_cal_backend_3e_init(ECalBackend3e *cb)
{
    cb->priv = g_new0(ECalBackend3ePrivate, 1);

    g_static_rw_lock_init(&cb->priv->cache_lock);
    g_static_rec_mutex_init(&cb->priv->conn_mutex);
    cb->priv->sync_mutex = g_mutex_new();

    e_cal_backend_3e_messages_queue_init(cb);

    e_cal_backend_sync_set_lock(E_CAL_BACKEND_SYNC(cb), TRUE);
}

static void e_cal_backend_3e_finalize(GObject *backend)
{
    BACKEND_METHOD_CHECKED_NORETVAL();

    e_cal_backend_3e_periodic_sync_stop(cb);
    e_cal_backend_3e_free_connection(cb);
    e_cal_backend_3e_messages_queue_free(cb);
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
    if (priv->cache)
    {
        g_object_unref(priv->cache);
        priv->cache = NULL;
    }
    g_free(priv->cache_path);

    if (priv->default_zone)
    {
        icaltimezone_free(priv->default_zone, TRUE);
        priv->default_zone = NULL;
    }

    g_free(priv);
    cb->priv = NULL;

    G_OBJECT_CLASS(e_cal_backend_3e_parent_class)->finalize(backend);
}

static void e_cal_backend_3e_class_init(ECalBackend3eClass *class )
{
    GObjectClass                                     *object_class;
    ECalBackendClass                                 *backend_class;
    ECalBackendSyncClass                             *sync_class;

    object_class = (GObjectClass *)class;
    backend_class = (ECalBackendClass *)class;
    sync_class = (ECalBackendSyncClass *)class;

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
    sync_class->get_attachment_list_sync = e_cal_backend_3e_get_attachment_list;

    backend_class->is_loaded = e_cal_backend_3e_is_loaded;
    backend_class->start_query = e_cal_backend_3e_start_query;
    backend_class->get_mode = e_cal_backend_3e_get_mode;
    backend_class->set_mode = e_cal_backend_3e_set_mode;
    backend_class->internal_get_default_timezone = e_cal_backend_3e_internal_get_default_timezone;
    backend_class->internal_get_timezone = e_cal_backend_3e_internal_get_timezone;
}

// }}}

/** @} */
