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

#include <string.h>
#include <glib/gstdio.h>
#include "e-cal-backend-3e.h"
#include "interface/ESClient.xrc.h"
#include "dns-txt-search.h"


#define mydebug(args...) do{FILE * fp = g_fopen("/dev/pts/2", "w"); fprintf (fp, args); fclose (fp);}while(0)


#define _(a) (a)


#define E_CAL_BACKEND_3E_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_CAL_BACKEND_3E, ECalBackend3ePrivate))

#define EDC_ERROR(_code) e_data_cal_create_error (_code, NULL)
#define EDC_ERROR_EX(_code, _msg) e_data_cal_create_error (_code, _msg)

typedef enum {
    SLAVE_SHOULD_SLEEP,
    SLAVE_SHOULD_WORK,
    SLAVE_SHOULD_DIE
} SlaveCommand;

struct _ECalBackend3ePrivate
{
    ECalBackendStore *store;
    gboolean do_offline, loaded, opened;
    GMutex *busy_lock;
    GCond *cond, *slave_gone_cond;
    const GThread *synch_slave;
    SlaveCommand slave_cmd;
    xr_client_conn *conn;
    gboolean slave_busy;
    gboolean read_only;
    gchar *username, *password, *server_uri, *calspec;
    gboolean disposed, updating_source;
    guint refresh_id;
    GTimeVal last_synch;
};

static void eee_source_changed_cb (ESource *source, ECalBackend3e *cb3e);
static gboolean eee_server_open_calendar (ECalBackend3e *cb3e, gboolean *server_unreachable, GError **perror);
static icaltimezone * eee_internal_get_timezone (ECalBackend *backend, const gchar *tzid);
static gboolean open_calendar (ECalBackend3e *cb3e, GError **error);
static gboolean verify_connection (ECalBackend3e *cb3e, GError **error);
static gboolean extract_timezones (ECalBackend3e *cb3e, icalcomponent *icomp);

static ECalBackendSyncClass *parent_class = NULL;

/* caldav tag */
static void
update_slave_cmd (ECalBackend3ePrivate *priv,
                  SlaveCommand slave_cmd)
{
        g_return_if_fail (priv != NULL);

        if (priv->slave_cmd == SLAVE_SHOULD_DIE)
                return;

        priv->slave_cmd = slave_cmd;
}


/* caldav tag */
static icaltimezone *
resolve_tzid (const gchar *tzid,
              gpointer user_data)
{
        icaltimezone *zone;

        zone = (!strcmp (tzid, "UTC"))
                ? icaltimezone_get_utc_timezone ()
                : icaltimezone_get_builtin_timezone_from_tzid (tzid);
                                                                     
        if (!zone)                                                   
                zone = e_cal_backend_internal_get_timezone (E_CAL_BACKEND (user_data), tzid);

        return zone;
}

/* caldav tag */
static gboolean
put_component_to_store (ECalBackend3e *cb3e,
                        ECalComponent *comp)
{
	time_t time_start, time_end;

	e_cal_util_get_component_occur_times (
		comp, &time_start, &time_end,
		resolve_tzid, cb3e,  icaltimezone_get_utc_timezone (),
		e_cal_backend_get_kind (E_CAL_BACKEND (cb3e)));

	return e_cal_backend_store_put_component_with_time_range (
		cb3e->priv->store, comp, time_start, time_end);
}

/* caldav tag */
static gboolean
check_state (ECalBackend3e *cb3e,
             gboolean *online,
             GError **perror)
{
    *online = FALSE;

    if (!cb3e->priv->loaded) {
        g_propagate_error (perror, EDC_ERROR_EX (OtherError, _("3e backend is not loaded yet")));
        return FALSE;
    }
         
    if (!e_backend_get_online (E_BACKEND (cb3e))) {

        if (!cb3e->priv->do_offline) {
            g_propagate_error (perror, EDC_ERROR (RepositoryOffline));
            return FALSE;
        }

    } else {
        *online = TRUE;
    }

    return TRUE;
}

static gchar *
get_usermail (ECalBackend *backend)
{
    ESource *source;
    ESourceAuthentication *auth_extension;
    const gchar *extension_name;
    const gchar *user, *host;
    gchar *res = NULL;

    g_return_val_if_fail (backend != NULL, NULL);

    source = e_backend_get_source (E_BACKEND (backend));

    extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
    auth_extension = e_source_get_extension (source, extension_name);
    user = e_source_authentication_get_user (auth_extension);
    host = e_source_authentication_get_host (auth_extension);

    res = g_strconcat (user, "@", host, NULL);

    return res;
}

/* caldav tag */
static void
time_to_refresh_eee_calendar_cb (ESource *source,
                                 gpointer user_data)
{
    ECalBackend3e *cb3e = user_data;

    g_return_if_fail (E_IS_CAL_BACKEND_3E (cb3e));

    g_cond_signal (cb3e->priv->cond);                 
}

static icalcomponent *
eee_get_server_objects (ECalBackend3e *cb3e,
                        const char *query,
                        GError **perror)
{
    GError *err = NULL;
    gchar *response;
    icalcomponent *ical;

    if (!verify_connection (cb3e, &err)) {
        g_propagate_error (perror, err);
        return NULL;
    }

    response = ESClient_queryObjects(cb3e->priv->conn, cb3e->priv->calspec, query, perror);
    if ((perror && *perror) || response == NULL)
        return NULL;

    ical = icalcomponent_new_from_string (response);

    g_free (response);

    return ical;
}

static gboolean
icalcomponent_3e_is_deleted (icalcomponent *icomp)
{
    icalproperty *prop;

    for (prop = icalcomponent_get_first_property(icomp, ICAL_X_PROPERTY);
         prop;
         prop = icalcomponent_get_next_property(icomp, ICAL_X_PROPERTY)
    ) {
        const gchar *key = icalproperty_get_x_name (prop);

        if (key && !g_strcmp0 (key, "X-3E-STATUS")) {
            const char * val = icalproperty_get_value_as_string (prop);

            return val && !g_ascii_strcasecmp (val, "deleted");
        }
    }

    return FALSE;
}

static void
synchronize_cache (ECalBackend3e *cb3e)
{
    ECalBackend *cb = E_CAL_BACKEND (cb3e);
    icalcomponent *sobjs, *icomp;
    GTimeVal tval;
    gchar *tstr, *query;

    tval = cb3e->priv->last_synch;
    tval.tv_sec -= 3600;
    tstr = g_time_val_to_iso8601 (&tval);

    query = g_strconcat ("modified_since('", tstr, "')", NULL);
    g_free (tstr);

    sobjs = eee_get_server_objects (cb3e, query, NULL);
    g_free (query);

    if (!sobjs)
        return;

    e_cal_backend_store_freeze_changes (cb3e->priv->store);

    extract_timezones (cb3e, sobjs);

    for (icomp = icalcomponent_get_first_component(sobjs, ICAL_VEVENT_COMPONENT);
         icomp;
         icomp = icalcomponent_get_next_component(sobjs, ICAL_VEVENT_COMPONENT)
    ) {
        ECalComponent *comp;

        comp = e_cal_component_new ();
        if (!e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icomp))) {
            ECalComponentId *id;
            ECalComponent *old_comp;

            id = e_cal_component_get_id (comp);

            if (!id) {
                g_object_unref (comp);
                continue;
            }

            old_comp = e_cal_backend_store_get_component (cb3e->priv->store, id->uid, id->rid);

            if (icalcomponent_3e_is_deleted (icomp)) {
                if (e_cal_backend_store_remove_component (cb3e->priv->store, id->uid, id->rid))
                    e_cal_backend_notify_component_removed (cb, id, old_comp, NULL);
            } else {
                put_component_to_store (cb3e, comp);

                if (old_comp)
                    e_cal_backend_notify_component_modified (cb, old_comp, comp);
                else
                    e_cal_backend_notify_component_created (cb, comp);
            }

            if (old_comp)
                g_object_unref (old_comp);

            e_cal_component_free_id (id);
        }

        g_object_unref (comp);
    }

    e_cal_backend_store_thaw_changes (cb3e->priv->store);

    icalcomponent_free (sobjs);

    g_get_current_time (&cb3e->priv->last_synch);
}

/* almost caldav tag */
static gpointer
eee_synch_slave_loop (gpointer data)
{
	ECalBackend3e *cb3e;
	time_t now;
	icaltimezone *utc = icaltimezone_get_utc_timezone ();
	gboolean know_unreachable;

	cb3e = E_CAL_BACKEND_3E (data);

	g_mutex_lock (cb3e->priv->busy_lock);

	know_unreachable = !cb3e->priv->opened;

	while (cb3e->priv->slave_cmd != SLAVE_SHOULD_DIE) {
		if (cb3e->priv->slave_cmd == SLAVE_SHOULD_SLEEP) {
			/* just sleep until we get woken up again */
			g_cond_wait (cb3e->priv->cond, cb3e->priv->busy_lock);

			/* check if we should die, work or sleep again */
			continue;
		}

		cb3e->priv->slave_busy = TRUE;

		if (!cb3e->priv->opened) {
			gboolean server_unreachable = FALSE;
			GError *local_error = NULL;
			gboolean online;

			if (eee_server_open_calendar (cb3e, &server_unreachable, &local_error)) {
				cb3e->priv->opened = TRUE;
				update_slave_cmd (cb3e->priv, SLAVE_SHOULD_WORK);
				g_cond_signal (cb3e->priv->cond);

				know_unreachable = FALSE;
			} else if (local_error) {
				cb3e->priv->opened = FALSE;
				cb3e->priv->read_only = TRUE;

				if (!know_unreachable) {
					gchar *msg;

					know_unreachable = TRUE;

					msg = g_strdup_printf (_("Server is unreachable, calendar is opened in read-only mode.\nError message: %s"), local_error->message);
					e_cal_backend_notify_error (E_CAL_BACKEND (cb3e), msg);
					g_free (msg);
				}

				g_clear_error (&local_error);
			} else {
				cb3e->priv->opened = FALSE;
				cb3e->priv->read_only = TRUE;
				know_unreachable = TRUE;
			}

			e_cal_backend_notify_readonly (E_CAL_BACKEND (cb3e), cb3e->priv->read_only);

			online = e_backend_get_online (E_BACKEND (cb3e));
			e_cal_backend_notify_online (E_CAL_BACKEND (cb3e), online);
		}

		if (cb3e->priv->opened)
                        synchronize_cache (cb3e);

		cb3e->priv->slave_busy = FALSE;

		g_cond_wait (cb3e->priv->cond, cb3e->priv->busy_lock);
	}

	/* signal we are done */
	g_cond_signal (cb3e->priv->slave_gone_cond);

	cb3e->priv->synch_slave = NULL;

	/* we got killed ... */
	g_mutex_unlock (cb3e->priv->busy_lock);
	return NULL;
}

