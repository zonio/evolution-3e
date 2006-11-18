/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* Evolution calendar - iCalendar 3e backend
 *
 * Copyright (C) 2003 Novell, Inc.
 *
 * Authors: Hans Petter Jansson <hpj@ximian.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU Lesser General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA.
 *
 * Based in part on the file backend.
 */

#include <config.h>
#include <string.h>
#include <unistd.h>
#include <gconf/gconf-client.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-moniker-util.h>
#include <libedataserver/e-xml-hash-utils.h>
#include <libecal/e-cal-recur.h>
#include <libecal/e-cal-util.h>
#include <libecal/e-cal-time-util.h>
#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-util.h>
#include <libedata-cal/e-cal-backend-sexp.h>
#include "e-cal-backend-3e.h"

struct _ECalBackend3ePrivate
{
	/* URI to get remote calendar data from */
	char *uri;

	/* Local/remote mode */
	CalMode mode;

	/* The file cache */
	ECalBackendCache *cache;

	/* The calendar's default timezone, used for resolving DATE and
	   floating DATE-TIME values. */
	icaltimezone *default_zone;

	/* The list of live queries */
	GList *queries;

	/* Reload */
	guint reload_timeout_id;
	guint is_loading : 1;

	/* Flags */
	gboolean opened;
};

static ECalBackendSyncClass *parent_class;

/* Dispose handler for the file backend */
static void
e_cal_backend_3e_dispose (GObject *object)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;

	cb = E_CAL_BACKEND_3E (object);
	priv = cb->priv;

	if (G_OBJECT_CLASS (parent_class)->dispose)
		(* G_OBJECT_CLASS (parent_class)->dispose) (object);
}

/* Finalize handler for the file backend */
static void
e_cal_backend_3e_finalize (GObject *object)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (E_IS_CAL_BACKEND_3E (object));

	cb = E_CAL_BACKEND_3E (object);
	priv = cb->priv;

	/* Clean up */

	if (priv->cache) {
		g_object_unref (priv->cache);
		priv->cache = NULL;
	}

	if (priv->uri) {
        g_free (priv->uri);
		priv->uri = NULL;
	}

	if (priv->default_zone) {
		icaltimezone_free (priv->default_zone, 1);
		priv->default_zone = NULL;
	}
	
	if (priv->reload_timeout_id) {
		g_source_remove (priv->reload_timeout_id);
		priv->reload_timeout_id = 0;
	}

	g_free (priv);
	cb->priv = NULL;

	if (G_OBJECT_CLASS (parent_class)->finalize)
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
}

/* Calendar backend methods */

/* Is_read_only handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_3e_is_read_only (ECalBackendSync *backend, EDataCal *cal, gboolean *read_only)
{
	*read_only = FALSE;

	return GNOME_Evolution_Calendar_Success;
}

/* Get_email_address handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_3e_get_cal_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	*address = NULL;

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_3e_get_ldap_attribute (ECalBackendSync *backend, EDataCal *cal, char **attribute)
{
	*attribute = NULL;
	
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_3e_get_alarm_email_address (ECalBackendSync *backend, EDataCal *cal, char **address)
{
	*address = NULL;
	
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_3e_get_static_capabilities (ECalBackendSync *backend, EDataCal *cal, char **capabilities)
{
	*capabilities = g_strdup (CAL_STATIC_CAPABILITY_NO_EMAIL_ALARMS);

	return GNOME_Evolution_Calendar_Success;
}

/* Open handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_3e_open (ECalBackendSync *backend, EDataCal *cal, gboolean only_if_exists,
			 const char *username, const char *password)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;
	
	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	if (!priv->cache) {
		priv->cache = e_cal_backend_cache_new (e_cal_backend_get_uri (E_CAL_BACKEND (backend)), E_CAL_SOURCE_TYPE_EVENT);


		if (!priv->cache) {
			e_cal_backend_notify_error (E_CAL_BACKEND(cb), "Could not create cache file");
			return GNOME_Evolution_Calendar_OtherError;	
		}
		
		if (priv->default_zone) {
			e_cal_backend_cache_put_default_timezone (priv->cache, priv->default_zone);
		}

		if (priv->mode == CAL_MODE_LOCAL) 
			return GNOME_Evolution_Calendar_Success;
		
//		g_idle_add ((GSourceFunc) begin_retrieval_cb, cb);
	}

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_3e_remove (ECalBackendSync *backend, EDataCal *cal)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;
	
	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	if (!priv->cache)
		return GNOME_Evolution_Calendar_OtherError;

	e_file_cache_remove (E_FILE_CACHE (priv->cache));
	return GNOME_Evolution_Calendar_Success;
}

/* is_loaded handler for the file backend */
static gboolean
e_cal_backend_3e_is_loaded (ECalBackend *backend)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	if (!priv->cache)
		return FALSE;

	return TRUE;
}

