/*
 * module-eee-backend.c
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with the program; if not, see <http://www.gnu.org/licenses/>
 *
 */

#include <config.h>
#include <glib/gi18n-lib.h>

#include <libebackend/libebackend.h>
#include <dns-txt-search.h>
#include <ESClient.xrc.h>
#include <e-source-eee.h>

/* Standard GObject macros */
#define E_TYPE_EEE_BACKEND \
	(e_eee_backend_get_type ())
#define E_EEE_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_EEE_BACKEND, EEeeBackend))
#define E_EEE_BACKEND_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_EEE_BACKEND, EEeeBackendPrivate))

typedef struct _EEeeBackendPrivate EEeeBackendPrivate;

struct _EEeeBackendPrivate {
	gchar *password;
	gboolean need_update_calendars;
};

typedef struct _EEeeBackend EEeeBackend;
typedef struct _EEeeBackendClass EEeeBackendClass;

typedef struct _EEeeBackendFactory EEeeBackendFactory;
typedef struct _EEeeBackendFactoryClass EEeeBackendFactoryClass;

struct _EEeeBackend {
	ECollectionBackend parent;
	EEeeBackendPrivate *priv;
};

struct _EEeeBackendClass {
	ECollectionBackendClass parent_class;
};

struct _EEeeBackendFactory {
	ECollectionBackendFactory parent;
};

struct _EEeeBackendFactoryClass {
	ECollectionBackendFactoryClass parent_class;
};

/* Module Entry Points */
void e_module_load (GTypeModule *type_module);
void e_module_unload (GTypeModule *type_module);

/* Forward Declarations */
GType e_eee_backend_get_type (void);
GType e_eee_backend_factory_get_type (void);

static void eee_backend_authenticator_init (ESourceAuthenticatorInterface *interface);

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	EEeeBackend,
	e_eee_backend,
	E_TYPE_COLLECTION_BACKEND,
	0,
        G_IMPLEMENT_INTERFACE (
                E_TYPE_SOURCE_AUTHENTICATOR,
                eee_backend_authenticator_init))

G_DEFINE_DYNAMIC_TYPE (
	EEeeBackendFactory,
	e_eee_backend_factory,
	E_TYPE_COLLECTION_BACKEND_FACTORY)

static const gchar *
eee_get_attr_value (GArray *attrs, const gchar *name)
{
	guint i;

	for (i = 0; i < attrs->len; i++) {
		ESAttribute *attr = g_array_index (attrs, ESAttribute *, i);

		if (!g_strcmp0 (attr->name, name))
			return attr->value;
	}

	return NULL;
}

static void
eee_backend_queue_auth (EEeeBackend *backend)
{
	backend->priv->need_update_calendars = FALSE;

	e_backend_authenticate (E_BACKEND (backend), E_SOURCE_AUTHENTICATOR (backend), NULL, NULL, NULL);
}