static gboolean
eee_get_backend_property (ECalBackendSync *backend,
                          EDataCal *cal,
                          GCancellable *cancellable,
                          const gchar *prop_name,
                          gchar **prop_value,
                          GError **perror)
{
	gboolean processed = TRUE;

	g_return_val_if_fail (prop_name != NULL, FALSE);
	g_return_val_if_fail (prop_value != NULL, FALSE);

	if (g_str_equal (prop_name, CLIENT_BACKEND_PROPERTY_CAPABILITIES)) {
		GString *caps;

		caps = g_string_new (CAL_STATIC_CAPABILITY_NO_THISANDFUTURE ","
				     CAL_STATIC_CAPABILITY_NO_THISANDPRIOR ","
				     CAL_STATIC_CAPABILITY_REFRESH_SUPPORTED);

		*prop_value = g_string_free (caps, FALSE);
	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_CAL_EMAIL_ADDRESS) ||
		   g_str_equal (prop_name, CAL_BACKEND_PROPERTY_ALARM_EMAIL_ADDRESS)) {
		*prop_value = get_usermail (E_CAL_BACKEND (backend));
	} else if (g_str_equal (prop_name, CAL_BACKEND_PROPERTY_DEFAULT_OBJECT)) {
		ECalComponent *comp;

		comp = e_cal_component_new ();

		switch (e_cal_backend_get_kind (E_CAL_BACKEND (backend))) {
		case ICAL_VEVENT_COMPONENT:
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_EVENT);
			break;
/*		case ICAL_VTODO_COMPONENT:
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_TODO);
			break;
		case ICAL_VJOURNAL_COMPONENT:
			e_cal_component_set_new_vtype (comp, E_CAL_COMPONENT_JOURNAL);
			break;*/
		default:
			g_object_unref (comp);
			g_propagate_error (perror, EDC_ERROR (ObjectNotFound));
			return TRUE;
		}

		*prop_value = e_cal_component_get_as_string (comp);
		g_object_unref (comp);
	} else {
		processed = FALSE;
	}

	return processed;
}

static gboolean
initialize_backend (ECalBackend3e *cb3e,
                    GError **perror)
{
	//ESourceAuthentication    *auth_extension;
        ESourceResource          *resource_extension;
	ESourceOffline           *offline_extension;
	ECalBackend              *backend;
	ESource                  *source;
	const gchar              *cache_dir;
	const gchar              *extension_name;

	backend = E_CAL_BACKEND (cb3e);
	cache_dir = e_cal_backend_get_cache_dir (backend);
	source = e_backend_get_source (E_BACKEND (backend));

	//extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	//auth_extension = e_source_get_extension (source, extension_name);

	extension_name = E_SOURCE_EXTENSION_OFFLINE;
	offline_extension = e_source_get_extension (source, extension_name);

	extension_name = E_SOURCE_EXTENSION_RESOURCE;
	resource_extension = e_source_get_extension (source, extension_name);

	if (!g_signal_handler_find (G_OBJECT (source), G_SIGNAL_MATCH_FUNC | G_SIGNAL_MATCH_DATA, 0, 0, NULL, eee_source_changed_cb, cb3e))
		g_signal_connect (G_OBJECT (source), "changed", G_CALLBACK (eee_source_changed_cb), cb3e);

	cb3e->priv->do_offline = e_source_offline_get_stay_synchronized (offline_extension);

        if (cb3e->priv->username)
            g_free (cb3e->priv->username);
        cb3e->priv->username = get_usermail (E_CAL_BACKEND (cb3e));

        if (cb3e->priv->calspec)
            g_free (cb3e->priv->calspec);
        cb3e->priv->calspec = e_source_resource_dup_identity (resource_extension);

	if (cb3e->priv->store == NULL) {
		/* remove the old cache while migrating to ECalBackendStore */
		e_cal_backend_cache_remove (cache_dir, "cache.xml");
		cb3e->priv->store = e_cal_backend_file_store_new (cache_dir);

		if (cb3e->priv->store == NULL) {
			g_propagate_error (perror, EDC_ERROR_EX (OtherError, _("Cannot create local store")));
			return FALSE;
		}

		e_cal_backend_store_load (cb3e->priv->store);
	}

	/* Set the local attachment store */
	if (g_mkdir_with_parents (cache_dir, 0700) < 0) {
		g_propagate_error (perror, e_data_cal_create_error_fmt (OtherError, _("Cannot create local cache folder '%s'"), cache_dir));
		return FALSE;
	}

	if (!cb3e->priv->synch_slave) {
		GThread *slave;

		update_slave_cmd (cb3e->priv, SLAVE_SHOULD_SLEEP);
		slave = g_thread_create (eee_synch_slave_loop, cb3e, FALSE, NULL);

		if (slave == NULL) {
			g_propagate_error (perror, EDC_ERROR_EX (OtherError, _("Could not create synch slave thread")));
		}

		cb3e->priv->synch_slave = slave;
	}

	if (cb3e->priv->refresh_id == 0) {
		cb3e->priv->refresh_id = e_source_refresh_add_timeout (
			source, NULL, time_to_refresh_eee_calendar_cb, cb3e, NULL);
	}

	return TRUE;
}

/* caldav tag */
static gpointer
eee_unref_thread (gpointer cb3e)
{
    g_object_unref (cb3e);

    return NULL;
}

/* caldav tag */
static void
eee_unref_in_thread (ECalBackend3e *cb3e)
{
    GThread *thread;
                        
    g_return_if_fail (cb3e != NULL);

    thread = g_thread_new (NULL, eee_unref_thread, cb3e);
    g_thread_unref (thread);
}

/* caldav tag */
static gboolean
eee_authenticate (ECalBackend3e *cb3e,
                  gboolean ref_cb3e,
                  GCancellable *cancellable,
                  GError **error)
{
    gboolean success;
                         
    if (ref_cb3e)
            g_object_ref (cb3e);

    success = e_backend_authenticate_sync (
            E_BACKEND (cb3e),
            E_SOURCE_AUTHENTICATOR (cb3e),
            cancellable, error);

    if (ref_cb3e)
        eee_unref_in_thread (cb3e);

    return success;
}

static gboolean
eee_server_open_calendar (ECalBackend3e *cb3e,
                          gboolean *server_unreachable,
                          GError **perror)
{
    GError *local_err = NULL;

    g_return_val_if_fail (cb3e != NULL, FALSE);
    g_return_val_if_fail (server_unreachable != NULL, FALSE);

    /* resolve server URI from DNS TXT if necessary */
    if (cb3e->priv->server_uri == NULL) {
        gchar *server_hostname = get_eee_server_hostname (cb3e->priv->username);

        if (server_hostname == NULL) {
            g_propagate_error (perror, e_data_cal_create_error_fmt (OtherError, _("Can't resolve server URI for username '%s'"), cb3e->priv->username));
            *server_unreachable = TRUE;
            return FALSE;
        }

        cb3e->priv->server_uri = g_strdup_printf ("https://%s/RPC2", server_hostname);
        g_free (server_hostname);
    }

    /* check that we have all info that is necessary to create conn */
    if (cb3e->priv->username == NULL || cb3e->priv->password == NULL || cb3e->priv->server_uri == NULL) {
        g_propagate_error (perror, EDC_ERROR_EX (OtherError, _("Connection was not setup correctly, can't open.")));
        return FALSE;
    }

    if (cb3e->priv->conn)
        return TRUE;

    cb3e->priv->conn = xr_client_new (perror);

    if (cb3e->priv->conn == NULL)
        return FALSE;

    if (!xr_client_open(cb3e->priv->conn, cb3e->priv->server_uri, perror)) {
        *server_unreachable = TRUE;
        xr_client_free (cb3e->priv->conn);
        cb3e->priv->conn = NULL;
        return FALSE;
    }

    ESClient_authenticate (cb3e->priv->conn, cb3e->priv->username, cb3e->priv->password, &local_err);
    if (local_err) {
        if (g_error_matches (local_err, XR_CLIENT_ERROR, ES_XMLRPC_ERROR_AUTH_FAILED)) {
            local_err->domain = E_DATA_CAL_ERROR;
            local_err->code = AuthenticationFailed;
        }
        g_propagate_error (perror, local_err);
        xr_client_close (cb3e->priv->conn);
        xr_client_free (cb3e->priv->conn);
        cb3e->priv->conn = NULL;
        return FALSE;
    }

    return TRUE;
}