/* is_remote handler for the 3e backend */
static CalMode
e_cal_backend_3e_get_mode (ECalBackend *backend)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	return priv->mode;
}

/* Set_mode handler for the 3e backend */
static void
e_cal_backend_3e_set_mode (ECalBackend *backend, CalMode mode)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;
	GNOME_Evolution_Calendar_CalMode set_mode;
	gboolean loaded;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	loaded = e_cal_backend_3e_is_loaded (backend);
				
	switch (mode) {
		case CAL_MODE_LOCAL:
			priv->mode = mode;
			set_mode = cal_mode_to_corba (mode);
			if (loaded && priv->reload_timeout_id) {
				g_source_remove (priv->reload_timeout_id);
				priv->reload_timeout_id = 0;
			}
			break;
		case CAL_MODE_REMOTE:
		case CAL_MODE_ANY:	
			priv->mode = mode;
			set_mode = cal_mode_to_corba (mode);
			if (loaded) 
//				g_idle_add ((GSourceFunc) begin_retrieval_cb, backend);
			break;
		
			priv->mode = CAL_MODE_REMOTE;
			set_mode = GNOME_Evolution_Calendar_MODE_REMOTE;
			break;
		default:
			set_mode = GNOME_Evolution_Calendar_MODE_ANY;
			break;
	}

	if (loaded) {
		
		if (set_mode == GNOME_Evolution_Calendar_MODE_ANY)
			e_cal_backend_notify_mode (backend,
						   GNOME_Evolution_Calendar_CalListener_MODE_NOT_SUPPORTED,
						   cal_mode_to_corba (priv->mode));
		else
			e_cal_backend_notify_mode (backend,
						   GNOME_Evolution_Calendar_CalListener_MODE_SET,
						   set_mode);
	}
}

static ECalBackendSyncStatus
e_cal_backend_3e_get_default_object (ECalBackendSync *backend, EDataCal *cal, char **object)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;
	icalcomponent *icalcomp;
	icalcomponent_kind kind;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	kind = e_cal_backend_get_kind (E_CAL_BACKEND (backend));
	icalcomp = e_cal_util_new_component (kind);
	*object = g_strdup (icalcomponent_as_ical_string (icalcomp));
	icalcomponent_free (icalcomp);

	return GNOME_Evolution_Calendar_Success;
}

/* Get_object_component handler for the 3e backend */
static ECalBackendSyncStatus
e_cal_backend_3e_get_object (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *rid, char **object)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;
	ECalComponent *comp = NULL;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	if (!priv->cache)
		return GNOME_Evolution_Calendar_ObjectNotFound;

	comp = e_cal_backend_cache_get_component (priv->cache, uid, rid);
	if (!comp)
		return GNOME_Evolution_Calendar_ObjectNotFound;

	*object = e_cal_component_get_as_string (comp);
	g_object_unref (comp);

	return GNOME_Evolution_Calendar_Success;
}

/* Get_timezone_object handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_3e_get_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzid, char **object)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;
	const icaltimezone *zone;
	icalcomponent *icalcomp;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	g_return_val_if_fail (tzid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	/* first try to get the timezone from the cache */
	zone = e_cal_backend_cache_get_timezone (priv->cache, tzid);
	if (!zone) {
		zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
		if (!zone)
			return GNOME_Evolution_Calendar_ObjectNotFound;
	}

	icalcomp = icaltimezone_get_component ((icaltimezone *)zone);
	if (!icalcomp)
		return GNOME_Evolution_Calendar_InvalidObject;

	*object = g_strdup (icalcomponent_as_ical_string (icalcomp));

	return GNOME_Evolution_Calendar_Success;
}

