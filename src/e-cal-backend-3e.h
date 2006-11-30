/* Evolution calendar - iCalendar file backend
 *
 * Copyright (C) 2000 Ximian, Inc.
 * Copyright (C) 2000 Ximian, Inc.
 *
 * Author: Federico Mena-Quintero <federico@ximian.com>
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
 */

#ifndef E_CAL_BACKEND_3E_H
#define E_CAL_BACKEND_3E_H

#include <libedata-cal/e-cal-backend-sync.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_3E            (e_cal_backend_3e_get_type ())
#define E_CAL_BACKEND_3E(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_3E,		\
					  ECalBackend3e))
#define E_CAL_BACKEND_3E_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_3E,	\
					  ECalBackend3eClass))
#define E_IS_CAL_BACKEND_3E(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_3E))
#define E_IS_CAL_BACKEND_3E_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_3E))

typedef struct _ECalBackend3e ECalBackend3e;
typedef struct _ECalBackend3eClass ECalBackend3eClass;

typedef struct _ECalBackend3ePrivate ECalBackend3ePrivate;

struct _ECalBackend3e {
	ECalBackendSync backend;

	/* Private data */
	ECalBackend3ePrivate *priv;
};

struct _ECalBackend3eClass {
	ECalBackendSyncClass parent_class;
};

GType       e_cal_backend_3e_get_type      (void);

G_END_DECLS

#endif