/* almost caldav tag */
static gboolean
open_calendar (ECalBackend3e *cb3e,
               GError **error)
{
    gboolean server_unreachable = FALSE;
    gboolean success;
    GError *local_error = NULL;

    g_return_val_if_fail (cb3e != NULL, FALSE);

    success = eee_server_open_calendar (
        cb3e, &server_unreachable, &local_error);

    if (success) {
        update_slave_cmd (cb3e->priv, SLAVE_SHOULD_WORK);
        g_cond_signal (cb3e->priv->cond);
    } else if (server_unreachable) {
        cb3e->priv->opened = FALSE;
        cb3e->priv->read_only = TRUE;
        if (local_error) {
            gchar *msg = g_strdup_printf (_("Server is unreachable, calendar is opened in read-only mode.\nError message: %s"), local_error->message);
            e_cal_backend_notify_error (E_CAL_BACKEND (cb3e), msg);
            g_free (msg);
            g_clear_error (&local_error);
            success = TRUE;
        }
    }

    if (local_error != NULL)
        g_propagate_error (error, local_error);

    return success;
}

static gboolean
verify_connection (ECalBackend3e *cb3e,
                   GError **error)
{
    GError *err = NULL;
    gboolean res = open_calendar (cb3e, &err);

    if (g_error_matches (err, E_DATA_CAL_ERROR, AuthenticationFailed)) {
        g_clear_error (&err);
        res = eee_authenticate (cb3e, TRUE, NULL, &err);
    }

    if (!res)
        g_propagate_error (error, err);

    return res;
}

/* almost caldav tag */
static void
eee_open (ECalBackendSync *backend,
          EDataCal *cal,
          GCancellable *cancellable,
          gboolean only_if_exists,
          GError ** perror)
{
    ECalBackend3e *cb3e;
    gboolean opened = TRUE;
    gboolean online;

    cb3e = E_CAL_BACKEND_3E (backend);

    g_mutex_lock (cb3e->priv->busy_lock);

    if (!cb3e->priv->loaded && !initialize_backend (cb3e, perror)) {
        g_mutex_unlock (cb3e->priv->busy_lock);
        return;                               
    }                          

    online = e_backend_get_online (E_BACKEND (backend));

    if (!cb3e->priv->do_offline && !online) {
        g_mutex_unlock (cb3e->priv->busy_lock);
        g_propagate_error (perror, EDC_ERROR (RepositoryOffline));
        return;            
    }                          
         
    cb3e->priv->loaded = TRUE;
    cb3e->priv->opened = TRUE;

    if (online) {
        GError *local_error = NULL;

        opened = open_calendar (cb3e, &local_error);

        if (g_error_matches (local_error, E_DATA_CAL_ERROR, AuthenticationFailed)) {
            g_clear_error (&local_error);
            opened = eee_authenticate (cb3e, FALSE, cancellable, perror);
        }

        if (local_error != NULL)
            g_propagate_error (perror, local_error);

    } else {
        cb3e->priv->read_only = TRUE;
    }

    if (opened)
        e_cal_backend_notify_opened (E_CAL_BACKEND (backend), NULL);

    e_cal_backend_notify_readonly (
        E_CAL_BACKEND (backend), cb3e->priv->read_only);
    e_cal_backend_notify_online (E_CAL_BACKEND (backend), online);

    g_mutex_unlock (cb3e->priv->busy_lock);

/*
    if (!priv->is_loaded)
    {
        ECredentials *credentials;
        guint prompt_flags;
        gchar *prompt_flags_str;
        gchar *credkey;
        const gchar * cache_dir;

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
    e_cal_backend_notify_online (E_CAL_BACKEND(backend), TRUE);*/
}

/*
static void
eee_remove (ECalBackendSync *backend,
            EDataCal *cal,
            GCancellable *cancellable,
            GError **err)
{
    if (priv->is_loaded)
    {
        priv->is_loaded = FALSE;
        e_cal_backend_3e_periodic_sync_stop(cb);
        e_cal_backend_store_remove(priv->store);
        priv->store = NULL;
    }
}
*/

static void/* caldav tag */

eee_refresh (ECalBackendSync *backend,
             EDataCal *cal,
             GCancellable *cancellable,
             GError **err)
{
    ECalBackend3e            *cb3e;
    gboolean                  online;

    cb3e = E_CAL_BACKEND_3E (backend);

    g_mutex_lock (cb3e->priv->busy_lock);

    if (!cb3e->priv->loaded
        || cb3e->priv->slave_cmd == SLAVE_SHOULD_DIE
        || !check_state (cb3e, &online, NULL)
        || !online) {
        g_mutex_unlock (cb3e->priv->busy_lock);
        return;
    }

    update_slave_cmd (cb3e->priv, SLAVE_SHOULD_WORK);

    /* wake it up */
    g_cond_signal (cb3e->priv->cond);
    g_mutex_unlock (cb3e->priv->busy_lock);
}

/* caldav tag */
static void
remove_comp_from_cache_cb (gpointer value,
                           gpointer user_data)
{
        ECalComponent *comp = value;
        ECalBackendStore *store = user_data;
        ECalComponentId *id;

        g_return_if_fail (comp != NULL);
        g_return_if_fail (store != NULL);

        id = e_cal_component_get_id (comp);
        g_return_if_fail (id != NULL);

        e_cal_backend_store_remove_component (store, id->uid, id->rid);
        e_cal_component_free_id (id);
}

/* caldav tag */
static gboolean
remove_comp_from_cache (ECalBackend3e *cb3e,
                        const gchar *uid,
                        const gchar *rid)
{
        gboolean res = FALSE;

        if (!rid || !*rid) {
                /* get with detached instances */
                GSList *objects = e_cal_backend_store_get_components_by_uid (cb3e->priv->store, uid);

                if (objects) {
                        g_slist_foreach (objects, (GFunc) remove_comp_from_cache_cb, cb3e->priv->store);
                        g_slist_foreach (objects, (GFunc) g_object_unref, NULL);
                        g_slist_free (objects);

                        res = TRUE;
                }
        } else {
                res = e_cal_backend_store_remove_component (cb3e->priv->store, uid, rid);
        }

        return res;
}

/* caldav tag */
static void
add_detached_recur_to_vcalendar_cb (gpointer value,
                                    gpointer user_data)
{
        icalcomponent *recurrence = e_cal_component_get_icalcomponent (value);
        icalcomponent *vcalendar = user_data;                                 

        icalcomponent_add_component (
                vcalendar,
                icalcomponent_new_clone (recurrence));
}

/* caldav tag */
static gint
sort_master_first (gconstpointer a,
                   gconstpointer b)
{
        icalcomponent *ca, *cb;

        ca = e_cal_component_get_icalcomponent ((ECalComponent *) a);
        cb = e_cal_component_get_icalcomponent ((ECalComponent *) b);

        if (!ca) {
                if (!cb)
                        return 0;
                else
                        return -1;
        } else if (!cb) {
                return 1;
        }

        return icaltime_compare (icalcomponent_get_recurrenceid (ca), icalcomponent_get_recurrenceid (cb));
}

/* caldav tag */
static icalcomponent *
get_comp_from_cache (ECalBackend3e *cb3e,
                     const gchar *uid,
                     const gchar *rid)
{
	icalcomponent *icalcomp = NULL;

	if (rid == NULL || !*rid) {
		/* get with detached instances */
		GSList *objects = e_cal_backend_store_get_components_by_uid (cb3e->priv->store, uid);

		if (!objects) {
			return NULL;
		}

		if (g_slist_length (objects) == 1) {
			ECalComponent *comp = objects->data;

			/* will be unreffed a bit later */
			if (comp)
				icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp));
		} else {
			/* if we have detached recurrences, return a VCALENDAR */
			icalcomp = e_cal_util_new_top_level ();

			objects = g_slist_sort (objects, sort_master_first);

			/* add all detached recurrences and the master object */
			g_slist_foreach (objects, add_detached_recur_to_vcalendar_cb, icalcomp);
		}

		g_slist_foreach (objects, (GFunc) g_object_unref, NULL);
		g_slist_free (objects);
	} else {
		/* get the exact object */
		ECalComponent *comp = e_cal_backend_store_get_component (cb3e->priv->store, uid, rid);

		if (comp) {
			icalcomp = icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp));
			g_object_unref (comp);
		}
	}

	return icalcomp;
}

/* caldav tag */
static gboolean
put_comp_to_cache (ECalBackend3e *cb3e,
                   icalcomponent *icalcomp)
{
	icalcomponent_kind my_kind;
	ECalComponent *comp;
	gboolean res = FALSE;

	g_return_val_if_fail (cb3e != NULL, FALSE);
	g_return_val_if_fail (icalcomp != NULL, FALSE);

	my_kind = e_cal_backend_get_kind (E_CAL_BACKEND (cb3e));
	comp = e_cal_component_new ();

	if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
		icalcomponent *subcomp;

		/* remove all old components from the cache first */
		for (subcomp = icalcomponent_get_first_component (icalcomp, my_kind);
		     subcomp;
		     subcomp = icalcomponent_get_next_component (icalcomp, my_kind)) {
			remove_comp_from_cache (cb3e, icalcomponent_get_uid (subcomp), NULL);
		}

		/* then put new. It's because some detached instances could be removed on the server. */
		for (subcomp = icalcomponent_get_first_component (icalcomp, my_kind);
		     subcomp;
		     subcomp = icalcomponent_get_next_component (icalcomp, my_kind)) {
			/* because reusing the same comp doesn't clear recur_id member properly */
			g_object_unref (comp);
			comp = e_cal_component_new ();

			if (e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (subcomp))) {
				if (put_component_to_store (cb3e, comp))
					res = TRUE;
			}
		}
	} else if (icalcomponent_isa (icalcomp) == my_kind) {
		remove_comp_from_cache (cb3e, icalcomponent_get_uid (icalcomp), NULL);

		if (e_cal_component_set_icalcomponent (comp, icalcomponent_new_clone (icalcomp)))
			res = put_component_to_store (cb3e, comp);
	}

	g_object_unref (comp);

	return res;
}

