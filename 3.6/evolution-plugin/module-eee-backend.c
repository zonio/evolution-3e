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

/* Standard GObject macros */
#define E_TYPE_EEE_BACKEND \
	(e_eee_backend_get_type ())
#define E_EEE_BACKEND(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_EEE_BACKEND, EEeeBackend))

typedef struct _EEeeBackend EEeeBackend;
typedef struct _EEeeBackendClass EEeeBackendClass;

typedef struct _EEeeBackendFactory EEeeBackendFactory;
typedef struct _EEeeBackendFactoryClass EEeeBackendFactoryClass;

struct _EEeeBackend {
	ECollectionBackend parent;
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

G_DEFINE_DYNAMIC_TYPE (
	EEeeBackend,
	e_eee_backend,
	E_TYPE_COLLECTION_BACKEND)

G_DEFINE_DYNAMIC_TYPE (
	EEeeBackendFactory,
	e_eee_backend_factory,
	E_TYPE_COLLECTION_BACKEND_FACTORY)

static void
eee_backend_add_calendar (ECollectionBackend *backend)
{
	ESource *source;
	ESource *collection_source;
	ESourceRegistryServer *server;
	ESourceExtension *extension;
	ESourceCollection *collection_extension;
	const gchar *backend_name;
	const gchar *extension_name;
	const gchar *identity;
	const gchar *resource_id;
	gchar *path;

	/* FIXME As a future enhancement, we should query Eee
	 *       for a list of user calendars and add them to the
	 *       collection with matching display names and colors. */

	collection_source = e_backend_get_source (E_BACKEND (backend));
/*
	resource_id = GOOGLE_CALENDAR_RESOURCE_ID;
	source = e_collection_backend_new_child (backend, resource_id);
	e_source_set_display_name (source, _("Calendar"));

	collection_extension = e_source_get_extension (
		collection_source, E_SOURCE_EXTENSION_COLLECTION);
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
	GList *list;

	list = e_collection_backend_list_calendar_sources (backend);
	if (list == NULL)
		eee_backend_add_calendar (backend);
	g_list_free_full (list, (GDestroyNotify) g_object_unref);

	/* Chain up to parent's populate() method. */
	E_COLLECTION_BACKEND_CLASS (e_eee_backend_parent_class)->
		populate (backend);
}

static gchar *
eee_backend_dup_resource_id (ECollectionBackend *backend,
                                ESource *child_source)
{
	const gchar *extension_name;

	/* XXX This is trivial for now since we only
	 *     add one calendar and one address book. */

	extension_name = E_SOURCE_EXTENSION_CALENDAR;
	if (e_source_has_extension (child_source, extension_name))
		return g_strdup ("kalendari nazov");

	return NULL;
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
e_eee_backend_class_init (EEeeBackendClass *class)
{
	ECollectionBackendClass *backend_class;

	backend_class = E_COLLECTION_BACKEND_CLASS (class);
	backend_class->populate = eee_backend_populate;
	backend_class->dup_resource_id = eee_backend_dup_resource_id;
	backend_class->child_added = eee_backend_child_added;
}

static void
e_eee_backend_class_finalize (EEeeBackendClass *class)
{
}

static void
e_eee_backend_init (EEeeBackend *backend)
{
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
	e_eee_backend_register_type (type_module);
	e_eee_backend_factory_register_type (type_module);
        e_cal_config_eee_type_register (type_module);
        e_mail_config_eee_summary_type_register (type_module);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}

