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

#ifndef _E_CAL_BACKEND_3E_FACTORY_H_
#define _E_CAL_BACKEND_3E_FACTORY_H_

#include <glib-object.h>
#include <libedata-cal/e-cal-backend-factory.h>

G_BEGIN_DECLS

void                 eds_module_initialize(GTypeModule *module);
void                 eds_module_shutdown(void);
void                 eds_module_list_types(const GType * *types, int *num_types);

G_END_DECLS

#endif
