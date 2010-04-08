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

#include <time.h>
#include "e-cal-backend-3e-priv.h"
#include "dns-txt-search.h"

// {{{ 3E server connection API

/** @addtogroup eds_conn */
/** @{ */

/** Setup 3E server connection information.
 *
 * @param cb 3E calendar backend.
 * @param username Username used for authentication.
 * @param password Password.
 * @param err Error pointer.
 */
gboolean e_cal_backend_3e_setup_connection(ECalBackend3e *cb, const char *username, const char *password)
{
    ESource *source;

    g_return_val_if_fail(cb != NULL, FALSE);
    g_return_val_if_fail(username != NULL, FALSE);
    g_return_val_if_fail(password != NULL, FALSE);

    source = e_cal_backend_get_source(E_CAL_BACKEND(cb));

    g_free(cb->priv->username);
    g_free(cb->priv->password);
    g_free(cb->priv->server_uri);
    cb->priv->server_uri = NULL;

    if (e_source_get_property(source, "eee-server"))
    {
        cb->priv->server_uri = g_strdup_printf("https://%s/RPC2", e_source_get_property(source, "eee-server"));
    }
    cb->priv->username = g_strdup(username);
    cb->priv->password = g_strdup(password);

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
gboolean e_cal_backend_3e_open_connection(ECalBackend3e *cb, GError * *err)
{
    GError *local_err = NULL;

    g_return_val_if_fail(cb != NULL, FALSE);
    g_return_val_if_fail(err == NULL || *err == NULL, FALSE);

    cb->priv->last_conn_failed = FALSE;

    /* if user tried to open connection in offline mode, close current connection
       if any and return error. this shouldn't happen, but let's check it anyway */
    if (!e_cal_backend_3e_calendar_is_online(cb))
    {
        g_set_error(err, 0, -1, "Can't open 3E server connection in offline mode.");
        e_cal_backend_3e_close_connection(cb);
        return FALSE;
    }

    g_static_rec_mutex_lock(&cb->priv->conn_mutex);

    /* resolve server URI from DNS TXT if necessary */
    if (cb->priv->server_uri == NULL)
    {
        char *server_hostname = get_eee_server_hostname(cb->priv->username);
        if (server_hostname == NULL)
        {
            g_set_error(err, 0, -1, "Can't resolve server URI for username '%s'", cb->priv->username);
            goto err;
        }
        cb->priv->server_uri = g_strdup_printf("https://%s/RPC2", server_hostname);
        g_free(server_hostname);
    }

    /* check that we have all info that is necessary to create conn */
    if (cb->priv->username == NULL || cb->priv->password == NULL || cb->priv->server_uri == NULL)
    {
        g_set_error(err, 0, -1, "Connection was not setup correctly, can't open.");
        goto err;
    }

    /* create conn object */
    if (cb->priv->conn == NULL)
    {
        cb->priv->conn = xr_client_new(err);
        if (cb->priv->conn == NULL)
        {
            goto err;
        }
        cb->priv->is_open = FALSE;
    }

    if (cb->priv->is_open)
    {
        /* was already locked in this thread */
        g_static_rec_mutex_unlock(&cb->priv->conn_mutex);
        return TRUE;
    }

    if (!xr_client_open(cb->priv->conn, cb->priv->server_uri, err))
    {
        goto err;
    }

    ESClient_authenticate(cb->priv->conn, cb->priv->username, cb->priv->password, &local_err);
    if (local_err)
    {
        g_propagate_error(err, local_err);
        xr_client_close(cb->priv->conn);
        goto err;
    }

    cb->priv->is_open = TRUE;

    return TRUE;

err:
    cb->priv->last_conn_failed = TRUE;
    g_static_rec_mutex_unlock(&cb->priv->conn_mutex);
    return FALSE;
}

/** Close connection to the server.
 *
 * @param cb 3E calendar backend.
 */
void e_cal_backend_3e_close_connection(ECalBackend3e *cb)
{
    g_return_if_fail(cb != NULL);

    if (cb->priv->is_open)
    {
        xr_client_close(cb->priv->conn);
        cb->priv->is_open = FALSE;

        g_static_rec_mutex_unlock(&cb->priv->conn_mutex);
    }
}

/** Close conenction and free private data.
 *
 * @param cb 3E calendar backend.
 */
void e_cal_backend_3e_free_connection(ECalBackend3e *cb)
{
    g_return_if_fail(cb != NULL);

    g_static_rec_mutex_lock(&cb->priv->conn_mutex);
    e_cal_backend_3e_close_connection(cb);

    g_free(cb->priv->username);
    g_free(cb->priv->password);
    g_free(cb->priv->server_uri);
    if (cb->priv->conn)
    {
        xr_client_free(cb->priv->conn);
    }

    cb->priv->username = NULL;
    cb->priv->password = NULL;
    cb->priv->server_uri = NULL;
    cb->priv->conn = NULL;

    g_static_rec_mutex_unlock(&cb->priv->conn_mutex);
}

/** @} */

// }}}
// {{{ Calendar metadata extraction/manipulation

/** @addtogroup eds_cal */
/** @{ */

/** Load calendar name, owner and permission from the ESource.
 *
 * @param cb 3E calendar backend.
 *
 * @return TRUE on success, FALSE otherwise.
 */
gboolean e_cal_backend_3e_calendar_info_load(ECalBackend3e *cb)
{
    ESource *source;
    const char *immediate_sync;

    source = e_cal_backend_get_source(E_CAL_BACKEND(cb));
    immediate_sync = e_source_get_property(source, "eee-immediate-sync");

    g_free(cb->priv->calname);
    g_free(cb->priv->owner);
    g_free(cb->priv->perm);
    cb->priv->calname = g_strdup(e_source_get_property(source, "eee-calname"));
    cb->priv->owner = g_strdup(e_source_get_property(source, "eee-owner"));
    cb->priv->perm = g_strdup(e_source_get_property(source, "eee-perm"));
    cb->priv->sync_immediately = (immediate_sync == NULL || !strcmp(immediate_sync, "1"));

    g_free(cb->priv->calspec);
    cb->priv->calspec = g_strdup_printf("%s:%s", cb->priv->owner, cb->priv->calname);

    if (cb->priv->calname == NULL || cb->priv->owner == NULL)
    {
        return FALSE;
    }

    return TRUE;
}

/** Check if calendar is owned by the user who accesses it.
 *
 * @param cb 3E calendar backend.
 *
 * @return TRUE if owned, FALSE if shared.
 */
gboolean e_cal_backend_3e_calendar_is_owned(ECalBackend3e *cb)
{
    if (cb->priv->username == NULL || cb->priv->owner == NULL)
    {
        return FALSE;
    }
    return !g_ascii_strcasecmp(cb->priv->username, cb->priv->owner);
}

/** Check if calendar has given permission.
 *
 * @param cb 3E calendar backend.
 * @param perm Permission string ("read", "write").
 *
 * @return TRUE if @a perm is equal to or subset of calendar's permission.
 */
gboolean e_cal_backend_3e_calendar_has_perm(ECalBackend3e *cb, const char *perm)
{
    if (cb->priv->perm == NULL)
    {
        return FALSE;
    }
    if (!g_ascii_strcasecmp(perm, cb->priv->perm))
    {
        return TRUE;
    }
    if (!g_ascii_strcasecmp(cb->priv->perm, "write"))
    {
        return TRUE;
    }
    return FALSE;
}

/** Set permission in the priv structure of calendar backend.
 *
 * @param cb 3E calendar backend.
 * @param perm Permission string ("read", "write").
 */
void e_cal_backend_3e_calendar_set_perm(ECalBackend3e *cb, const char *perm)
{
    g_free(cb->priv->perm);
    cb->priv->perm = g_strdup(perm);
}

/** Check if calendar is online (i.e. network operations should be enabled).
 *
 * @param cb 3E calendar backend.
 *
 * @return TRUE if it is OK to run network communication.
 */
gboolean e_cal_backend_3e_calendar_is_online(ECalBackend3e *cb)
{
    return cb->priv->mode == CAL_MODE_REMOTE;
}

/** Check if backend method should call sync immediately after cache
 * modification.
 *
 * @param cb 3E calendar backend.
 *
 * @return TRUE if sync is needed.
 */
gboolean e_cal_backend_3e_calendar_needs_immediate_sync(ECalBackend3e *cb)
{
    return cb->priv->sync_immediately && e_cal_backend_3e_calendar_is_online(cb) && !cb->priv->last_conn_failed;
}

/** Load permission from the calendar list.
 *
 * @param cb 3E calendar backend.
 *
 * @return TRUE if user has read or write access, FALSE on error or no
 * permission.
 */
gboolean e_cal_backend_3e_calendar_load_perm(ECalBackend3e *cb)
{
    GError *local_err = NULL;
    GSList *cals;
    GSList *iter;

    /* don't sync perm for owned calendars because it can't change */
    if (e_cal_backend_3e_calendar_is_owned(cb))
    {
        e_cal_backend_3e_calendar_set_perm(cb, "write");
        return TRUE;
    }

    if (!e_cal_backend_3e_open_connection(cb, &local_err))
    {
        g_error_free(local_err);
        return FALSE;
    }

    cals = ESClient_getCalendars(cb->priv->conn, "", &local_err);
    if (local_err)
    {
        g_error_free(local_err);
        e_cal_backend_3e_close_connection(cb);
        return FALSE;
    }

    for (iter = cals; iter; iter = iter->next)
    {
        ESCalendarInfo *cal = iter->data;
        if (!g_ascii_strcasecmp(cal->owner, cb->priv->owner) &&
            !g_ascii_strcasecmp(cal->name, cb->priv->calname))
        {
            e_cal_backend_3e_calendar_set_perm(cb, cal->perm);
            Array_ESCalendarInfo_free(cals);
            e_cal_backend_3e_close_connection(cb);
            return TRUE;
        }
    }

    e_cal_backend_3e_close_connection(cb);

    Array_ESCalendarInfo_free(cals);
    e_cal_backend_3e_calendar_set_perm(cb, "none");
    return FALSE;
}

/** @} */

// }}}
// {{{ Calendar synchronization

/** @addtogroup eds_sync */
/** @{ */

// {{{ 3E Cache Wrappers - Used to track state of objects in cache.

/** Wrapper for e_cal_backend_cache_put_component().
 *
 * Keeps track of cache state of the component by using
 * e_cal_component_set_cache_state(). If component already existed in cache and
 * did not have cache state E_CAL_COMPONENT_CACHE_STATE_CREATED, its cache state will
 * be set to E_CAL_COMPONENT_CACHE_STATE_MODIFIED, in all other cases it will be
 * set to will be set to E_CAL_COMPONENT_CACHE_STATE_CREATED.
 *
 * @param cb 3E calendar backend.
 * @param cache Calendar backend cache object.
 * @param comp ECalComponent object.
 *
 * @return TRUE if successfully stored.
 */
gboolean e_cal_backend_3e_cache_put_component(ECalBackend3e *cb, ECalBackendCache *cache, ECalComponent *comp)
{
    ECalComponentId *id;
    ECalComponentCacheState cache_state = E_CAL_COMPONENT_CACHE_STATE_CREATED;
    gboolean retval;

    id = e_cal_component_get_id(comp);
    if (id)
    {
        g_static_rw_lock_writer_lock(&cb->priv->cache_lock);
        ECalComponent *existing = e_cal_backend_cache_get_component(cache, id->uid, id->rid);
        if (existing)
        {
            if (e_cal_component_get_cache_state(existing) != E_CAL_COMPONENT_CACHE_STATE_CREATED)
            {
                cache_state = E_CAL_COMPONENT_CACHE_STATE_MODIFIED;
            }

            g_object_unref(existing);
        }

        e_cal_component_set_cache_state(comp, cache_state);
        retval = e_cal_backend_cache_put_component(cache, comp);
        g_static_rw_lock_writer_unlock(&cb->priv->cache_lock);

        e_cal_component_free_id(id);
        return retval;
    }

    return FALSE;
}

/** Wrapper for e_cal_backend_cache_remove_component().
 *
 * This function does not actually remove component from cache in most cases. If
 * component already existed in cache and did not have cache state
 * E_CAL_COMPONENT_CACHE_STATE_CREATED, its state will be set to
 * E_CAL_COMPONENT_CACHE_STATE_REMOVED, otherwise it will be removed from cache.
 *
 * @param cb 3E calendar backend.
 * @param cache Calendar backend cache object.
 * @param uid UID of the calendar component.
 * @param rid RID of the detached instance of recurring event.
 *
 * @return TRUE if successfully removed, FALSE on failure (storage problem or not
 * found).
 */
gboolean e_cal_backend_3e_cache_remove_component(ECalBackend3e *cb, ECalBackendCache *cache, const char *uid, const char *rid)
{
    ECalComponent *existing;
    gboolean retval;

    g_static_rw_lock_writer_lock(&cb->priv->cache_lock);
    existing = e_cal_backend_cache_get_component(cache, uid, rid);
    if (existing)
    {
        if (e_cal_component_get_cache_state(existing) == E_CAL_COMPONENT_CACHE_STATE_CREATED)
        {
            retval = e_cal_backend_cache_remove_component(cache, uid, rid);
        }
        else
        {
            e_cal_component_set_cache_state(existing, E_CAL_COMPONENT_CACHE_STATE_REMOVED);
            retval = e_cal_backend_cache_put_component(cache, existing);
        }

        g_object_unref(existing);
    }
    else
    {
        retval = FALSE;
    }
    g_static_rw_lock_writer_unlock(&cb->priv->cache_lock);

    return retval;
}

/** Wrapper for e_cal_backend_cache_get_component().
 *
 * Get single component from cache. If component exists in cache but have cache
 * state E_CAL_COMPONENT_CACHE_STATE_REMOVED, NULL will be returned.
 *
 * @param cb 3E calendar backend.
 * @param cache Calendar backend cache object.
 * @param uid UID of the calendar component.
 * @param rid RID of the detached instance of recurring event.
 *
 * @return ECalComponent object or NULL.
 */
ECalComponent *e_cal_backend_3e_cache_get_component(ECalBackend3e *cb, ECalBackendCache *cache, const char *uid, const char *rid)
{
    ECalComponent *comp;

    g_static_rw_lock_reader_lock(&cb->priv->cache_lock);
    comp = e_cal_backend_cache_get_component(cache, uid, rid);
    g_static_rw_lock_reader_unlock(&cb->priv->cache_lock);
    if (comp && e_cal_component_get_cache_state(comp) == E_CAL_COMPONENT_CACHE_STATE_REMOVED)
    {
        g_object_unref(comp);
        return NULL;
    }

    return comp;
}

/** Wrapper for e_cal_backend_cache_get_components().
 *
 * Get all components from cache. Components with cache state
 * E_CAL_COMPONENT_CACHE_STATE_REMOVED will be omitted.
 *
 * @param cb 3E calendar backend.
 * @param cache Calendar backend cache object.
 *
 * @return List of ECalComponent objects.
 */
GList *e_cal_backend_3e_cache_get_components(ECalBackend3e *cb, ECalBackendCache *cache)
{
    GList *iter, *iter_next;
    GList *list;

    g_static_rw_lock_reader_lock(&cb->priv->cache_lock);
    list = e_cal_backend_cache_get_components(cache);
    g_static_rw_lock_reader_unlock(&cb->priv->cache_lock);
    for (iter = list; iter; iter = iter_next)
    {
        ECalComponent *comp = E_CAL_COMPONENT(iter->data);
        iter_next = iter->next;

        if (e_cal_component_get_cache_state(comp) == E_CAL_COMPONENT_CACHE_STATE_REMOVED)
        {
            list = g_list_remove_link(list, iter);
            g_object_unref(comp);
        }
    }

    return list;
}

/** Wrapper for e_cal_backend_cache_get_components_by_uid().
 *
 * Get master components and all detached instances for given UID.
 * Components/detached instances with cache state
 * E_CAL_COMPONENT_CACHE_STATE_REMOVED will be omitted.
 *
 * @param cb 3E calendar backend.
 * @param cache Calendar backend cache object.
 * @param uid UID of the calendar components.
 *
 * @return List of matching ECalComponent objects.
 */
GSList *e_cal_backend_3e_cache_get_components_by_uid(ECalBackend3e *cb, ECalBackendCache *cache, const char *uid)
{
    GSList *iter, *iter_next;
    GSList *list;

    g_static_rw_lock_reader_lock(&cb->priv->cache_lock);
    list = e_cal_backend_cache_get_components_by_uid(cache, uid);
    g_static_rw_lock_reader_unlock(&cb->priv->cache_lock);
    for (iter = list; iter; iter = iter_next)
    {
        ECalComponent *comp = E_CAL_COMPONENT(iter->data);
        iter_next = iter->next;

        if (e_cal_component_get_cache_state(comp) == E_CAL_COMPONENT_CACHE_STATE_REMOVED)
        {
            list = g_slist_remove_link(list, iter);
            g_object_unref(comp);
        }
    }

    return list;
}

/** Wrapper for e_cal_backend_cache_get_timezone().
 *
 * @param cb 3E calendar backend.
 * @param cache Calendar backend cache object.
 * @param tzid TZID of timezone.
 *
 * @return icaltimezone object (owned by cache, don't free).
 */
const icaltimezone *e_cal_backend_3e_cache_get_timezone(ECalBackend3e *cb, ECalBackendCache *cache, const char *tzid)
{
    const icaltimezone *zone;

    g_static_rw_lock_reader_lock(&cb->priv->cache_lock);
    zone = e_cal_backend_cache_get_timezone(cache, tzid);
    g_static_rw_lock_reader_unlock(&cb->priv->cache_lock);

    return zone;
}

/** Wrapper for e_cal_backend_cache_put_timezone().
 *
 * Put timezone into cache. Timezone component cache state will be set to
 * E_CAL_COMPONENT_CACHE_STATE_CREATED.
 *
 * @param cb 3E calendar backend.
 * @param cache Calendar backend cache object.
 * @param zone icaltimezone object.
 *
 * @return TRUE if successfully stored.
 */
gboolean e_cal_backend_3e_cache_put_timezone(ECalBackend3e *cb, ECalBackendCache *cache, const icaltimezone *zone)
{
    gboolean retval;

    icalcomponent_set_cache_state(icaltimezone_get_component((icaltimezone *)zone), E_CAL_COMPONENT_CACHE_STATE_CREATED);

    g_static_rw_lock_writer_lock(&cb->priv->cache_lock);
    retval = e_cal_backend_cache_put_timezone(cache, zone);
    g_static_rw_lock_writer_unlock(&cb->priv->cache_lock);

    return retval;
}

// }}}
// {{{ Last sync timestamp storage

/** Get timestamp of last sync.
 *
 * @param cb 3E calendar backend.
 *
 * @return Timestamp in local time.
 */
time_t e_cal_backend_3e_get_sync_timestamp(ECalBackend3e *cb)
{
    time_t stamp = 0;

    g_static_rw_lock_reader_lock(&cb->priv->cache_lock);
    const char *ts = e_cal_backend_cache_get_server_utc_time(cb->priv->cache);
    g_static_rw_lock_reader_unlock(&cb->priv->cache_lock);
    if (ts)
    {
        icaltimetype time = icaltime_from_string(ts);
        stamp = icaltime_as_timet_with_zone(time, NULL);
    }

    return stamp;
}

/** Store timestamp of last sync.
 *
 * @param cb 3E calendar backend.
 * @param stamp Timestamp in local time.
 */
void e_cal_backend_3e_set_sync_timestamp(ECalBackend3e *cb, time_t stamp)
{
    g_static_rw_lock_writer_lock(&cb->priv->cache_lock);
    e_cal_backend_cache_put_server_utc_time(cb->priv->cache, icaltime_as_ical_string(icaltime_from_timet_with_zone(stamp, 0, NULL)));
    g_static_rw_lock_writer_unlock(&cb->priv->cache_lock);
}

// }}}
// {{{ iTIP message queue

static char *get_messages_queue_file(ECalBackend3e *cb)
{
    return g_build_filename(e_cal_backend_3e_get_cache_path(cb), "messages.xml", NULL);
}

static void add_message(const char *item, xmlNode *root)
{
    xmlNewChild(root, NULL, BAD_CAST "item", BAD_CAST item);
}

void e_cal_backend_3e_messages_queue_init(ECalBackend3e *cb)
{
    cb->priv->message_queue_mutex = g_mutex_new();
    cb->priv->message_queue = g_queue_new();
}

void e_cal_backend_3e_messages_queue_free(ECalBackend3e *cb)
{
    e_cal_backend_3e_messages_queue_clear(cb);
    g_queue_free(cb->priv->message_queue);
    g_mutex_free(cb->priv->message_queue_mutex);
}

gboolean e_cal_backend_3e_messages_queue_save(ECalBackend3e *cb)
{
    xmlDoc *doc = xmlNewDoc(BAD_CAST "1.0");
    xmlNode *root = xmlNewNode(NULL, BAD_CAST "queue");

    xmlDocSetRootElement(doc, root);

    g_mutex_lock(cb->priv->message_queue_mutex);

    g_queue_foreach(cb->priv->message_queue, (GFunc)add_message, root);

    char *path = get_messages_queue_file(cb);
    int rs = xmlSaveFormatFile(path, doc, 1);
    g_free(path);

    g_mutex_unlock(cb->priv->message_queue_mutex);

    xmlFreeDoc(doc);

    return rs != -1;
}

void e_cal_backend_3e_messages_queue_load(ECalBackend3e *cb)
{
    char *path = get_messages_queue_file(cb);
    xmlDoc *doc = xmlReadFile(path, "UTF-8", XML_PARSE_NOERROR | XML_PARSE_NOWARNING | XML_PARSE_NONET);

    g_free(path);
    xmlNode *root = xmlDocGetRootElement(doc);

    g_mutex_lock(cb->priv->message_queue_mutex);

    e_cal_backend_3e_messages_queue_clear(cb);

    if (root)
    {
        xmlNode *item;
        for (item = root->children; item; item = item->next)
        {
            if (item->type != XML_ELEMENT_NODE)
            {
                continue;
            }

            xmlChar *content = xmlNodeGetContent(item);
            g_queue_push_tail(cb->priv->message_queue, g_strdup((char *)content));
            xmlFree(content);
        }
    }

    g_mutex_unlock(cb->priv->message_queue_mutex);

    xmlFreeDoc(doc);
}

void e_cal_backend_3e_messages_queue_clear(ECalBackend3e *cb)
{
    g_queue_foreach(cb->priv->message_queue, (GFunc)g_free, NULL);
    g_queue_clear(cb->priv->message_queue);
}

/** Add message to the beginning of the iTIPs queue.
 *
 * @param cb 3E calendar backend.
 * @param object iTIP string.
 *
 * @return TRUE on success, FALSE otherwise.
 */
gboolean e_cal_backend_3e_push_message(ECalBackend3e *cb, const char *object)
{
    g_return_val_if_fail(object != NULL, FALSE);

    g_mutex_lock(cb->priv->message_queue_mutex);
    g_queue_push_head(cb->priv->message_queue, g_strdup(object));
    g_mutex_unlock(cb->priv->message_queue_mutex);

    return e_cal_backend_3e_messages_queue_save(cb);
}

/** Remove given message of the end of the iTIPs queue.
 *
 * @param cb 3E calendar backend.
 *
 * @return TRUE on success, FALSE otherwise.
 */
gboolean e_cal_backend_3e_pop_message(ECalBackend3e *cb)
{
    g_mutex_lock(cb->priv->message_queue_mutex);
    g_free(g_queue_pop_tail(cb->priv->message_queue));
    g_mutex_unlock(cb->priv->message_queue_mutex);

    return e_cal_backend_3e_messages_queue_save(cb);
}

/** Get the oldest message from the queue.
 *
 * @param cb 3E calendar backend.
 *
 * @return iTIP string or NULL if queue is empty.
 */
const char *e_cal_backend_3e_get_message(ECalBackend3e *cb)
{
    const char *msg;

    g_mutex_lock(cb->priv->message_queue_mutex);
    msg = g_queue_peek_tail(cb->priv->message_queue);
    g_mutex_unlock(cb->priv->message_queue_mutex);

    return msg;
}

/** Send message.
 *
 * @param cb 3E calendar backend.
 * @param object iTIP string.
 * @param err Error pointer.
 *
 * @return TRUE on success, FALSE otherwise.
 */
gboolean e_cal_backend_3e_send_message(ECalBackend3e *cb, const char *object, GError * *err)
{
    icalcomponent *comp;
    GSList *recipients = NULL, *iter;
    GError *local_err = NULL;
    ECalComponent *ecomp;
    char *remote_object = NULL;

    comp = icalparser_parse_string(object);
    if (comp == NULL)
    {
        g_set_error(err, 0, -1, "Invalid iTIP string. Can't send the message.");
        return FALSE;
    }

    icalcomponent_collect_recipients(comp, cb->priv->username, &recipients);
    icalcomponent *comp_payload = icalcomponent_get_itip_payload(comp);
    e_cal_backend_3e_convert_attachment_uris_to_remote_icalcomp(cb, comp_payload);
    ecomp = e_cal_component_new();
    e_cal_component_set_icalcomponent(ecomp, icalcomponent_new_clone(comp_payload));
    remote_object = icalcomponent_as_ical_string(comp);
    icalcomponent_free(comp);

    if (!e_cal_backend_3e_upload_attachments(cb, ecomp, &local_err))
    {
        //XXX: handle errors
        g_object_unref(ecomp);
        g_clear_error(&local_err);
        return FALSE;
    }
    g_object_unref(ecomp);

    if (recipients)
    {
        if (!e_cal_backend_3e_open_connection(cb, &local_err))
        {
            g_error_free(local_err);
            return GNOME_Evolution_Calendar_OtherError;
        }

        ESClient_sendMessage(cb->priv->conn, recipients, remote_object, &local_err);

        g_slist_foreach(recipients, (GFunc)g_free, NULL);
        g_slist_free(recipients);

        e_cal_backend_3e_close_connection(cb);

        if (local_err)
        {
            e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb), "Can't send meeting request.", local_err);
            g_propagate_error(err, local_err);
            return FALSE;
        }
    }

    return TRUE;
}