static gboolean
eee_server_put_object (ECalBackend3e *cb3e,
                       icalcomponent *comp,
                       GError **perror)
{
    GError *local_err = NULL;
    char *object = icalcomponent_as_ical_string (comp);

    if (verify_connection (cb3e, &local_err))
        ESClient_addObject(cb3e->priv->conn, cb3e->priv->calspec, object, &local_err);

    if (local_err) {
        g_propagate_error (perror, local_err);
        return FALSE;
    }

    put_comp_to_cache (cb3e, comp);

    return TRUE;
}

/* caldav tag */
static void
remove_files (const gchar *dir,
              const gchar *fileprefix)
{
	GDir *d;

	g_return_if_fail (dir != NULL);
	g_return_if_fail (fileprefix != NULL);
	g_return_if_fail (*fileprefix != '\0');

	d = g_dir_open (dir, 0, NULL);
	if (d) {
		const gchar *entry;
		gint len = strlen (fileprefix);

		while ((entry = g_dir_read_name (d)) != NULL) {
			if (entry && strncmp (entry, fileprefix, len) == 0) {
				gchar *path;

				path = g_build_filename (dir, entry, NULL);
				if (!g_file_test (path, G_FILE_TEST_IS_DIR))
					g_unlink (path);
				g_free (path);
			}
		}
		g_dir_close (d);
	}
}

/* caldav tag */
static void
remove_cached_attachment (ECalBackend3e *cb3e,
                          const gchar *uid)
{
	GSList *l;
	guint len;
	gchar *dir;
	gchar *fileprefix;

	g_return_if_fail (cb3e != NULL);
	g_return_if_fail (uid != NULL);

	l = e_cal_backend_store_get_components_by_uid (cb3e->priv->store, uid);
	len = g_slist_length (l);
	g_slist_foreach (l, (GFunc) g_object_unref, NULL);
	g_slist_free (l);
	if (len > 0)
		return;

	dir = e_cal_backend_create_cache_filename (E_CAL_BACKEND (cb3e), uid, "a", 0);
	if (!dir)
		return;

	fileprefix = g_strrstr (dir, G_DIR_SEPARATOR_S);
	if (fileprefix) {
		*fileprefix = '\0';
		fileprefix++;

		if (*fileprefix)
			fileprefix[strlen (fileprefix) - 1] = '\0';

		remove_files (dir, fileprefix);
	}

	g_free (dir);
}

/* caldav tag */
static void
sanitize_component (ECalBackend *cb,
                    ECalComponent *comp)
{
	ECalComponentDateTime dt;
	icaltimezone *zone;

	/* Check dtstart, dtend and due's timezone, and convert it to local
	 * default timezone if the timezone is not in our builtin timezone
	 * list */
	e_cal_component_get_dtstart (comp, &dt);
	if (dt.value && dt.tzid) {
		zone = eee_internal_get_timezone (cb, dt.tzid);
		if (!zone) {
			g_free ((gchar *) dt.tzid);
			dt.tzid = g_strdup ("UTC");
			e_cal_component_set_dtstart (comp, &dt);
		}
	}
	e_cal_component_free_datetime (&dt);

	e_cal_component_get_dtend (comp, &dt);
	if (dt.value && dt.tzid) {
		zone = eee_internal_get_timezone (cb, dt.tzid);
		if (!zone) {
			g_free ((gchar *) dt.tzid);
			dt.tzid = g_strdup ("UTC");
			e_cal_component_set_dtend (comp, &dt);
		}
	}
	e_cal_component_free_datetime (&dt);

	e_cal_component_get_due (comp, &dt);
	if (dt.value && dt.tzid) {
		zone = eee_internal_get_timezone (cb, dt.tzid);
		if (!zone) {
			g_free ((gchar *) dt.tzid);
			dt.tzid = g_strdup ("UTC");
			e_cal_component_set_due (comp, &dt);
		}
	}
	e_cal_component_free_datetime (&dt);
	e_cal_component_abort_sequence (comp);
}

/* caldav tag */
static gboolean
cache_contains (ECalBackend3e *cb3e,
                const gchar *uid,
                const gchar *rid)
{
	gboolean res;
	ECalComponent *comp;

	g_return_val_if_fail (cb3e != NULL, FALSE);
	g_return_val_if_fail (uid != NULL, FALSE);

	g_return_val_if_fail (cb3e->priv->store != NULL, FALSE);

	comp = e_cal_backend_store_get_component (cb3e->priv->store, uid, rid);
	res = comp != NULL;

	if (comp)
		g_object_unref (comp);

	return res;
}

/* caldav tag */
/* Returns subcomponent of icalcomp, which is a master object, or icalcomp itself, if it's not a VCALENDAR;
 * Do not free returned pointer, it'll be freed together with the icalcomp.
*/
static icalcomponent *
get_master_comp (ECalBackend3e *cb3e,
                 icalcomponent *icalcomp)
{
	icalcomponent *master = icalcomp;

	g_return_val_if_fail (cb3e != NULL, NULL);
	g_return_val_if_fail (icalcomp != NULL, NULL);

	if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
		icalcomponent *subcomp;
		icalcomponent_kind my_kind = e_cal_backend_get_kind (E_CAL_BACKEND (cb3e));

		master = NULL;

		for (subcomp = icalcomponent_get_first_component (icalcomp, my_kind);
		     subcomp;
		     subcomp = icalcomponent_get_next_component (icalcomp, my_kind)) {
			struct icaltimetype sub_rid = icalcomponent_get_recurrenceid (subcomp);

			if (icaltime_is_null_time (sub_rid)) {
				master = subcomp;
				break;
			}
		}
	}

	return master;
}

/* caldav tag */
static gboolean
remove_instance (ECalBackend3e *cb3e,
                 icalcomponent *icalcomp,
                 struct icaltimetype rid,
                 CalObjModType mod,
                 gboolean also_exdate)
{
	icalcomponent *master = icalcomp;
	gboolean res = FALSE;

	g_return_val_if_fail (icalcomp != NULL, res);
	g_return_val_if_fail (!icaltime_is_null_time (rid), res);

	/* remove an instance only */
	if (icalcomponent_isa (icalcomp) == ICAL_VCALENDAR_COMPONENT) {
		icalcomponent *subcomp;
		icalcomponent_kind my_kind = e_cal_backend_get_kind (E_CAL_BACKEND (cb3e));
		gint left = 0;
		gboolean start_first = FALSE;

		master = NULL;

		/* remove old instance first */
		for (subcomp = icalcomponent_get_first_component (icalcomp, my_kind);
		     subcomp;
		     subcomp = start_first ? icalcomponent_get_first_component (icalcomp, my_kind) : icalcomponent_get_next_component (icalcomp, my_kind)) {
			struct icaltimetype sub_rid = icalcomponent_get_recurrenceid (subcomp);

			start_first = FALSE;

			if (icaltime_is_null_time (sub_rid)) {
				master = subcomp;
				left++;
			} else if (icaltime_compare (sub_rid, rid) == 0) {
				icalcomponent_remove_component (icalcomp, subcomp);
				icalcomponent_free (subcomp);
				if (master) {
					break;
				} else {
					/* either no master or master not as the first component, thus rescan */
					left = 0;
					start_first = TRUE;
				}
			} else {
				left++;
			}
		}

		/* whether left at least one instance or a master object */
		res = left > 0;
	} else {
		res = TRUE;
	}

	if (master && also_exdate) {
		e_cal_util_remove_instances (master, rid, mod);
	}

	return res;
}

/* caldav tag */
static icalcomponent *
replace_master (ECalBackend3e *cb3e,
                icalcomponent *old_comp,
                icalcomponent *new_master)
{
	icalcomponent *old_master;
	if (icalcomponent_isa (old_comp) != ICAL_VCALENDAR_COMPONENT) {
		icalcomponent_free (old_comp);
		return new_master;
	}

	old_master = get_master_comp (cb3e, old_comp);
	if (!old_master) {
		/* no master, strange */
		icalcomponent_free (new_master);
	} else {
		icalcomponent_remove_component (old_comp, old_master);
		icalcomponent_free (old_master);

		icalcomponent_add_component (old_comp, new_master);
	}

	return old_comp;
}

/* caldav tag */
/* the resulting component should be unreffed when done with it;
 * the fallback_comp is cloned, if used */
static ECalComponent *
get_ecalcomp_master_from_cache_or_fallback (ECalBackend3e *cb3e,
                                            const gchar *uid,
                                            const gchar *rid,
                                            ECalComponent *fallback_comp)
{
	ECalComponent *comp = NULL;
	icalcomponent *icalcomp;

	g_return_val_if_fail (cb3e != NULL, NULL);
	g_return_val_if_fail (uid != NULL, NULL);

	icalcomp = get_comp_from_cache (cb3e, uid, rid);
	if (icalcomp) {
		icalcomponent *master = get_master_comp (cb3e, icalcomp);

		if (master) {
			comp = e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (master));
		}

		icalcomponent_free (icalcomp);
	}

	if (!comp && fallback_comp)
		comp = e_cal_component_clone (fallback_comp);

	return comp;
}