/* Add_timezone handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_3e_add_timezone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;
	icalcomponent *tz_comp;
	icaltimezone *zone;

	cb = (ECalBackend3e *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_3E (cb), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cb->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	if (icalcomponent_isa (tz_comp) != ICAL_VTIMEZONE_COMPONENT) {
		icalcomponent_free (tz_comp);
		return GNOME_Evolution_Calendar_InvalidObject;
	}

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);
	e_cal_backend_cache_put_timezone (priv->cache, zone);

	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_3e_set_default_zone (ECalBackendSync *backend, EDataCal *cal, const char *tzobj)
{
	icalcomponent *tz_comp;
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;
	icaltimezone *zone;

	cb = (ECalBackend3e *) backend;

	g_return_val_if_fail (E_IS_CAL_BACKEND_3E (cb), GNOME_Evolution_Calendar_OtherError);
	g_return_val_if_fail (tzobj != NULL, GNOME_Evolution_Calendar_OtherError);

	priv = cb->priv;

	tz_comp = icalparser_parse_string (tzobj);
	if (!tz_comp)
		return GNOME_Evolution_Calendar_InvalidObject;

	zone = icaltimezone_new ();
	icaltimezone_set_component (zone, tz_comp);

	if (priv->default_zone)
		icaltimezone_free (priv->default_zone, 1);

	/* Set the default timezone to it. */
	priv->default_zone = zone;

	return GNOME_Evolution_Calendar_Success;
}

/* Get_objects_in_range handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_3e_get_object_list (ECalBackendSync *backend, EDataCal *cal, const char *sexp, GList **objects)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;
	GList *components, *l;
	ECalBackendSExp *cbsexp;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	if (!priv->cache)
		return GNOME_Evolution_Calendar_NoSuchCal;

	/* process all components in the cache */
	cbsexp = e_cal_backend_sexp_new (sexp);

	*objects = NULL;
	components = e_cal_backend_cache_get_components (priv->cache);
	for (l = components; l != NULL; l = l->next) {
		if (e_cal_backend_sexp_match_comp (cbsexp, E_CAL_COMPONENT (l->data), E_CAL_BACKEND (backend))) {
			*objects = g_list_append (*objects, e_cal_component_get_as_string (l->data));
		}
	}

	g_list_foreach (components, (GFunc) g_object_unref, NULL);
	g_list_free (components);
	g_object_unref (cbsexp);

	return GNOME_Evolution_Calendar_Success;
}

/* get_query handler for the file backend */
static void
e_cal_backend_3e_start_query (ECalBackend *backend, EDataCalView *query)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;
	GList *components, *l, *objects = NULL;
	ECalBackendSExp *cbsexp;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	if (!priv->cache) {
		e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_NoSuchCal);
		return;
	}

	/* process all components in the cache */
	cbsexp = e_cal_backend_sexp_new (e_data_cal_view_get_text (query));

	objects = NULL;
	components = e_cal_backend_cache_get_components (priv->cache);
	for (l = components; l != NULL; l = l->next) {
		if (e_cal_backend_sexp_match_comp (cbsexp, E_CAL_COMPONENT (l->data), E_CAL_BACKEND (backend))) {
			objects = g_list_append (objects, e_cal_component_get_as_string (l->data));
		}
	}

	e_data_cal_view_notify_objects_added (query, (const GList *) objects);

	g_list_foreach (components, (GFunc) g_object_unref, NULL);
	g_list_free (components);
	g_list_foreach (objects, (GFunc) g_free, NULL);
	g_list_free (objects);
	g_object_unref (cbsexp);

	e_data_cal_view_notify_done (query, GNOME_Evolution_Calendar_Success);
}

/* Get_free_busy handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_3e_get_free_busy (ECalBackendSync *backend, EDataCal *cal, GList *users,
				time_t start, time_t end, GList **freebusy)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;
	gchar *address, *name;
	icalcomponent *vfb;
	char *calobj;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	g_return_val_if_fail (start != -1 && end != -1, GNOME_Evolution_Calendar_InvalidRange);
	g_return_val_if_fail (start <= end, GNOME_Evolution_Calendar_InvalidRange);

	if (!priv->cache)
		return GNOME_Evolution_Calendar_NoSuchCal;

	return GNOME_Evolution_Calendar_Success;
}

/* Get_changes handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_3e_get_changes (ECalBackendSync *backend, EDataCal *cal, const char *change_id,
				GList **adds, GList **modifies, GList **deletes)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	g_return_val_if_fail (change_id != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	/* FIXME */
	return GNOME_Evolution_Calendar_Success;
}

/* Discard_alarm handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_3e_discard_alarm (ECalBackendSync *backend, EDataCal *cal, const char *uid, const char *auid)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	/* FIXME */
	return GNOME_Evolution_Calendar_Success;
}