/** Send all messages that are in the iTIPs queue.
 *
 * @param cb 3E calendar backend.
 *
 * @return TRUE on success, FALSE otherwise.
 */
gboolean e_cal_backend_3e_process_message_queue(ECalBackend3e *cb)
{
    GError *local_err = NULL;
    const char *message;

    while ((message = e_cal_backend_3e_get_message(cb)) != NULL)
    {
        e_cal_backend_3e_send_message(cb, message, &local_err);
        if (local_err)
        {
            g_warning("send_message failed. (%s)", local_err->message);
            g_clear_error(&local_err);
        }

        e_cal_backend_3e_pop_message(cb);
    }

    return TRUE;
}

// }}}
// {{{ Timezones synchronization

/** Get all timezones from the cache.
 *
 * @param cb 3E calendar backend.
 * @param cache Calendar backend cache object.
 *
 * @return List of icaltimezone objects (free them using icaltimezone_free(x, 1);).
 */
static GSList *e_cal_backend_cache_get_timezones(ECalBackend3e *cb, ECalBackendCache *cache)
{
    GSList *keys, *iter;
    GSList *list = NULL;

    g_return_val_if_fail(E_IS_CAL_BACKEND_CACHE(cache), NULL);
    keys = e_file_cache_get_keys(E_FILE_CACHE(cache));

    for (iter = keys; iter; iter = iter->next)
    {
        char *key = iter->data;
        const icaltimezone *zone;

        g_static_rw_lock_reader_lock(&cb->priv->cache_lock);
        zone = e_cal_backend_cache_get_timezone(cache, key);
        g_static_rw_lock_reader_unlock(&cb->priv->cache_lock);

        if (zone)
        {
            icalcomponent *zone_comp = icaltimezone_get_component((icaltimezone *)zone);
            icaltimezone *new_zone = icaltimezone_new();
            /* make sure you have patched eds if you get segfaults here */
            if (zone_comp == NULL)
            {
                g_critical("Patch your evolution-data-server or else...");
            }

            icaltimezone_set_component(new_zone, icalcomponent_new_clone(zone_comp));
            list = g_slist_prepend(list, new_zone);
        }
    }

    /* eds patch required here! */
    g_slist_free(keys);
    return list;
}