static void
eee_backend_get_calendars_list (EEeeBackend *backend, xr_client_conn *conn)
{
	ESource *collection_source;
	ESourceRegistryServer *server;
	ESourceCollection *collection_extension;
	const gchar *backend_name;
	GArray *cals;
	guint i;

	collection_source = e_backend_get_source (E_BACKEND (backend));

	collection_extension = e_source_get_extension (
		collection_source, E_SOURCE_EXTENSION_COLLECTION);

	cals = ESClient_getCalendars (conn, "", NULL);

	backend_name = "eee";

	server = e_collection_backend_ref_server (E_COLLECTION_BACKEND (backend));

	for (i = 0; cals != NULL && i < cals->len; i++) {
		guint j;
		ESource *source;
		const gchar *extension_name;
		ESourceExtension *extension;
		ESCalendarInfo *cal = g_array_index (cals, ESCalendarInfo *, i);
		gchar *resource_id;
		GArray *perms;
		const gchar *title, *color;

		resource_id = g_strconcat (cal->owner, ":", cal->name, NULL);
		source = e_collection_backend_new_child (E_COLLECTION_BACKEND (backend), resource_id);

		title = eee_get_attr_value (cal->attrs, "title");
		e_source_set_display_name (source, title ? title : cal->name);

		e_server_side_source_set_remote_deletable (E_SERVER_SIDE_SOURCE (source), TRUE);

		extension_name = E_SOURCE_EXTENSION_CALENDAR;
		extension = e_source_get_extension (source, extension_name);

		color = eee_get_attr_value (cal->attrs, "color");
		if (color)
			e_source_selectable_set_color (E_SOURCE_SELECTABLE (extension), color);

		e_source_backend_set_backend_name (E_SOURCE_BACKEND (extension), backend_name);

		extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
		extension = e_source_get_extension (source, extension_name);

		g_object_bind_property (
			collection_extension, "identity",
			extension, "user",
			G_BINDING_SYNC_CREATE);

		extension_name = E_SOURCE_EXTENSION_RESOURCE;
		extension = e_source_get_extension (source, extension_name);
		e_source_resource_set_identity (E_SOURCE_RESOURCE (extension), resource_id);
		g_free (resource_id);

		extension_name = E_SOURCE_EXTENSION_EEE;
		extension = e_source_get_extension (source, extension_name);
		perms = ESClient_getUserPermissions (conn, cal->name, NULL);
		for (j = 0; perms != NULL && j < perms->len; j++) {
			glong p = 0;
			ESUserPermission *perm = g_array_index (perms, ESUserPermission *, j);

			if (!g_strcmp0 (perm->perm, "read"))
				p = EEE_PERM_READ;
			else if (!g_strcmp0 (perm->perm, "readwrite"))
				p = EEE_PERM_READWRITE;
			if (p != 0)
				e_source_eee_add_user_perm (E_SOURCE_EEE (extension), perm->user, p);
		}

		e_source_registry_server_add_source (server, source);

		g_object_unref (source);
	}

	g_object_unref (server);

	Array_ESCalendarInfo_free (cals);

/*
	resource_id = GOOGLE_CALENDAR_RESOURCE_ID;
	source = e_collection_backend_new_child (backend, resource_id);
	e_source_set_display_name (source, _("Calendar"));

*/
	/* Configure the calendar source. */
/*
	backend_name = "eee";

	extension_name = E_SOURCE_EXTENSION_CALENDAR;
	extension = e_source_get_extension (source, extension_name);

	e_source_backend_set_backend_name (
		E_SOURCE_BACKEND (extension), backend_name);

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	extension = e_source_get_extension (source, extension_name);

	e_source_authentication_set_host (
		E_SOURCE_AUTHENTICATION (extension),
		GOOGLE_CALENDAR_HOST);

	g_object_bind_property (
		collection_extension, "identity",
		extension, "user",
		G_BINDING_SYNC_CREATE);

	extension_name = E_SOURCE_EXTENSION_SECURITY;
	extension = e_source_get_extension (source, extension_name);

	e_source_security_set_secure (
		E_SOURCE_SECURITY (extension), TRUE);

	extension_name = E_SOURCE_EXTENSION_RESOURCE;
	extension = e_source_get_extension (source, extension_name);

	identity = e_source_collection_get_identity (collection_extension);
	path = g_strdup_printf (GOOGLE_CALENDAR_CALDAV_PATH, identity);
	e_source_webdav_set_resource_path (
		E_SOURCE_WEBDAV (extension), path);
	g_free (path);

	server = e_collection_backend_ref_server (backend);
	e_source_registry_server_add_source (server, source);
	g_object_unref (server);

	g_object_unref (source);
*/
}

static void
eee_backend_populate (ECollectionBackend *backend)
{
	ESource *source;
	EEeeBackend *eee_backend = E_EEE_BACKEND (backend);

	source = e_backend_get_source (E_BACKEND (backend));

	eee_backend->priv->need_update_calendars = TRUE;

	if (!e_source_get_enabled (source) || !e_backend_get_online (E_BACKEND (backend)))
		return;

	eee_backend_queue_auth (E_EEE_BACKEND (backend));
}

static gchar *
eee_backend_dup_resource_id (ECollectionBackend *backend,
                                ESource *child_source)
{
	const gchar *extension_name, *parent_id, *identity;
	ESourceExtension *extension;
	ESource *source;

	source = e_backend_get_source (E_BACKEND (backend));

	parent_id = e_source_get_uid (source);

	extension_name = E_SOURCE_EXTENSION_RESOURCE;
	extension = e_source_get_extension (child_source, extension_name);
	identity = e_source_resource_get_identity (E_SOURCE_RESOURCE (extension));

	return g_strconcat (parent_id ? parent_id : "eee", ".", identity, NULL);
}