static ECalBackendSyncStatus
e_cal_backend_3e_create_object (ECalBackendSync *backend, EDataCal *cal, char **calobj, char **uid)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;
	
	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	return GNOME_Evolution_Calendar_PermissionDenied;
}

static ECalBackendSyncStatus
e_cal_backend_3e_modify_object (ECalBackendSync *backend, EDataCal *cal, const char *calobj, 
				CalObjModType mod, char **old_object, char **new_object)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	return GNOME_Evolution_Calendar_PermissionDenied;
}

/* Remove_object handler for the file backend */
static ECalBackendSyncStatus
e_cal_backend_3e_remove_object (ECalBackendSync *backend, EDataCal *cal,
				const char *uid, const char *rid,
				CalObjModType mod, char **old_object,
				char **object)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	g_return_val_if_fail (uid != NULL, GNOME_Evolution_Calendar_ObjectNotFound);

	*old_object = *object = NULL;

	return GNOME_Evolution_Calendar_PermissionDenied;
}

/* Update_objects handler for the file backend. */
static ECalBackendSyncStatus
e_cal_backend_3e_receive_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	g_return_val_if_fail (calobj != NULL, GNOME_Evolution_Calendar_InvalidObject);

	return GNOME_Evolution_Calendar_PermissionDenied;
}

static ECalBackendSyncStatus
e_cal_backend_3e_send_objects (ECalBackendSync *backend, EDataCal *cal, const char *calobj, GList **users,
				 char **modified_calobj)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	*users = NULL;
	*modified_calobj = NULL;

	return GNOME_Evolution_Calendar_PermissionDenied;
}

static icaltimezone *
e_cal_backend_3e_internal_get_default_timezone (ECalBackend *backend)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	if (!priv->cache)
		return NULL;

	return NULL;
}

static icaltimezone *
e_cal_backend_3e_internal_get_timezone (ECalBackend *backend, const char *tzid)
{
	ECalBackend3e *cb;
	ECalBackend3ePrivate *priv;
	icaltimezone *zone;

	cb = E_CAL_BACKEND_3E (backend);
	priv = cb->priv;

	if (!strcmp (tzid, "UTC"))
	        zone = icaltimezone_get_utc_timezone ();
	else {
		zone = icaltimezone_get_builtin_timezone_from_tzid (tzid);
	}

	return zone;
}

/* Object initialization function for the file backend */
static void
e_cal_backend_3e_init (ECalBackend3e *cb, ECalBackend3eClass *class)
{
	ECalBackend3ePrivate *priv;

	priv = g_new0 (ECalBackend3ePrivate, 1);
	cb->priv = priv;

	priv->uri = NULL;
	priv->reload_timeout_id = 0;
	priv->opened = FALSE;

	e_cal_backend_sync_set_lock (E_CAL_BACKEND_SYNC (cb), TRUE);
}

/* Class initialization function for the file backend */
static void
e_cal_backend_3e_class_init (ECalBackend3eClass *class)
{
	GObjectClass *object_class;
	ECalBackendClass *backend_class;
	ECalBackendSyncClass *sync_class;

	object_class = (GObjectClass *) class;
	backend_class = (ECalBackendClass *) class;
	sync_class = (ECalBackendSyncClass *) class;

	parent_class = (ECalBackendSyncClass *) g_type_class_peek_parent (class);

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


/**
 * e_cal_backend_3e_get_type:
 * @void: 
 * 
 * Registers the #ECalBackend3e class if necessary, and returns the type ID
 * associated to it.
 * 
 * Return value: The type ID of the #ECalBackend3e class.
 **/
GType
e_cal_backend_3e_get_type (void)
{
	static GType e_cal_backend_3e_type = 0;

	if (!e_cal_backend_3e_type) {
		static GTypeInfo info = {
                        sizeof (ECalBackend3eClass),
                        (GBaseInitFunc) NULL,
                        (GBaseFinalizeFunc) NULL,
                        (GClassInitFunc) e_cal_backend_3e_class_init,
                        NULL, NULL,
                        sizeof (ECalBackend3e),
                        0,
                        (GInstanceInitFunc) e_cal_backend_3e_init
                };
		e_cal_backend_3e_type = g_type_register_static (E_TYPE_CAL_BACKEND_SYNC,
								  "ECalBackend3e", &info, 0);
	}

	return e_cal_backend_3e_type;
}