/** Sync new timezones from the cache to the server.
 *
 * Assumes caller opened connection using e_cal_backend_3e_open_connection().
 *
 * @param cb 3E calendar backend.
 *
 * @return TRUE on success.
 */
static gboolean sync_timezones_to_server(ECalBackend3e *cb)
{
    GError *local_err = NULL;
    GSList *timezones, *iter;

    timezones = e_cal_backend_cache_get_timezones(cb, cb->priv->cache);

    for (iter = timezones; iter; iter = iter->next)
    {
        icaltimezone *zone = iter->data;
        icalcomponent *zone_comp = icaltimezone_get_component(zone);
        ECalComponentCacheState state = icalcomponent_get_cache_state(zone_comp);
        icalcomponent_set_cache_state(zone_comp, E_CAL_COMPONENT_CACHE_STATE_NONE);
        char *object = icalcomponent_as_ical_string(zone_comp);

        //XXX: always try to add all timezones from the cache
        //if (state == E_CAL_COMPONENT_CACHE_STATE_CREATED)
        {
            ESClient_addObject(cb->priv->conn, cb->priv->calspec, object, &local_err);
            if (local_err)
            {
                g_clear_error(&local_err);
                break;
            }

            g_static_rw_lock_writer_lock(&cb->priv->cache_lock);
            e_cal_backend_cache_put_timezone(cb->priv->cache, zone);
            g_static_rw_lock_writer_unlock(&cb->priv->cache_lock);
        }

        icaltimezone_free(zone, 1);
    }

    g_slist_free(timezones);
    return TRUE;
}

