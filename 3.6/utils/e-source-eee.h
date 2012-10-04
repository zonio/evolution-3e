/*
 * e-source-eee.h
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

#ifndef E_SOURCE_EEE_H
#define E_SOURCE_EEE_H

#include <libedataserver/libedataserver.h>

/* Standard GObject macros */
#define E_TYPE_SOURCE_EEE \
	(e_source_eee_get_type ())
#define E_SOURCE_EEE(obj) \
	(G_TYPE_CHECK_INSTANCE_CAST \
	((obj), E_TYPE_SOURCE_EEE, ESourceEee))
#define E_SOURCE_EEE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_CAST \
	((cls), E_TYPE_SOURCE_EEE, ESourceEeeClass))
#define E_IS_SOURCE_EEE(obj) \
	(G_TYPE_CHECK_INSTANCE_TYPE \
	((obj), E_TYPE_SOURCE_EEE))
#define E_IS_SOURCE_EEE_CLASS(cls) \
	(G_TYPE_CHECK_CLASS_TYPE \
	((cls), E_TYPE_SOURCE_EEE))
#define E_SOURCE_EEE_GET_CLASS(obj) \
	(G_TYPE_INSTANCE_GET_CLASS \
	((obj), E_TYPE_SOURCE_EEE, ESourceEeeClass))

#define E_SOURCE_EXTENSION_EEE "Exchange Eee"

G_BEGIN_DECLS

typedef struct _ESourceEee ESourceEee;
typedef struct _ESourceEeeClass ESourceEeeClass;
typedef struct _ESourceEeePrivate ESourceEeePrivate;

struct _ESourceEee {
	ESourceExtension parent;
	ESourceEeePrivate *priv;
};

struct _ESourceEeeClass {
	ESourceExtensionClass parent_class;
};

enum { EEE_PERM_READ = 1, EEE_PERM_READWRITE };

GType		e_source_eee_get_type		(void) G_GNUC_CONST;
void		e_source_eee_type_register	(GTypeModule *type_module);

void e_source_eee_add_user_perm (ESourceEee *extension, const gchar *user, glong perm);
glong e_source_eee_get_user_perm (ESourceEee *extension, const gchar *user);
void e_source_eee_delete_user_perms (ESourceEee *extension);
void e_source_eee_notify_user_perms (ESourceEee *extension);
void e_source_eee_foreach_user_perm (ESourceEee *extension, void (*func) (const gchar *name, glong perm));

void e_source_eee_add_group_perm (ESourceEee *extension, const gchar *group, glong perm);
glong e_source_eee_get_group_perm (ESourceEee *extension, const gchar *group);
void e_source_eee_delete_group_perms (ESourceEee *extension);
void e_source_eee_notify_group_perms (ESourceEee *extension);
void e_source_eee_foreach_group_perm (ESourceEee *extension, void (*func) (const gchar *name, glong perm));

G_END_DECLS

#endif /* E_SOURCE_EEE_H */