static void
eee_backend_child_added (ECollectionBackend *backend,
                         ESource *child_source)
{
	ESource *collection_source;
	const gchar *extension_name;
	gboolean is_mail = FALSE;

	collection_source = e_backend_get_source (E_BACKEND (backend));

	extension_name = E_SOURCE_EXTENSION_MAIL_ACCOUNT;
	is_mail |= e_source_has_extension (child_source, extension_name);

	extension_name = E_SOURCE_EXTENSION_MAIL_IDENTITY;
	is_mail |= e_source_has_extension (child_source, extension_name);

	extension_name = E_SOURCE_EXTENSION_MAIL_TRANSPORT;
	is_mail |= e_source_has_extension (child_source, extension_name);

	/* Synchronize mail-related display names with the collection. */
	if (is_mail)
		g_object_bind_property (
			collection_source, "display-name",
			child_source, "display-name",
			G_BINDING_SYNC_CREATE);

	/* Synchronize mail-related user with the collection identity. */
	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	if (is_mail && e_source_has_extension (child_source, extension_name)) {
		ESourceAuthentication *auth_child_extension;
		ESourceCollection *collection_extension;

		extension_name = E_SOURCE_EXTENSION_COLLECTION;
		collection_extension = e_source_get_extension (
			collection_source, extension_name);

		extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
		auth_child_extension = e_source_get_extension (
			child_source, extension_name);

		g_object_bind_property (
			collection_extension, "identity",
			auth_child_extension, "user",
			G_BINDING_SYNC_CREATE);
	}

	/* Chain up to parent's child_added() method. */
	E_COLLECTION_BACKEND_CLASS (e_eee_backend_parent_class)->
		child_added (backend, child_source);
}

static void
eee_create_calendar (EEeeBackend *backend, xr_client_conn *conn, const gchar *username, ESource *source)
{
	ESourceRegistryServer *server;
	ESourceExtension *extension;
	ESource *collection_source;
	ESourceCollection *collection_extension;
	const gchar *title, *color, *extension_name, *cache_dir;
	gchar *resource_id, calname[9];
	GRand *rnd = g_rand_new ();

	do {
		sprintf (calname, "%08x", g_rand_int (rnd));
	} while (!ESClient_createCalendar (conn, calname, NULL));

	g_rand_free (rnd);

	resource_id = g_strconcat (username, ":", calname, NULL);

	extension_name = E_SOURCE_EXTENSION_RESOURCE;
	extension = e_source_get_extension (source, extension_name);

	e_source_resource_set_identity (E_SOURCE_RESOURCE (extension), resource_id);

	title = e_source_get_display_name (source);
	
	extension_name = E_SOURCE_EXTENSION_CALENDAR;
	extension = e_source_get_extension (source, extension_name);

	color = e_source_selectable_get_color (E_SOURCE_SELECTABLE (extension));

	ESClient_setCalendarAttribute (conn, resource_id, "title", title, TRUE, NULL);
	ESClient_setCalendarAttribute (conn, resource_id, "color", color, FALSE, NULL);

	g_free (resource_id);

	collection_source = e_backend_get_source (E_BACKEND (backend));
	collection_extension = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_COLLECTION);

	extension_name = E_SOURCE_EXTENSION_AUTHENTICATION;
	extension = e_source_get_extension (source, extension_name);

	g_object_bind_property (
		collection_extension, "identity",
		extension, "user",
		G_BINDING_SYNC_CREATE);

	e_source_set_parent (source, e_source_get_uid (collection_source));

	cache_dir = e_collection_backend_get_cache_dir (E_COLLECTION_BACKEND (backend));
	e_server_side_source_set_write_directory (E_SERVER_SIDE_SOURCE (source), cache_dir);

	e_server_side_source_set_writable (E_SERVER_SIDE_SOURCE (source), TRUE);
	e_server_side_source_set_remote_deletable (E_SERVER_SIDE_SOURCE (source), TRUE);

	server = e_collection_backend_ref_server (E_COLLECTION_BACKEND (backend));
	e_source_registry_server_add_source (server, source);
	g_object_unref (server);
}