// }}}
// {{{ Client -> Server synchronization

/** Sync cache changes to the server and unmark them.
 *
 * @param cb 3E calendar backend.
 *
 * @return TRUE on success.
 *
 * @todo Conflict resolution.
 */
gboolean e_cal_backend_3e_sync_cache_to_server(ECalBackend3e *cb)
{
    GError *local_err = NULL;
    GList *components, *iter;

    if (!e_cal_backend_3e_open_connection(cb, &local_err))
    {
        g_warning("Sync failed. Can't open connection to the 3E server. (%s)", local_err ? local_err->message : "Unknown error");
        g_clear_error(&local_err);
        return FALSE;
    }

    sync_timezones_to_server(cb);

    g_static_rw_lock_reader_lock(&cb->priv->cache_lock);
    components = e_cal_backend_cache_get_components(cb->priv->cache);
    g_static_rw_lock_reader_unlock(&cb->priv->cache_lock);

    for (iter = components; iter && !e_cal_backend_3e_sync_should_stop(cb); iter = iter->next)
    {
        ECalComponent *comp = E_CAL_COMPONENT(iter->data);
        ECalComponent *remote_comp;
        ECalComponentId *id = e_cal_component_get_id(comp);
        ECalComponentCacheState state = e_cal_component_get_cache_state(comp);
        /* remove client properties before sending component to the server */
        e_cal_component_set_x_property(comp, "X-EVOLUTION-STATUS", NULL);
        e_cal_component_set_cache_state(comp, E_CAL_COMPONENT_CACHE_STATE_NONE);
        e_cal_component_set_x_property(comp, "X-3E-DELETED", NULL);
        remote_comp = e_cal_component_clone(comp);
        gboolean attachments_converted = e_cal_backend_3e_convert_attachment_uris_to_remote(cb, remote_comp);
        char *remote_object = e_cal_component_get_as_string(remote_comp);
        char *object = e_cal_component_get_as_string(comp);

        if (!attachments_converted)
        {
            goto next;
        }

        if (state == E_CAL_COMPONENT_CACHE_STATE_CREATED || state == E_CAL_COMPONENT_CACHE_STATE_MODIFIED)
        {
            if (!e_cal_backend_3e_upload_attachments(cb, remote_comp, &local_err))
            {
                e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb), "3E attachemnts sync failure", local_err);
                g_clear_error(&local_err);
                goto next;
            }
        }

        switch (state)
        {
        case E_CAL_COMPONENT_CACHE_STATE_CREATED:
        {
            ESClient_addObject(cb->priv->conn, cb->priv->calspec, remote_object, &local_err);
            if (local_err)
            {
                e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb), "3E sync failure", local_err);
                g_clear_error(&local_err);
                break;
            }

            char *new_object = e_cal_component_get_as_string(comp);
            e_cal_backend_notify_object_modified(E_CAL_BACKEND(cb), object, new_object);
            g_free(new_object);

            g_static_rw_lock_writer_lock(&cb->priv->cache_lock);
            e_cal_backend_cache_put_component(cb->priv->cache, comp);
            g_static_rw_lock_writer_unlock(&cb->priv->cache_lock);
            break;
        }

        case E_CAL_COMPONENT_CACHE_STATE_MODIFIED:
        {
            ESClient_updateObject(cb->priv->conn, cb->priv->calspec, remote_object, &local_err);
            if (local_err)
            {
                e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb), "3E sync failure", local_err);
                g_clear_error(&local_err);
                break;
            }

            char *new_object = e_cal_component_get_as_string(comp);
            e_cal_backend_notify_object_modified(E_CAL_BACKEND(cb), object, new_object);
            g_free(new_object);

            g_static_rw_lock_writer_lock(&cb->priv->cache_lock);
            e_cal_backend_cache_put_component(cb->priv->cache, comp);
            g_static_rw_lock_writer_unlock(&cb->priv->cache_lock);
            break;
        }

        case E_CAL_COMPONENT_CACHE_STATE_REMOVED:
        {
            char *oid = id->rid ? g_strdup_printf("%s@%s", id->uid, id->rid) : g_strdup(id->uid);
            ESClient_deleteObject(cb->priv->conn, cb->priv->calspec, oid, &local_err);
            g_free(oid);
            if (local_err)
            {
                // ignore the error if component doesn't exist anymore
                if (local_err->code == ES_XMLRPC_ERROR_UNKNOWN_COMPONENT)
                {
                    g_clear_error(&local_err);
                }
                else 
                {
                    e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb), "3E sync failure", local_err);
                    g_clear_error(&local_err);
                    break;
                }
            }

            g_static_rw_lock_writer_lock(&cb->priv->cache_lock);
            e_cal_backend_cache_remove_component(cb->priv->cache, id->uid, id->rid);
            g_static_rw_lock_writer_unlock(&cb->priv->cache_lock);
            break;
        }

        case E_CAL_COMPONENT_CACHE_STATE_NONE:
        default:
            break;
        }

