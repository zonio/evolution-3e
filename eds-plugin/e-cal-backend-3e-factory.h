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