static gboolean
eee_backend_create_resource_sync (ECollectionBackend *backend,
                                  ESource *source,
                                  GCancellable *cancellable,
                                  GError **perror)
{
	xr_client_conn * conn;
	const gchar *username;
	gchar *eee_server, *server_uri;
	ESourceCollection *collection;
	ESource *collection_source;
	gboolean result = FALSE;

	collection_source = e_backend_get_source (E_BACKEND (backend));
	collection = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_COLLECTION);
	username = e_source_collection_get_identity (collection);

	eee_server = get_eee_server_hostname (username);
	if (eee_server) {
		server_uri = g_strdup_printf ("https://%s/RPC2", eee_server);
		g_free (eee_server);

		conn = xr_client_new (perror);
		if (conn) {
			if (xr_client_open (conn, server_uri, perror)) {
				if (ESClient_authenticate (conn, username, E_EEE_BACKEND (backend)->priv->password, perror)) {
					eee_create_calendar (E_EEE_BACKEND (backend), conn, username, source);
					result = TRUE;
				}

				xr_client_close (conn);
			}

			xr_client_free (conn);
		}

		g_free (server_uri);
	} else {
		g_set_error (perror, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to get 3e server address.");
	}

	return result;
}

static gboolean
eee_backend_delete_resource_sync (ECollectionBackend *backend,
                                  ESource *source,
                                  GCancellable *cancellable,
                                  GError **perror)
{
	xr_client_conn *conn;
	const gchar *username;
	gchar *eee_server, *server_uri;
	ESource *collection_source;
	ESourceCollection *collection;
	gboolean result = FALSE;

	collection_source = e_backend_get_source (E_BACKEND (backend));
	collection = e_source_get_extension (collection_source, E_SOURCE_EXTENSION_COLLECTION);
	username = e_source_collection_get_identity (collection);

	eee_server = get_eee_server_hostname (username);
	if (eee_server) {
		server_uri = g_strdup_printf ("https://%s/RPC2", eee_server);
		g_free (eee_server);

		conn = xr_client_new (perror);
		if (conn) {
			if (xr_client_open (conn, server_uri, perror)) {
				if (ESClient_authenticate (conn, username, E_EEE_BACKEND (backend)->priv->password, perror)) {
					ESourceExtension *extension;
					const gchar *resource_id, *calname;

					extension = e_source_get_extension (source, E_SOURCE_EXTENSION_RESOURCE);
					resource_id = e_source_resource_get_identity (E_SOURCE_RESOURCE (extension));
					calname = strchr (resource_id, ':');
					if (resource_id && calname && *(calname+1)) {
						ESourceRegistryServer *server;

						calname++;
						ESClient_deleteCalendar (conn, calname, NULL);

						server = e_collection_backend_ref_server (backend);
						e_source_registry_server_remove_source (server, source);
						g_object_unref (server);

						result = TRUE;
					} else {
						g_set_error (perror, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to get calendar ID.");
					}
				}

				xr_client_close (conn);
			}

			xr_client_free (conn);
		}

		g_free (server_uri);
	} else {
		g_set_error (perror, G_IO_ERROR, G_IO_ERROR_FAILED, "Failed to get 3e server address.");
	}

	return result;
}

static void
eee_backend_source_changed_cb (ESource *source, EEeeBackend *backend)
{
	if (!e_source_get_enabled (source)) {
		backend->priv->need_update_calendars = TRUE;
		return;
	}

	if (e_source_get_enabled (source) && e_backend_get_online (E_BACKEND (backend)) && backend->priv->need_update_calendars)
		eee_backend_queue_auth (backend);
}

static ESourceAuthenticationResult
eee_backend_try_password_sync (ESourceAuthenticator *authenticator,
                               const GString *password,
                               GCancellable *cancellable,
                               GError **perror)
{
	ESourceAuthenticationResult result;
	EEeeBackend *backend;
	xr_client_conn *conn;
	ESourceCollection *collection;
	const gchar *username;
	gchar *eee_server, *server_uri;
	ESource *source;

	result = E_SOURCE_AUTHENTICATION_ERROR;

	backend = E_EEE_BACKEND (authenticator);
	source = e_backend_get_source (E_BACKEND (backend));

	collection = e_source_get_extension (source, E_SOURCE_EXTENSION_COLLECTION);

	username = e_source_collection_get_identity (collection);

	eee_server = get_eee_server_hostname (username);
	if (eee_server) {
		server_uri = g_strdup_printf ("https://%s/RPC2", eee_server);
		g_free (eee_server);

		conn = xr_client_new (perror);
		if (conn) {
			if (xr_client_open (conn, server_uri, perror)) {
				result = E_SOURCE_AUTHENTICATION_REJECTED;

				if (ESClient_authenticate (conn, username, password->str, perror)) {
					result = E_SOURCE_AUTHENTICATION_ACCEPTED;

					if (backend->priv->password)
						g_free (backend->priv->password);
					backend->priv->password = g_strdup (password->str);

					eee_backend_get_calendars_list (backend, conn);
				}

				xr_client_close (conn);
			}

			xr_client_free (conn);
		}

		g_free (server_uri);
	}

	return result;
}

static void
eee_backend_authenticator_init (ESourceAuthenticatorInterface *interface)
{
	interface->try_password_sync = eee_backend_try_password_sync;
}

static void
eee_backend_dispose (GObject *object)
{
	g_free (E_EEE_BACKEND (object)->priv->password);

	/* Chain up to parent's dispose() method. */
	G_OBJECT_CLASS (e_eee_backend_parent_class)->dispose (object);
}

static void
eee_backend_constructed (GObject *object)
{
	ESource *source;

        /* Chain up to parent's constructed() method. */
        G_OBJECT_CLASS (e_eee_backend_parent_class)->constructed (object);

	source = e_backend_get_source (E_BACKEND (object));

	e_server_side_source_set_remote_creatable (E_SERVER_SIDE_SOURCE (source), TRUE);

	g_signal_connect (source, "changed", G_CALLBACK (eee_backend_source_changed_cb), object);
}

static void
e_eee_backend_class_init (EEeeBackendClass *class)
{
	GObjectClass *object_class;
	ECollectionBackendClass *backend_class;

	g_type_class_add_private (class, sizeof (EEeeBackendPrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->dispose = eee_backend_dispose;
	object_class->constructed = eee_backend_constructed;

	backend_class = E_COLLECTION_BACKEND_CLASS (class);
	backend_class->populate = eee_backend_populate;
	backend_class->dup_resource_id = eee_backend_dup_resource_id;
	backend_class->child_added = eee_backend_child_added;

	backend_class->create_resource_sync = eee_backend_create_resource_sync;
	backend_class->delete_resource_sync = eee_backend_delete_resource_sync;
}

static void
e_eee_backend_class_finalize (EEeeBackendClass *class)
{
}

static void
e_eee_backend_init (EEeeBackend *backend)
{
	backend->priv = E_EEE_BACKEND_GET_PRIVATE (backend);

	backend->priv->need_update_calendars = FALSE;
	backend->priv->password = NULL;
}

static void
eee_backend_factory_prepare_mail (ECollectionBackendFactory *factory,
                                  ESource *mail_account_source,
                                  ESource *mail_identity_source,
                                  ESource *mail_transport_source)
{
        ECollectionBackendFactoryClass *parent_class;

        /* Chain up to parent's prepare_mail() method. */
        parent_class =
                E_COLLECTION_BACKEND_FACTORY_CLASS (
                e_eee_backend_factory_parent_class);
        parent_class->prepare_mail (
                factory,
                mail_account_source,
                mail_identity_source,
                mail_transport_source);

//        google_backend_prepare_mail_account_source (mail_account_source);
//        google_backend_prepare_mail_transport_source (mail_transport_source);
}

static void
e_eee_backend_factory_class_init (EEeeBackendFactoryClass *class)
{
	ECollectionBackendFactoryClass *factory_class;

	factory_class = E_COLLECTION_BACKEND_FACTORY_CLASS (class);
	factory_class->factory_name = "eee";
	factory_class->backend_type = E_TYPE_EEE_BACKEND;
        factory_class->prepare_mail = eee_backend_factory_prepare_mail;
}

static void
e_eee_backend_factory_class_finalize (EEeeBackendFactoryClass *class)
{
}

static void
e_eee_backend_factory_init (EEeeBackendFactory *factory)
{
}

void e_cal_config_eee_type_register (GTypeModule *type_module);
void e_mail_config_eee_summary_type_register (GTypeModule *type_module);

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
	bindtextdomain (GETTEXT_PACKAGE, EXCHANGE_EEE_LOCALEDIR);
	bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");

	e_eee_backend_register_type (type_module);
	e_eee_backend_factory_register_type (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