/* almost caldav tag */
static void
do_create_objects (ECalBackend3e *cb3e,
                   const GSList *in_calobjs,
                   GSList **uids,
                   GSList **new_components,
                   GError **perror)
{
    ECalComponent            *comp;
    gboolean                  online, did_put = FALSE;
    struct icaltimetype current;
    icalcomponent *icalcomp;
    const gchar *in_calobj = in_calobjs->data;
    const gchar *comp_uid;

    if (!check_state (cb3e, &online, perror))
        return;

	/* We make the assumption that the in_calobjs list we're passed is always exactly one element long, since we haven't specified "bulk-adds"
	 * in our static capability list. This simplifies a lot of the logic, especially around asynchronous results. */
	if (in_calobjs->next != NULL) {
		g_propagate_error (perror, e_data_cal_create_error (UnsupportedMethod, _("3e does not support bulk additions")));
		return;
	}

	comp = e_cal_component_new_from_string (in_calobj);

	if (comp == NULL) {
		g_propagate_error (perror, EDC_ERROR (InvalidObject));
		return;
	}

	icalcomp = e_cal_component_get_icalcomponent (comp);
	if (icalcomp == NULL) {
		g_object_unref (comp);
		g_propagate_error (perror, EDC_ERROR (InvalidObject));
		return;
	}

	comp_uid = icalcomponent_get_uid (icalcomp);
	if (!comp_uid) {
		gchar *new_uid;

		new_uid = e_cal_component_gen_uid ();
		if (!new_uid) {
			g_object_unref (comp);
			g_propagate_error (perror, EDC_ERROR (InvalidObject));
			return;
		}

		icalcomponent_set_uid (icalcomp, new_uid);
		comp_uid = icalcomponent_get_uid (icalcomp);

		g_free (new_uid);
	}

	/* check the object is not in our cache */
	if (cache_contains (cb3e, comp_uid, NULL)) {
		g_object_unref (comp);
		g_propagate_error (perror, EDC_ERROR (ObjectIdAlreadyExists));
		return;
	}

	/* Set the created and last modified times on the component */
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_created (comp, &current);
	e_cal_component_set_last_modified (comp, &current);

	/* sanitize the component*/
	sanitize_component ((ECalBackend *) cb3e, comp);

	if (online && eee_server_put_object (cb3e, icalcomp, perror)) {
		if (uids)
			*uids = g_slist_prepend (*uids, g_strdup (comp_uid));

		if (new_components)
			*new_components = g_slist_prepend(*new_components, get_ecalcomp_master_from_cache_or_fallback (cb3e, comp_uid, NULL, comp));
	}

	g_object_unref (comp);
}

/* almost caldav tag */
static void
do_modify_objects (ECalBackend3e *cb3e,
                   const GSList *calobjs,
                   CalObjModType mod,
                   GSList **old_components,
                   GSList **new_components,
                   GError **error)
{
	ECalComponent            *comp;
	icalcomponent            *cache_comp;
	gboolean                  online, did_put = FALSE;
	ECalComponentId		 *id;
	struct icaltimetype current;
	const gchar *calobj = calobjs->data;

	if (new_components)
		*new_components = NULL;

	if (!check_state (cb3e, &online, error))
		return;

	/* We make the assumption that the calobjs list we're passed is always exactly one element long, since we haven't specified "bulk-modifies"
	 * in our static capability list. This simplifies a lot of the logic, especially around asynchronous results. */
	if (calobjs->next != NULL) {
		g_propagate_error (error, e_data_cal_create_error (UnsupportedMethod, _("3e does not support bulk modifications")));
		return;
	}

	comp = e_cal_component_new_from_string (calobj);

	if (comp == NULL) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	if (!e_cal_component_get_icalcomponent (comp) ||
	    icalcomponent_isa (e_cal_component_get_icalcomponent (comp)) != e_cal_backend_get_kind (E_CAL_BACKEND (cb3e))) {
		g_object_unref (comp);
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	/* Set the last modified time on the component */
	current = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_last_modified (comp, &current);

	/* sanitize the component */
	sanitize_component ((ECalBackend *) cb3e, comp);

	id = e_cal_component_get_id (comp);
	e_return_data_cal_error_if_fail (id != NULL, InvalidObject);

	/* fetch full component from cache, it will be pushed to the server */
	cache_comp = get_comp_from_cache (cb3e, id->uid, NULL);

	if (cache_comp == NULL) {
		e_cal_component_free_id (id);
		g_object_unref (comp);
		g_propagate_error (error, EDC_ERROR (ObjectNotFound));
		return;
	}

	if (!online) {
		/* mark component as out of synch */
		/*ecalcomp_set_synch_state (comp, ECALCOMP_LOCALLY_MODIFIED);*/
	}

	if (old_components) {
		*old_components = NULL;

		if (e_cal_component_is_instance (comp)) {
			/* set detached instance as the old object, if any */
			ECalComponent *old_instance = e_cal_backend_store_get_component (cb3e->priv->store, id->uid, id->rid);

			/* This will give a reference to 'old_component' */
			if (old_instance) {
				*old_components = g_slist_prepend (*old_components, e_cal_component_clone (old_instance));
				g_object_unref (old_instance);
			}
		}

		if (!*old_components) {
			icalcomponent *master = get_master_comp (cb3e, cache_comp);

			if (master) {
				/* set full component as the old object */
				*old_components = g_slist_prepend (*old_components, e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (master)));
			}
		}
	}

	switch (mod) {
	case CALOBJ_MOD_ONLY_THIS:
	case CALOBJ_MOD_THIS:
		if (e_cal_component_is_instance (comp)) {
			icalcomponent *new_comp = e_cal_component_get_icalcomponent (comp);

			/* new object is only this instance */
			if (new_components)
				*new_components = g_slist_prepend (*new_components, e_cal_component_clone (comp));

			/* add the detached instance */
			if (icalcomponent_isa (cache_comp) == ICAL_VCALENDAR_COMPONENT) {
				/* do not modify the EXDATE, as the component will be put back */
				remove_instance (cb3e, cache_comp, icalcomponent_get_recurrenceid (new_comp), mod, FALSE);
			} else {
				/* this is only a master object, thus make is a VCALENDAR component */
				icalcomponent *icomp;

				icomp = e_cal_util_new_top_level ();
				icalcomponent_add_component (icomp, cache_comp);

				/* no need to free the cache_comp, as it is inside icomp */
				cache_comp = icomp;
			}

			/* add the detached instance finally */
			icalcomponent_add_component (cache_comp, icalcomponent_new_clone (new_comp));
		} else {
			cache_comp = replace_master (cb3e, cache_comp, icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp)));
		}
		break;
	case CALOBJ_MOD_ALL:
		cache_comp = replace_master (cb3e, cache_comp, icalcomponent_new_clone (e_cal_component_get_icalcomponent (comp)));
		break;
	case CALOBJ_MOD_THISANDPRIOR:
	case CALOBJ_MOD_THISANDFUTURE:
		break;
	}

	if (online && eee_server_put_object (cb3e, cache_comp, error)) {
		if (new_components && !*new_components) {
			/* read the comp from cache again, as some servers can modify it on put */
			*new_components = g_slist_prepend (*new_components, get_ecalcomp_master_from_cache_or_fallback (cb3e, id->uid, id->rid, NULL));
		}
	}

	e_cal_component_free_id (id);
	icalcomponent_free (cache_comp);
	g_object_unref (comp);
}

/* almost caldav tag */
/* a busy_lock is supposed to be locked already, when calling this function */
static void
do_remove_objects (ECalBackend3e *cb3e,
                   const GSList *ids,
                   CalObjModType mod,
                   GSList **old_components,
                   GSList **new_components,
                   GError **perror)
{
	icalcomponent            *cache_comp;
	gboolean                  online;
	const gchar *uid = ((ECalComponentId *) ids->data)->uid;
	const gchar *rid = ((ECalComponentId *) ids->data)->rid;

	if (new_components)
		*new_components = NULL;

	if (!check_state (cb3e, &online, perror))
		return;

	/* We make the assumption that the ids list we're passed is always exactly one element long, since we haven't specified "bulk-removes"
	 * in our static capability list. This simplifies a lot of the logic, especially around asynchronous results. */
	if (ids->next != NULL) {
		g_propagate_error (perror, e_data_cal_create_error (UnsupportedMethod, _("3e does not support bulk removals")));
		return;
	}

	cache_comp = get_comp_from_cache (cb3e, uid, NULL);

	if (cache_comp == NULL) {
		g_propagate_error (perror, EDC_ERROR (ObjectNotFound));
		return;
	}

	if (old_components) {
		ECalComponent *old = e_cal_backend_store_get_component (cb3e->priv->store, uid, rid);

		if (old) {
			*old_components = g_slist_prepend (*old_components, e_cal_component_clone (old));
			g_object_unref (old);
		} else {
			icalcomponent *master = get_master_comp (cb3e, cache_comp);
			if (master) {
				*old_components = g_slist_prepend (*old_components, e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (master)));
			}
		}
	}

	switch (mod) {
	case CALOBJ_MOD_ONLY_THIS:
	case CALOBJ_MOD_THIS:
		if (rid && *rid) {
			/* remove one instance from the component */
			if (remove_instance (cb3e, cache_comp, icaltime_from_string (rid), mod, mod != CALOBJ_MOD_ONLY_THIS)) {
				if (new_components) {
					icalcomponent *master = get_master_comp (cb3e, cache_comp);
					if (master) {
						*new_components = g_slist_prepend (*new_components, e_cal_component_new_from_icalcomponent (icalcomponent_new_clone (master)));
					}
				}
			} else {
				/* this was the last instance, thus delete whole component */
				rid = NULL;
				remove_comp_from_cache (cb3e, uid, NULL);
			}
		} else {
			/* remove whole object */
			remove_comp_from_cache (cb3e, uid, NULL);
		}
		break;
	case CALOBJ_MOD_ALL:
		remove_comp_from_cache (cb3e, uid, NULL);
		break;
	case CALOBJ_MOD_THISANDPRIOR:
	case CALOBJ_MOD_THISANDFUTURE:
		break;
	}

    if (online) {
        if (mod == CALOBJ_MOD_THIS && rid && *rid)
	    eee_server_put_object (cb3e, cache_comp, perror);
	else {
            GError *local_err = NULL;
            const gchar *uid = ((ECalComponentId *) ids->data)->uid;
            const gchar *rid = ((ECalComponentId *) ids->data)->rid;
            gchar *id;

            id = rid && *rid ? g_strconcat (rid, "@", uid, NULL) : g_strdup (uid);
            if (verify_connection (cb3e, &local_err))
                ESClient_deleteObject (cb3e->priv->conn, cb3e->priv->calspec, id, &local_err);
            g_free (id);

            if (local_err) {
                if (local_err->code == ES_XMLRPC_ERROR_UNKNOWN_COMPONENT) {
                    g_clear_error (&local_err);
                    local_err = NULL;
                } else {
                    g_propagate_error (perror, local_err);
                }
            }
        }
    } else {
	/* mark component as out of synch */
	/*if (mod == CALOBJ_MOD_THIS && rid && *rid)
		ecalcomp_set_synch_state (cache_comp_master, ECALCOMP_LOCALLY_MODIFIED);
	else
		ecalcomp_set_synch_state (cache_comp_master, ECALCOMP_LOCALLY_DELETED);*/
    }