next:
        g_object_unref(comp);
        g_object_unref(remote_comp);
        e_cal_component_free_id(id);
        g_free(object);
        g_free(remote_object);
    }

    g_list_free(components);

    e_cal_backend_3e_close_connection(cb);

    // send itips
    e_cal_backend_3e_process_message_queue(cb);

    return TRUE;
}

// }}}
// {{{ Server -> Client synchronization

/** Query server for VCALENDAR containing requested components.
 *
 * @param cb 3E calendar backend.
 * @param query Query as specified in 3E protocol (see queryObjects() method).
 *
 * @return VCALENDAR or NULL on error.
 */
static icalcomponent *get_server_objects(ECalBackend3e *cb, const char *query)
{
    GError *local_err = NULL;
    char *servercal;
    icalcomponent *ical;

    if (!e_cal_backend_3e_open_connection(cb, &local_err))
    {
        g_warning("Sync failed. Can't open connection to the 3E server. (%s)", local_err->message);
        g_error_free(local_err);
        return NULL;
    }

    servercal = ESClient_queryObjects(cb->priv->conn, cb->priv->calspec, query, &local_err);
    if (local_err)
    {
        g_clear_error(&local_err);
    }

    e_cal_backend_3e_close_connection(cb);

    if (servercal == NULL)
    {
        return NULL;
    }

    ical = icalcomponent_new_from_string(servercal);
    if (ical == NULL)
    {
        return NULL;
    }

    return ical;
}

