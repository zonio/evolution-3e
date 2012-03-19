/*
 * Zonio 3e calendar plugin
 *
 * Copyright (C) 2008-2010 Zonio s.r.o <developers@zonio.net>
 *
 * This file is part of evolution-3e.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "e-cal-backend-3e-factory.h"
#include "e-cal-backend-3e.h"
#include <xr-lib.h>

#include <libedataserver/e-list-iterator.h>
#include <libedata-cal/e-cal-backend-cache.h>

typedef struct
{
    ECalBackendFactory parent_object;
} ECalBackend3eFactory;

typedef struct
{
    ECalBackendFactoryClass parent_class;
} ECalBackend3eFactoryClass;

static const char *get_protocol(ECalBackendFactory *factory)
{
    return "eee";
}

static ECalBackend *new_backend(ECalBackendFactory *factory, ESource *source)
{
    return g_object_new(e_cal_backend_3e_get_type(), "source", source, "kind", ICAL_VEVENT_COMPONENT, NULL);
}

static icalcomponent_kind get_kind(ECalBackendFactory *factory)
{
    return ICAL_VEVENT_COMPONENT;
}

static void backend_factory_class_init(ECalBackend3eFactoryClass *klass)
{
    E_CAL_BACKEND_FACTORY_CLASS(klass)->get_protocol = get_protocol;
    E_CAL_BACKEND_FACTORY_CLASS(klass)->get_kind = get_kind;
    E_CAL_BACKEND_FACTORY_CLASS(klass)->new_backend = new_backend;
}

static GType backend_factory_get_type(GTypeModule *module)
{
    GType type;

    GTypeInfo info = {
        sizeof(ECalBackend3eFactoryClass),
        NULL,                                       /* base_class_init */
        NULL,                                       /* base_class_finalize */
        (GClassInitFunc)backend_factory_class_init,
        NULL,                                       /* class_finalize */
        NULL,                                       /* class_data */
        sizeof(ECalBackend),                        /* ??? */
        0,                                          /* n_preallocs */
        (GInstanceInitFunc)NULL,
        NULL
    };

    type = g_type_module_register_type(module, E_TYPE_CAL_BACKEND_FACTORY, "ECalBackend3eFactory", &info, 0);

    return type;
}

/* eds module API */

static GType eee_types[1];

void eds_module_list_types(const GType * *types, int *num_types)
{
    *types = eee_types;
    *num_types = 1;
}

void eds_module_initialize(GTypeModule *module)
{
    xr_init();

    //xr_debug_enabled = XR_DEBUG_CALL;

    // make some types thread safe, add more if you see g_type_plugin_*() bug
    g_type_class_ref(E_TYPE_CAL_COMPONENT);
    g_type_class_ref(E_TYPE_CAL_BACKEND_CACHE);
    g_type_class_ref(E_TYPE_LIST_ITERATOR);
    g_type_class_ref(E_TYPE_LIST);
    icaltimezone_get_utc_timezone();

    eee_types[0] = backend_factory_get_type(module);
}

void eds_module_shutdown(void)
{
}