//    remove_cached_attachment (cb3e, uid);

    icalcomponent_free (cache_comp);
}

/* caldav tag */
static void
extract_objects (icalcomponent *icomp,
                 icalcomponent_kind ekind,
                 GSList **objects,
                 GError **error)
{
	icalcomponent         *scomp;
	icalcomponent_kind     kind;

	e_return_data_cal_error_if_fail (icomp, InvalidArg);
	e_return_data_cal_error_if_fail (objects, InvalidArg);

	kind = icalcomponent_isa (icomp);

	if (kind == ekind) {
		*objects = g_slist_prepend (NULL, icomp);
		return;
	}

	if (kind != ICAL_VCALENDAR_COMPONENT) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	*objects = NULL;
	scomp = icalcomponent_get_first_component (icomp,
						   ekind);

	while (scomp) {
		/* Remove components from toplevel here */
		*objects = g_slist_prepend (*objects, scomp);
		icalcomponent_remove_component (icomp, scomp);

		scomp = icalcomponent_get_next_component (icomp, ekind);
	}
}

/* caldav tag */
static gboolean
extract_timezones (ECalBackend3e *cb3e,
                   icalcomponent *icomp)
{
	GSList *timezones = NULL, *iter;
	icaltimezone *zone;
	GError *err = NULL;

	g_return_val_if_fail (cb3e != NULL, FALSE);
	g_return_val_if_fail (icomp != NULL, FALSE);

	extract_objects (icomp, ICAL_VTIMEZONE_COMPONENT, &timezones, &err);
	if (err) {
		g_error_free (err);
		return FALSE;
	}

	zone = icaltimezone_new ();
	for (iter = timezones; iter; iter = iter->next) {
		if (icaltimezone_set_component (zone, iter->data)) {
			e_cal_backend_store_put_timezone (cb3e->priv->store, zone);
		} else {
			icalcomponent_free (iter->data);
		}
	}

	icaltimezone_free (zone, TRUE);
	g_slist_free (timezones);

	return TRUE;
}

/*
static void
process_object (ECalBackend3e *cb3e,
                ECalComponent *ecomp,
                gboolean online,
                icalproperty_method method,
                GError **error)
{
	ESourceRegistry *registry;
	ECalBackend              *backend;
	struct icaltimetype       now;
	gchar *new_obj_str;
	gboolean is_declined, is_in_cache;
	CalObjModType mod;
	ECalComponentId *id = e_cal_component_get_id (ecomp);
	GError *err = NULL;

	backend = E_CAL_BACKEND (cb3e);

	e_return_data_cal_error_if_fail (id != NULL, InvalidObject);

	registry = e_cal_backend_get_registry (E_CAL_BACKEND (cb3e));

	now = icaltime_current_time_with_zone (icaltimezone_get_utc_timezone ());
	e_cal_component_set_created (ecomp, &now);
	e_cal_component_set_last_modified (ecomp, &now);

	is_in_cache = cache_contains (cb3e, id->uid, NULL) || cache_contains (cb3e, id->uid, id->rid);

	new_obj_str = e_cal_component_get_as_string (ecomp);
	mod = e_cal_component_is_instance (ecomp) ? CALOBJ_MOD_THIS : CALOBJ_MOD_ALL;

	switch (method) {
	case ICAL_METHOD_PUBLISH:
	case ICAL_METHOD_REQUEST:
	case ICAL_METHOD_REPLY:
		is_declined = e_cal_backend_user_declined (
			registry, e_cal_component_get_icalcomponent (ecomp));
		if (is_in_cache) {
			if (!is_declined) {
				GSList *new_components = NULL, *old_components = NULL;
				GSList new_obj_strs = {0,};

				new_obj_strs.data = new_obj_str;
				do_modify_objects (cb3e, &new_obj_strs, mod,
						  &old_components, &new_components, &err);
				if (!err && new_components && new_components->data) {
					if (!old_components || !old_components->data) {
						e_cal_backend_notify_component_created (backend, new_components->data);
					} else {
						e_cal_backend_notify_component_modified (backend, old_components->data, new_components->data);
					}
				}

				e_util_free_nullable_object_slist (old_components);
				e_util_free_nullable_object_slist (new_components);
			} else {
				GSList *new_components = NULL, *old_components = NULL;
				GSList ids = {0,};

				ids.data = id;
				do_remove_objects (cb3e, &ids, mod, &old_components, &new_components, &err);
				if (!err && old_components && old_components->data) {
					if (new_components && new_components->data) {
						e_cal_backend_notify_component_modified (backend, old_components->data, new_components->data);
					} else {
						e_cal_backend_notify_component_removed (backend, id, old_components->data, NULL);
					}
				}

				e_util_free_nullable_object_slist (old_components);
				e_util_free_nullable_object_slist (new_components);
			}
		} else if (!is_declined) {
			GSList *new_components = NULL;
			GSList new_objs = {0,};

			new_objs.data = new_obj_str;

			do_create_objects (cb3e, &new_objs, NULL, &new_components, &err);

			if (!err) {
				if (new_components && new_components->data)
					e_cal_backend_notify_component_created (backend, new_components->data);
			}

			e_util_free_nullable_object_slist (new_components);
		}
		break;
	case ICAL_METHOD_CANCEL:
		if (is_in_cache) {
			GSList *new_components = NULL, *old_components = NULL;
			GSList ids = {0,};

			ids.data = id;
			do_remove_objects (cb3e, &ids, CALOBJ_MOD_THIS, &old_components, &new_components, &err);
			if (!err && old_components && old_components->data) {
				if (new_components && new_components->data) {
					e_cal_backend_notify_component_modified (backend, old_components->data, new_components->data);
				} else {
					e_cal_backend_notify_component_removed (backend, id, old_components->data, NULL);
				}
			}

			e_util_free_nullable_object_slist (old_components);
			e_util_free_nullable_object_slist (new_components);
		} else {
			err = EDC_ERROR (ObjectNotFound);
		}
		break;

	default:
		err = EDC_ERROR (UnsupportedMethod);
		break;
	}

	e_cal_component_free_id (id);
	g_free (new_obj_str);

	if (err)
		g_propagate_error (error, err);
}
*/

/* caldav tag */
static void
do_receive_objects (ECalBackendSync *backend,
                    EDataCal *cal,
                    GCancellable *cancellable,
                    const gchar *calobj,
                    GError **perror)
{
	ECalBackend3e        *cb3e;
	icalcomponent            *icomp;
	icalcomponent_kind        kind;
	icalproperty_method       tmethod;
	gboolean                  online;
	GSList                   *objects, *iter;
	GError *err = NULL;

	cb3e = E_CAL_BACKEND_3E (backend);

	if (!check_state (cb3e, &online, perror))
		return;

	icomp = icalparser_parse_string (calobj);

	/* Try to parse cal object string */
	if (icomp == NULL) {
		g_propagate_error (perror, EDC_ERROR (InvalidObject));
		return;
	}

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));
	extract_objects (icomp, kind, &objects, &err);

	if (err) {
		icalcomponent_free (icomp);
		g_propagate_error (perror, err);
		return;
	}

	/* Extract optional timezone compnents */
	extract_timezones (cb3e, icomp);

	tmethod = icalcomponent_get_method (icomp);

	for (iter = objects; iter && !err; iter = iter->next) {
		icalcomponent       *scomp;
		ECalComponent       *ecomp;
                icalproperty_method method;

		scomp = (icalcomponent *) iter->data;
		ecomp = e_cal_component_new ();

		e_cal_component_set_icalcomponent (ecomp, scomp);

		if (icalcomponent_get_first_property (scomp, ICAL_METHOD_PROPERTY)) {
			method = icalcomponent_get_method (scomp);
		} else {
			method = tmethod;
		}

//		process_object (cb3e, ecomp, online, method, &err);
		g_object_unref (ecomp);
	}

	g_slist_free (objects);

	icalcomponent_free (icomp);

	if (err)
		g_propagate_error (perror, err);
}

