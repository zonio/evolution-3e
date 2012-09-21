/*
 * Author: Ondrej Jirman <ondrej.jirman@zonio.net>
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

#include <glib-object.h>
#include "e-cal-backend-3e.h"
#include <xr-lib.h>

#include <libedataserver/e-list-iterator.h>
#include <libedataserver/e-categories.h>
#include <libedata-cal/e-cal-backend-cache.h>
#include <libedata-cal/e-cal-backend-factory.h>

typedef ECalBackendFactory ECalBackend3eFactory;
typedef ECalBackendFactoryClass ECalBackend3eFactoryClass;

GType e_cal_backend_3e_factory_get_type (void);

G_DEFINE_DYNAMIC_TYPE (
    ECalBackend3eFactory,
    e_cal_backend_3e_factory,
    E_TYPE_CAL_BACKEND_FACTORY
    )

static void
e_cal_backend_3e_factory_class_init(ECalBackend3eFactoryClass *class)
{
    class->factory_name = "eee";
    class->component_kind = ICAL_VEVENT_COMPONENT;
    class->backend_type = E_TYPE_CAL_BACKEND_3E;
}

static void
e_cal_backend_3e_factory_class_finalize (ECalBackendFactoryClass *class)
{
}

static void
e_cal_backend_3e_factory_init (ECalBackendFactory *factory)
{
}

G_MODULE_EXPORT void
e_module_load (GTypeModule *type_module)
{
    xr_init();

    //xr_debug_enabled = XR_DEBUG_CALL;
    // make some types thread safe, add more if you see g_type_plugin_*() bug
    g_type_class_ref(E_TYPE_CAL_COMPONENT);
    g_type_class_ref(E_TYPE_CAL_BACKEND_CACHE);
    g_type_class_ref(E_TYPE_LIST_ITERATOR);
    g_type_class_ref(E_TYPE_LIST);
    icaltimezone_get_utc_timezone();

    e_cal_backend_3e_factory_register_type (type_module);

    GList *l, *saved_cats;
    gchar *filename;

    saved_cats = e_categories_get_list ();

    for (l = saved_cats; l; l = g_list_next (l)) {
        if (!g_strcmp0 ((const gchar *) l->data, "outofsync"))
            goto exit;
    }

    filename = g_build_filename (E_DATA_SERVER_IMAGESDIR, "category_outofsync_16.png", NULL);
    e_categories_add ("outofsync", NULL, filename, FALSE);
    g_free (filename);

exit:
    g_list_free (saved_cats);
}

G_MODULE_EXPORT void
e_module_unload (GTypeModule *type_module)
{
}
