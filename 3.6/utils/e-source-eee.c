/*
 * e-source-eee.c
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

#include "e-source-eee.h"

#define E_SOURCE_EEE_GET_PRIVATE(obj) \
	(G_TYPE_INSTANCE_GET_PRIVATE \
	((obj), E_TYPE_SOURCE_EEE, ESourceEeePrivate))

struct _ESourceEeePrivate {
	GMutex *property_lock;
	GHashTable *user_perms, *group_perms;
};

enum {
	PROP_0,
	PROP_USER_PERMS,
	PROP_GROUP_PERMS
};

G_DEFINE_DYNAMIC_TYPE (
	ESourceEee,
	e_source_eee,
	E_TYPE_SOURCE_EXTENSION)

static void
source_eee_set_property (GObject *object,
				 guint property_id,
                                 const GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_USER_PERMS:
		case PROP_GROUP_PERMS:
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_eee_get_property (GObject *object,
				 guint property_id,
                                 GValue *value,
                                 GParamSpec *pspec)
{
	switch (property_id) {
		case PROP_USER_PERMS:
		case PROP_GROUP_PERMS:
			g_value_set_boolean (value, FALSE);
			return;
	}

	G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
}

static void
source_eee_finalize (GObject *object)
{
	ESourceEeePrivate *priv;

	priv = E_SOURCE_EEE_GET_PRIVATE (object);

	g_hash_table_destroy (priv->user_perms);
	g_hash_table_destroy (priv->group_perms);
	g_mutex_free (priv->property_lock);

	/* Chain up to parent's finalize() method. */
	G_OBJECT_CLASS (e_source_eee_parent_class)->finalize (object);
}

static void
e_source_eee_class_init (ESourceEeeClass *class)
{
	GObjectClass *object_class;
	ESourceExtensionClass *extension_class;

	g_type_class_add_private (class, sizeof (ESourceEeePrivate));

	object_class = G_OBJECT_CLASS (class);
	object_class->set_property = source_eee_set_property;
	object_class->get_property = source_eee_get_property;
	object_class->finalize = source_eee_finalize;

	extension_class = E_SOURCE_EXTENSION_CLASS (class);
	extension_class->name = E_SOURCE_EXTENSION_EEE;

	g_object_class_install_property (
		object_class,
		PROP_USER_PERMS,
		g_param_spec_boolean (
			"user-perms",
			"user permissions",
			"user permissions",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));

	g_object_class_install_property (
		object_class,
		PROP_GROUP_PERMS,
		g_param_spec_boolean (
			"group-perms",
			"group permissions",
			"group permissions",
			FALSE,
			G_PARAM_READWRITE |
			G_PARAM_STATIC_STRINGS |
			E_SOURCE_PARAM_SETTING));
}

static void
e_source_eee_class_finalize (ESourceEeeClass *class)
{
}

static void
e_source_eee_init (ESourceEee *extension)
{
	extension->priv = E_SOURCE_EEE_GET_PRIVATE (extension);
	extension->priv->property_lock = g_mutex_new ();

	extension->priv->user_perms = g_hash_table_new (g_str_hash, g_str_equal);
	extension->priv->group_perms = g_hash_table_new (g_str_hash, g_str_equal);
}

void
e_source_eee_type_register (GTypeModule *type_module)
{
	/* XXX G_DEFINE_DYNAMIC_TYPE declares a static type registration
	 *     function, so we have to wrap it with a public function in
	 *     order to register types from a separate compilation unit. */
	e_source_eee_register_type (type_module);
}

void
e_source_eee_add_user_perm (ESourceEee *extension, const gchar *user, glong perm)
{
	g_return_if_fail (E_IS_SOURCE_EEE (extension));

	g_mutex_lock (extension->priv->property_lock);
	g_hash_table_replace (extension->priv->user_perms, (gpointer) user, (gpointer) perm);
	g_mutex_unlock (extension->priv->property_lock);
}

glong
e_source_eee_get_user_perm (ESourceEee *extension, const gchar *user)
{
	gpointer ret;

	g_return_val_if_fail (E_IS_SOURCE_EEE (extension), 0);

	g_mutex_lock (extension->priv->property_lock);
	ret = g_hash_table_lookup (extension->priv->user_perms, user);
	g_mutex_unlock (extension->priv->property_lock);

	return (glong) ret;
}

void
e_source_eee_delete_user_perms (ESourceEee *extension)
{
	g_return_if_fail (E_IS_SOURCE_EEE (extension));

	g_mutex_lock (extension->priv->property_lock);
	g_hash_table_remove_all (extension->priv->user_perms);
	g_mutex_unlock (extension->priv->property_lock);
}

void
e_source_eee_notify_user_perms (ESourceEee *extension)
{
	g_return_if_fail (E_IS_SOURCE_EEE (extension));

	g_object_notify (G_OBJECT (extension), "user-perms");
}

void
e_source_eee_foreach_user_perm (ESourceEee *extension, void (*func) (const gchar *name, glong perm))
{
	GHashTableIter iter;
	gpointer key, value;

	g_return_if_fail (E_IS_SOURCE_EEE (extension));
	g_return_if_fail (func != NULL);

	g_mutex_lock (extension->priv->property_lock);
	g_hash_table_iter_init (&iter, extension->priv->user_perms);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		func ((const gchar *) key, (glong) value);
	}
	g_mutex_unlock (extension->priv->property_lock);
}

void
e_source_eee_add_group_perm (ESourceEee *extension, const gchar *group, glong perm)
{
	g_return_if_fail (E_IS_SOURCE_EEE (extension));

	g_mutex_lock (extension->priv->property_lock);
	g_hash_table_replace (extension->priv->group_perms, (gpointer) group, (gpointer) perm);
	g_mutex_unlock (extension->priv->property_lock);
}

glong
e_source_eee_get_group_perm (ESourceEee *extension, const gchar *group)
{
	gpointer ret;

	g_return_val_if_fail (E_IS_SOURCE_EEE (extension), 0);

	g_mutex_lock (extension->priv->property_lock);
	ret = g_hash_table_lookup (extension->priv->group_perms, group);
	g_mutex_unlock (extension->priv->property_lock);

	return (glong) ret;
}

void
e_source_eee_delete_group_perms (ESourceEee *extension)
{
	g_return_if_fail (E_IS_SOURCE_EEE (extension));

	g_mutex_lock (extension->priv->property_lock);
	g_hash_table_remove_all (extension->priv->group_perms);
	g_mutex_unlock (extension->priv->property_lock);
}

void
e_source_eee_notify_group_perms (ESourceEee *extension)
{
	g_return_if_fail (E_IS_SOURCE_EEE (extension));

	g_object_notify (G_OBJECT (extension), "group-perms");
}

void
e_source_eee_foreach_group_perm (ESourceEee *extension, void (*func) (const gchar *name, glong perm))
{
	GHashTableIter iter;
	gpointer key, value;

	g_return_if_fail (E_IS_SOURCE_EEE (extension));
	g_return_if_fail (func != NULL);

	g_mutex_lock (extension->priv->property_lock);
	g_hash_table_iter_init (&iter, extension->priv->group_perms);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		func ((const gchar *) key, (glong) value);
	}
	g_mutex_unlock (extension->priv->property_lock);
}

