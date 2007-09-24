/**************************************************************************************************
 *  3E plugin for Evolution Data Server                                                           * 
 *                                                                                                *
 *  Copyright (C) 2007 by Zonio                                                                   *
 *  www.zonio.net                                                                                 *
 *  stanislav.slusny@zonio.net                                                                    *
 *                                                                                                *
 **************************************************************************************************/

#ifndef E_CAL_BACKEND_3E_H
#define E_CAL_BACKEND_3E_H

#include <libedata-cal/e-cal-backend-sync.h>

G_BEGIN_DECLS

#define E_TYPE_CAL_BACKEND_3E            (e_cal_backend_3e_get_type ())
#define E_CAL_BACKEND_3E(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), E_TYPE_CAL_BACKEND_3E, \
                                         ECalBackend3e))
#define E_CAL_BACKEND_3E_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), E_TYPE_CAL_BACKEND_3E,  \
                                         ECalBackend3eClass))
#define E_IS_CAL_BACKEND_3E(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), E_TYPE_CAL_BACKEND_3E))
#define E_IS_CAL_BACKEND_3E_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), E_TYPE_CAL_BACKEND_3E))

typedef struct _ECalBackend3e ECalBackend3e;
typedef struct _ECalBackend3eClass ECalBackend3eClass;
typedef struct _ECalBackend3ePrivate ECalBackend3ePrivate;

struct _ECalBackend3e
{
  ECalBackendSync backend;
  ECalBackend3ePrivate *priv;
};

struct _ECalBackend3eClass
{
  ECalBackendSyncClass parent_class;
};

GType e_cal_backend_3e_get_type(void);

G_END_DECLS

#endif