/** Sync changes from the server to the cache.
 *
 * @param cb 3E calendar backend.
 *
 * @return TRUE on success.
 *
 * @todo Handle UID/RID.
 * @todo Better server error handling.
 * @todo Conflict resolution.
 */
gboolean e_cal_backend_3e_sync_server_to_cache(ECalBackend3e *cb)
{
    GError *local_err = NULL;
    gboolean update_sync = TRUE;
    icalcomponent *ical;
    icalcomponent *icomp;
    char filter[128];
    struct tm tm;
    time_t stamp = MAX(e_cal_backend_3e_get_sync_timestamp(cb) - 60 * 60 * 24, 0); /*XXX: always add 1 day padding to prevent timezone problems */

    /* prepare query filter string */
    gmtime_r(&stamp, &tm);
    strftime(filter, sizeof(filter), "date_from('%F %T')", &tm);

    ical = get_server_objects(cb, filter);
    if (ical == NULL)
    {
        return FALSE;
    }

    for (icomp = icalcomponent_get_first_component(ical, ICAL_ANY_COMPONENT);
         icomp;
         icomp = icalcomponent_get_next_component(ical, ICAL_ANY_COMPONENT))
    {
        icalcomponent_kind kind = icalcomponent_isa(icomp);
        icalcomponent_set_cache_state(icomp, E_CAL_COMPONENT_CACHE_STATE_NONE);

        if (kind == ICAL_VEVENT_COMPONENT)
        {
            ECalComponent *comp;
            const char *uid = icalcomponent_get_uid(icomp);
            gboolean server_deleted = icalcomponent_3e_status_is_deleted(icomp);
            ECalComponentCacheState comp_state = E_CAL_COMPONENT_CACHE_STATE_NONE;

            g_static_rw_lock_reader_lock(&cb->priv->cache_lock);
            comp = e_cal_backend_cache_get_component(cb->priv->cache, uid, NULL);
            g_static_rw_lock_reader_unlock(&cb->priv->cache_lock);
            if (comp)
            {
                comp_state = e_cal_component_get_cache_state(comp);
            }

            if (server_deleted)
            {
                /* deleted by the server */
                if (comp)
                {
                    char *object = e_cal_component_get_as_string(comp);
                    ECalComponentId *id = e_cal_component_get_id(comp);

                    //XXX: check if cached object was not modified since (use sequences?)
                    // if it was modified mark it as created?

                    g_static_rw_lock_writer_lock(&cb->priv->cache_lock);
                    e_cal_backend_cache_remove_component(cb->priv->cache, uid, NULL);
                    g_static_rw_lock_writer_unlock(&cb->priv->cache_lock);

                    e_cal_backend_notify_object_removed(E_CAL_BACKEND(cb), id, object, NULL);

                    e_cal_component_free_id(id);
                    g_free(object);
                }
            }
            else
            {
                char *old_object = NULL;
                char *object;
                ECalComponent *new_comp = e_cal_component_new();

                e_cal_component_set_icalcomponent(new_comp, icalcomponent_new_clone(icomp));
                e_cal_component_set_cache_state(new_comp, E_CAL_COMPONENT_CACHE_STATE_NONE);
                e_cal_backend_3e_convert_attachment_uris_to_local(cb, new_comp);
                if (comp)
                {
                    old_object = e_cal_component_get_as_string(comp);
                }
                object = e_cal_component_get_as_string(new_comp);

                if (old_object == NULL)
                {
                    if (e_cal_backend_3e_download_attachments(cb, new_comp, &local_err))
                    {
                        /* not in cache yet */
                        g_static_rw_lock_writer_lock(&cb->priv->cache_lock);
                        e_cal_backend_cache_put_component(cb->priv->cache, new_comp);
                        g_static_rw_lock_writer_unlock(&cb->priv->cache_lock);

                        e_cal_backend_notify_object_created(E_CAL_BACKEND(cb), object);
                    }
                    else
                    {
                        e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb), "Can't download attachment.", local_err);
                        g_clear_error(&local_err);
                        update_sync = FALSE;
                    }
                }
                else if (strcmp(old_object, object))
                {
                    /* what is in cache and what is on server differs */
                    if (comp_state != E_CAL_COMPONENT_CACHE_STATE_NONE)
                    {
                        /* modified in cache, don't do anything */
                    }
                    else
                    {
                        if (e_cal_backend_3e_download_attachments(cb, new_comp, &local_err))
                        {
                            /* sync with server */
                            g_static_rw_lock_writer_lock(&cb->priv->cache_lock);
                            e_cal_backend_cache_put_component(cb->priv->cache, new_comp);
                            g_static_rw_lock_writer_unlock(&cb->priv->cache_lock);

                            e_cal_backend_notify_object_modified(E_CAL_BACKEND(cb), old_object, object);
                        }
                        else
                        {
                            e_cal_backend_notify_gerror_error(E_CAL_BACKEND(cb), "Can't download attachment.", local_err);
                            g_clear_error(&local_err);
                            update_sync = FALSE;
                        }
                    }
                }

                g_free(old_object);
                g_free(object);
                g_object_unref(new_comp);
            }

            if (comp)
            {
                g_object_unref(comp);
            }
        }
        else if (kind == ICAL_VTIMEZONE_COMPONENT)
        {
            const char *tzid = icalcomponent_get_tzid(icomp);

            /* import non-existing timezones from the server */
            if (!e_cal_backend_cache_get_timezone(cb->priv->cache, tzid))
            {
                icaltimezone *zone = icaltimezone_new();
                icalcomponent *zone_comp = icalcomponent_new_clone(icomp);
                if (icaltimezone_set_component(zone, zone_comp))
                {
                    g_static_rw_lock_writer_lock(&cb->priv->cache_lock);
                    e_cal_backend_cache_put_timezone(cb->priv->cache, zone);
                    g_static_rw_lock_writer_unlock(&cb->priv->cache_lock);
                }
                else
                {
                    icalcomponent_free(zone_comp);
                }
                icaltimezone_free(zone, 1);
            }
        }
        else
        {
            g_warning("Unsupported component kind (%d) found on the 3E server.", kind);
        }
    }

    if (update_sync)
    {
        e_cal_backend_3e_set_sync_timestamp(cb, time(NULL));
    }

    icalcomponent_free(ical);
    return TRUE;
}