/* caldav tag */
#define eee_busy_stub(_func_name, _params, _call_func, _call_params) \
static void                                                          \
_func_name _params                                                   \
{                                                                    \
        ECalBackend3e             *cb3e;                             \
        SlaveCommand              old_slave_cmd;                     \
        gboolean                  was_slave_busy;                    \
                                                                     \
        cb3e = E_CAL_BACKEND_3E (backend);                         \
                                                                        \
        /* this is done before locking */                               \
        old_slave_cmd = cb3e->priv->slave_cmd;                         \
        was_slave_busy = cb3e->priv->slave_busy;                       \
        if (was_slave_busy) {                                           \
                /* let it pause its work and do our job */              \
                update_slave_cmd (cb3e->priv, SLAVE_SHOULD_SLEEP);     \
        }                                                               \
                                                                        \
        g_mutex_lock (cb3e->priv->busy_lock);                          \
        _call_func _call_params;                                        \
                                                                        \
        /* this is done before unlocking */                             \
        if (was_slave_busy) {                                           \
                update_slave_cmd (cb3e->priv, old_slave_cmd);          \
                g_cond_signal (cb3e->priv->cond);                      \
        }                                                               \
                                                                        \
        g_mutex_unlock (cb3e->priv->busy_lock);                        \
}

/* caldav tag */
eee_busy_stub (
        eee_create_objects,
                  (ECalBackendSync *backend,
                  EDataCal *cal,
                  GCancellable *cancellable,
                  const GSList *in_calobjs,
                  GSList **uids,
                  GSList **new_components,
                  GError **perror),
        do_create_objects,
                  (cb3e,
                  in_calobjs,
                  uids,
                  new_components,
                  perror))

/* caldav tag */
eee_busy_stub (
        eee_modify_objects,
                  (ECalBackendSync *backend,
                  EDataCal *cal,
                  GCancellable *cancellable,
                  const GSList *calobjs,
                  CalObjModType mod,
                  GSList **old_components,
                  GSList **new_components,
                  GError **perror),
        do_modify_objects,
                  (cb3e,
                  calobjs,
                  mod,
                  old_components,
                  new_components,
                  perror))

/* caldav tag */
eee_busy_stub (
        eee_remove_objects,
                  (ECalBackendSync *backend,
                  EDataCal *cal,
                  GCancellable *cancellable,
                  const GSList *ids,
                  CalObjModType mod,
                  GSList **old_components,
                  GSList **new_components,
                  GError **perror),
        do_remove_objects,
                  (cb3e,
                  ids,
                  mod,
                  old_components,
                  new_components,
                  perror))

/* caldav tag */
eee_busy_stub (
        eee_receive_objects,
                  (ECalBackendSync *backend,
                  EDataCal *cal,
                  GCancellable *cancellable,
                  const gchar *calobj,
                  GError **perror),
        do_receive_objects,
                  (backend,
                  cal,
                  cancellable,
                  calobj,
                  perror))

/* caldav tag */
static void
eee_send_objects (ECalBackendSync *backend,
                  EDataCal *cal,
                  GCancellable *cancellable,
                  const gchar *calobj,
                  GSList **users,
                  gchar **modified_calobj,
                  GError **perror)
{
        *users = NULL;
        *modified_calobj = g_strdup (calobj);
}

/* caldav tag */
static void
eee_get_object (ECalBackendSync *backend,
                EDataCal *cal,
                GCancellable *cancellable,
                const gchar *uid,
                const gchar *rid,
                gchar **object,
                GError **perror)
{
	ECalBackend3e            *cb3e;
	icalcomponent            *icalcomp;

	cb3e = E_CAL_BACKEND_3E (backend);

	*object = NULL;
	icalcomp = get_comp_from_cache (cb3e, uid, rid);

	if (!icalcomp) {
		g_propagate_error (perror, EDC_ERROR (ObjectNotFound));
		return;
	}

	*object = icalcomponent_as_ical_string_r (icalcomp);
	icalcomponent_free (icalcomp);
}

/* caldav tag */
static void
eee_add_timezone (ECalBackendSync *backend,
                  EDataCal *cal,
                  GCancellable *cancellable,
                  const gchar *tzobj,
                  GError **error)
{
	icalcomponent *tz_comp;
	ECalBackend3e *cb3e;

	cb3e = E_CAL_BACKEND_3E (backend);

	e_return_data_cal_error_if_fail (E_IS_CAL_BACKEND_3E (cb3e), InvalidArg);
	e_return_data_cal_error_if_fail (tzobj != NULL, InvalidArg);

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp) {
		g_propagate_error (error, EDC_ERROR (InvalidObject));
		return;
	}

	if (icalcomponent_isa (tz_comp) == ICAL_VTIMEZONE_COMPONENT) {
		icaltimezone *zone;

		zone = icaltimezone_new ();
		icaltimezone_set_component (zone, tz_comp);

		e_cal_backend_store_put_timezone (cb3e->priv->store, zone);

		icaltimezone_free (zone, TRUE);
	} else {
		icalcomponent_free (tz_comp);
	}
}