// }}}
// {{{ Synchronization thread

enum { SYNC_NORMAL, SYNC_NOW, SYNC_PAUSE, SYNC_STOP };

/** Periodic sync callback.
 *
 * @param cb 3E calendar backend.
 *
 * @return Always TRUE (continue sync).
 */
static gpointer sync_thread(ECalBackend3e *cb)
{
    int timeout = 5;

    while (TRUE)
    {
        switch (g_atomic_int_get(&cb->priv->sync_request))
        {
        case SYNC_NORMAL:
            g_usleep(1000000);
            if (--timeout > 0)
            {
                break;
            }

        case SYNC_NOW:
            if (g_atomic_int_get(&cb->priv->sync_request) == SYNC_STOP)
            {
                return NULL;
            }
            timeout = 5;

            g_atomic_int_set(&cb->priv->sync_request, SYNC_NORMAL);
            if (e_cal_backend_3e_calendar_is_online(cb) && e_cal_backend_3e_calendar_load_perm(cb))
            {
                e_cal_backend_3e_sync_server_to_cache(cb);

                if (e_cal_backend_3e_calendar_has_perm(cb, "write"))
                {
                    e_cal_backend_3e_sync_cache_to_server(cb);
                }
            }
            break;

        case SYNC_PAUSE:
            g_usleep(1000000);
            break;

        case SYNC_STOP:
            return NULL;
        }
    }

    return NULL;
}