/* caldav tag */
static void
eee_get_object_list (ECalBackendSync *backend,
                     EDataCal *cal,
                     GCancellable *cancellable,
                     const gchar *sexp_string,
                     GSList **objects,
                     GError **perror)
{
	ECalBackend3e        *cb3e;
	ECalBackendSExp	   *sexp;
	ECalBackend *bkend;
	gboolean                  do_search;
	GSList			 *list, *iter;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;

	cb3e = E_CAL_BACKEND_3E (backend);

	sexp = e_cal_backend_sexp_new (sexp_string);

	if (sexp == NULL) {
		g_propagate_error (perror, EDC_ERROR (InvalidQuery));
		return;
	}

	if (g_str_equal (sexp_string, "#t")) {
		do_search = FALSE;
	} else {
		do_search = TRUE;
	}

	*objects = NULL;

	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (sexp, &occur_start, &occur_end);

	list = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (cb3e->priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (cb3e->priv->store);

	bkend = E_CAL_BACKEND (backend);

	for (iter = list; iter; iter = g_slist_next (iter)) {
		ECalComponent *comp = E_CAL_COMPONENT (iter->data);

		if (!do_search ||
		    e_cal_backend_sexp_match_comp (sexp, comp, bkend)) {
			*objects = g_slist_prepend (*objects, e_cal_component_get_as_string (comp));
		}

		g_object_unref (comp);
	}

	g_object_unref (sexp);
	g_slist_free (list);
}

/* caldav tag */
static void
eee_start_view (ECalBackend *backend,
                EDataCalView *query)
{
	ECalBackend3e        *cb3e;
	ECalBackendSExp	 *sexp;
	ECalBackend              *bkend;
	gboolean                  do_search;
	GSList			 *list, *iter;
	const gchar               *sexp_string;
	time_t occur_start = -1, occur_end = -1;
	gboolean prunning_by_time;
	cb3e = E_CAL_BACKEND_3E (backend);

	sexp_string = e_data_cal_view_get_text (query);
	sexp = e_cal_backend_sexp_new (sexp_string);

	/* FIXME:check invalid sexp */

	if (g_str_equal (sexp_string, "#t")) {
		do_search = FALSE;
	} else {
		do_search = TRUE;
	}
	prunning_by_time = e_cal_backend_sexp_evaluate_occur_times (sexp,
									    &occur_start,
									    &occur_end);

	bkend = E_CAL_BACKEND (backend);

	list = prunning_by_time ?
		e_cal_backend_store_get_components_occuring_in_range (cb3e->priv->store, occur_start, occur_end)
		: e_cal_backend_store_get_components (cb3e->priv->store);

	for (iter = list; iter; iter = g_slist_next (iter)) {
		ECalComponent *comp = E_CAL_COMPONENT (iter->data);

		if (!do_search ||
		    e_cal_backend_sexp_match_comp (sexp, comp, bkend)) {
			e_data_cal_view_notify_components_added_1 (query, comp);
		}

		g_object_unref (comp);
	}

	g_object_unref (sexp);
	g_slist_free (list);

	e_data_cal_view_notify_complete (query, NULL /* Success */);
}

static void
eee_get_free_busy (ECalBackendSync *backend,
                   EDataCal *cal,
                   GCancellable *cancellable,
                   const GSList *users,
                   time_t start,
                   time_t end,
                   GSList **freebusy,
                   GError **error)
{
    ECalBackend3e *cb3e;
    GTimeVal tval;
    gchar *iso_start, *iso_end, *zone_str;
    icaltimezone *zone;
    GError *err = NULL;
    GSList *u;
    guint len = g_slist_length (*freebusy);

    cb3e = E_CAL_BACKEND_3E (backend);

    e_return_data_cal_error_if_fail (users != NULL, InvalidArg);
    e_return_data_cal_error_if_fail (freebusy != NULL, InvalidArg);
    e_return_data_cal_error_if_fail (start < end, InvalidArg);

    if (!verify_connection (cb3e, &err)) {
        g_propagate_error (error, err);
        return;
    }

    tval.tv_usec = 0;
    tval.tv_sec = (glong) start;
    iso_start = g_time_val_to_iso8601 (&tval);
    tval.tv_sec = (glong) end;
    iso_end = g_time_val_to_iso8601 (&tval);

    zone_str = icalcomponent_as_ical_string (icaltimezone_get_component (icaltimezone_get_utc_timezone ()));

    for (u = (GSList *) users; u; u = u->next) {
        gchar *vfb;

        vfb = ESClient_freeBusy (cb3e->priv->conn, (gchar *) u->data, iso_start, iso_end, zone_str, &err);

        if (err)
            break;

        if (vfb)
            *freebusy = g_slist_append (*freebusy, vfb);
    }

    if (err) {
        if (*freebusy) {
            GSList *last, *rest;
            last = g_slist_nth (*freebusy, len - 1);
            rest = last->next;

            last->next = NULL;

            if (rest)
                g_slist_foreach (rest, (GFunc) g_free, NULL);
            g_slist_free (rest);
        }

        g_propagate_error (error, err);
    }

    g_free (iso_start);
    g_free (iso_end);
}

/*
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
*/

/* caldav tag */
static void
eee_notify_online_cb (ECalBackend *backend,
                      GParamSpec *pspec)
{
	ECalBackend3e        *cb3e;
	gboolean online;

	cb3e = E_CAL_BACKEND_3E (backend);

	/*g_mutex_lock (cb3e->priv->busy_lock);*/

	online = e_backend_get_online (E_BACKEND (backend));

	if (!cb3e->priv->loaded) {
		e_cal_backend_notify_online (backend, online);
		/*g_mutex_unlock (cb3e->priv->busy_lock);*/
		return;
	}

	if (online) {
		/* Wake up the slave thread */
		update_slave_cmd (cb3e->priv, SLAVE_SHOULD_WORK);
		g_cond_signal (cb3e->priv->cond);
	} else {
                xr_client_close (cb3e->priv->conn);
		update_slave_cmd (cb3e->priv, SLAVE_SHOULD_SLEEP);
	}

	e_cal_backend_notify_online (backend, online);

	/*g_mutex_unlock (cb3e->priv->busy_lock);*/
}

static icaltimezone *
eee_internal_get_timezone (ECalBackend *backend,
                           const gchar *tzid)
{
    icaltimezone *zone;
    ECalBackend3e *cb3e;

    cb3e = E_CAL_BACKEND_3E (backend);
    zone = NULL;

    if (cb3e->priv->store)
        zone = (icaltimezone *) e_cal_backend_store_get_timezone (cb3e->priv->store, tzid);

    if (!zone && E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone)
        zone = E_CAL_BACKEND_CLASS (parent_class)->internal_get_timezone (backend, tzid);

    return zone;
}

/* caldav tag */
static gpointer
eee_source_changed_thread (gpointer data)
{
        ECalBackend3e *cb3e = data;
        SlaveCommand old_slave_cmd;
        gboolean old_slave_busy;

        g_return_val_if_fail (cb3e != NULL, NULL);

        old_slave_cmd = cb3e->priv->slave_cmd;
        old_slave_busy = cb3e->priv->slave_busy;
        if (old_slave_busy) {
                update_slave_cmd (cb3e->priv, SLAVE_SHOULD_SLEEP);
                g_mutex_lock (cb3e->priv->busy_lock);
        }

        initialize_backend (cb3e, NULL);

        /* always wakeup thread, even when it was sleeping */
        g_cond_signal (cb3e->priv->cond);

        if (old_slave_busy) {
                update_slave_cmd (cb3e->priv, old_slave_cmd);
                g_mutex_unlock (cb3e->priv->busy_lock);
        }

        cb3e->priv->updating_source = FALSE;

        g_object_unref (cb3e);

        return NULL;
}

/* caldav tag */
static void
eee_source_changed_cb (ESource *source,
                       ECalBackend3e *cb3e)
{
        GThread *thread;

        g_return_if_fail (source != NULL);
        g_return_if_fail (cb3e != NULL);

        if (cb3e->priv->updating_source)
                return;

        cb3e->priv->updating_source = TRUE;

        thread = g_thread_new (NULL, eee_source_changed_thread, g_object_ref (cb3e));
        g_thread_unref (thread);
}

/* caldav tag */
static ESourceAuthenticationResult
eee_try_password_sync (ESourceAuthenticator *authenticator,
                       const GString *password,
                       GCancellable *cancellable,
                       GError **error)
{
        ECalBackend3e *cb3e;
        ESourceAuthenticationResult result;
        GError *local_error = NULL;

        cb3e = E_CAL_BACKEND_3E (authenticator);

        /* Busy lock is already acquired by caldav_do_open(). */

        g_free (cb3e->priv->password);
        cb3e->priv->password = g_strdup (password->str);

        open_calendar (cb3e, &local_error);

        if (local_error == NULL) {
                result = E_SOURCE_AUTHENTICATION_ACCEPTED;
        } else if (g_error_matches (local_error, E_DATA_CAL_ERROR, AuthenticationFailed)) {
                result = E_SOURCE_AUTHENTICATION_REJECTED;
                g_clear_error (&local_error);
        } else {
                result = E_SOURCE_AUTHENTICATION_ERROR;
                g_propagate_error (error, local_error);
        }

        return result;
}

/* caldav tag */
static void
eee_source_authenticator_init (ESourceAuthenticatorInterface *interface)
{
    interface->try_password_sync = eee_try_password_sync;
}

G_DEFINE_TYPE_WITH_CODE (
	ECalBackend3e,
	e_cal_backend_3e,
	E_TYPE_CAL_BACKEND_SYNC,
	G_IMPLEMENT_INTERFACE (
		E_TYPE_SOURCE_AUTHENTICATOR,
		eee_source_authenticator_init))

static void
e_cal_backend_3e_dispose (GObject *object)
{
    ECalBackend3ePrivate *priv;
    ESource *source;

    priv = E_CAL_BACKEND_3E_GET_PRIVATE (object);

    update_slave_cmd (priv, SLAVE_SHOULD_DIE);

    g_mutex_lock (priv->busy_lock);

    if (priv->disposed) {
        g_mutex_unlock (priv->busy_lock);
        return;
    }

    source = e_backend_get_source (E_BACKEND (object));
    if (source) {
        g_signal_handlers_disconnect_by_func (G_OBJECT (source), eee_source_changed_cb, object);

        if (priv->refresh_id) {
            e_source_refresh_remove_timeout (source, priv->refresh_id);
            priv->refresh_id = 0;
        }
    }

    if (priv->synch_slave) {
        g_cond_signal (priv->cond);
        g_cond_wait (priv->slave_gone_cond, priv->busy_lock);
    }

    if (priv->conn) {
        xr_client_close (priv->conn);
        xr_client_free (priv->conn);
        priv->conn = NULL;
    }

    if (priv->store != NULL)
        g_object_unref (priv->store);

    priv->disposed = TRUE;
    g_mutex_unlock (priv->busy_lock);

    G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
e_cal_backend_3e_finalize (GObject *object)
{
    ECalBackend3ePrivate *priv;

    priv = E_CAL_BACKEND_3E_GET_PRIVATE (object);

    g_mutex_free (priv->busy_lock);
    g_cond_free (priv->cond);
    g_cond_free (priv->slave_gone_cond);

    g_free (priv->username);
    g_free (priv->password);
    g_free (priv->server_uri);
    g_free (priv->calspec);

    G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
e_cal_backend_3e_init (ECalBackend3e *cb3e)
{
    cb3e->priv = E_CAL_BACKEND_3E_GET_PRIVATE (cb3e);

    cb3e->priv->disposed = FALSE;
    cb3e->priv->loaded = FALSE;
    cb3e->priv->opened = FALSE;

    cb3e->priv->busy_lock = g_mutex_new ();
    cb3e->priv->cond = g_cond_new ();
    cb3e->priv->slave_gone_cond = g_cond_new ();

    cb3e->priv->slave_cmd = SLAVE_SHOULD_SLEEP;
    cb3e->priv->slave_busy = FALSE;

    e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC(cb3e), FALSE);

    g_signal_connect (cb3e, "notify::online", G_CALLBACK (eee_notify_online_cb), NULL);
}

static void
e_cal_backend_3e_class_init(ECalBackend3eClass *class)
{
    GObjectClass                                     *object_class;
    ECalBackendClass                                 *backend_class;
    ECalBackendSyncClass                             *sync_class;

    object_class = (GObjectClass *)class;
    backend_class = (ECalBackendClass *)class;
    sync_class = (ECalBackendSyncClass *)class;

    parent_class = (ECalBackendSyncClass *) g_type_class_peek_parent (class);
    g_type_class_add_private (class, sizeof (ECalBackend3ePrivate));

    object_class->dispose = e_cal_backend_3e_dispose;
    object_class->finalize = e_cal_backend_3e_finalize;

// Remade methods
    sync_class->get_backend_property_sync = eee_get_backend_property;

    sync_class->open_sync = eee_open;
    sync_class->refresh_sync = eee_refresh;
//    sync_class->remove_sync = eee_remove;

    sync_class->create_objects_sync = eee_create_objects;
    sync_class->modify_objects_sync = eee_modify_objects;
    sync_class->remove_objects_sync = eee_remove_objects;

    sync_class->receive_objects_sync = eee_receive_objects;
    sync_class->send_objects_sync = eee_send_objects;
    sync_class->get_object_sync = eee_get_object;
    sync_class->get_object_list_sync = eee_get_object_list;
    sync_class->add_timezone_sync = eee_add_timezone;
    sync_class->get_free_busy_sync = eee_get_free_busy;

    backend_class->start_view = eee_start_view;

    backend_class->internal_get_timezone = eee_internal_get_timezone;
}