/** Enable periodic sync on this backend.
 *
 * @param cb 3E calendar backend.
 */
void e_cal_backend_3e_periodic_sync_enable(ECalBackend3e *cb)
{
    g_mutex_lock(cb->priv->sync_mutex);

    /* do sync ASAP after enable */
    g_atomic_int_set(&cb->priv->sync_request, SYNC_NOW);

    /* start thread if necessary */
    if (cb->priv->sync_thread == NULL)
    {
        cb->priv->sync_thread = g_thread_create((GThreadFunc)sync_thread, cb, TRUE, NULL);
    }
    if (cb->priv->sync_thread == NULL)
    {
        g_warning("Failed to create sync thread for 3E calendar.");
    }

    g_mutex_unlock(cb->priv->sync_mutex);
}

/** Disable periodic sync.
 *
 * @param cb 3E calendar backend.
 */
void e_cal_backend_3e_periodic_sync_disable(ECalBackend3e *cb)
{
    g_mutex_lock(cb->priv->sync_mutex);
    g_atomic_int_set(&cb->priv->sync_request, SYNC_PAUSE);
    g_mutex_unlock(cb->priv->sync_mutex);
}

/** Check if whatever sync thread is doing should be cancelled.
 *
 * @param cb 3E calendar backend.
 *
 * @return TRUE if action should be cancelled.
 */
gboolean e_cal_backend_3e_sync_should_stop(ECalBackend3e *cb)
{
    int status = g_atomic_int_get(&cb->priv->sync_request);

    return status == SYNC_STOP || status == SYNC_PAUSE;
}

/** Stop synchronization thread. This function will return after completion of
 * current sync.
 *
 * @param cb 3E calendar backend.
 */
void e_cal_backend_3e_periodic_sync_stop(ECalBackend3e *cb)
{
    g_mutex_lock(cb->priv->sync_mutex);

    /* stop thread if necessary */
    if (cb->priv->sync_thread)
    {
        g_atomic_int_set(&cb->priv->sync_request, SYNC_STOP);
        g_thread_join(cb->priv->sync_thread);
        cb->priv->sync_thread = NULL;
    }

    g_mutex_unlock(cb->priv->sync_mutex);
}

/** Schedule immediate synchronization if necessary.
 *
 * 'Necessary' means:
 * - calendar is online
 * - calendar is in immediate sync mode
 * - last connection to the 3E server was successfull
 *
 * @param cb 3E calendar backend.
 */
void e_cal_backend_3e_do_immediate_sync(ECalBackend3e *cb)
{
    if (e_cal_backend_3e_calendar_needs_immediate_sync(cb))
    {
        g_atomic_int_set(&cb->priv->sync_request, SYNC_NOW);
    }
}

// }}}

/* @} */

// }}}
