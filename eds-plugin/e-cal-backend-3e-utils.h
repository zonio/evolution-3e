/**************************************************************************************************
 *  3E plugin for Evolution Data Server                                                           * 
 *                                                                                                *
 *  Copyright (C) 2007 by Zonio                                                                   *
 *  www.zonio.net                                                                                 *
 *  stanislav.slusny@zonio.net                                                                    *
 *                                                                                                *
 **************************************************************************************************/

#ifndef E_CAL_BACKEND_3E_UTILS
#define  E_CAL_BACKEND_3E_UTILS

#include <string.h>
#include "e-cal-backend-3e.h"

#define DEBUG 1
#ifdef DEBUG
#include <syslog.h>
//#define D(fmt, args...) syslog(LOG_DEBUG, "DEBUG: %s " fmt, G_STRLOC, ## args)
//#define T(fmt, args...) syslog(LOG_DEBUG, "TRACE: %s(" fmt ")", G_STRFUNC, ## args)
#define D(fmt, args...) g_debug("DEBUG: %s " fmt, G_STRLOC, ## args)
#define T(fmt, args...) g_debug("TRACE: %s(" fmt ")", G_STRFUNC, ## args)
#else
#define D(fmt, args...)
#define T(fmt, args...)
#endif

#define DEFAULT_REFRESH_TIME 30
#define CALENDAR_SOURCES "/apps/evolution/calendar/sources"
#define EEE_URI_PREFIX "eee://"
#define EEE_PASSWORD_COMPONENT "3E Account"


struct ECalBackend;

typedef enum
{
  E_CAL_COMPONENT_IN_SYNCH,
  E_CAL_COMPONENT_LOCALLY_CREATED,
  E_CAL_COMPONENT_LOCALLY_DELETED,
  E_CAL_COMPONENT_LOCALLY_MODIFIED, 
} ECalComponentSyncState;

// set property in ical component
void icomp_x_prop_set(icalcomponent *comp,
                      const char *key,
                      const char *value);

// get value of ical property
const char * icomp_x_prop_get (icalcomponent *comp,
                               const char *key);

const char*
icomp_get_uid(icalcomponent* comp);

// extract X-3e-STATUS property
gboolean icomp_get_deleted_status(icalcomponent* comp);

// set internal synchro state
void e_cal_component_set_sync_state(ECalComponent *comp,
                                    ECalComponentSyncState state);

// get internal synchro state
ECalComponentSyncState e_cal_component_get_sync_state(ECalComponent* comp);

void e_cal_component_get_ids(ECalComponent* comp,
                             const gchar** uid,
                             const gchar** rid);

gint e_cal_component_compare(gconstpointer ptr1,
                             gconstpointer ptr2);


void e_cal_backend_notify_gerror_error(ECalBackend * backend,
                                       char *message,
                                       GError* err);
void
e_cal_component_set_stamp(ECalComponent *comp,
                          const gchar* stamp);

const gchar*
e_cal_component_get_stamp(ECalComponent* comp);

gboolean e_cal_has_write_permission(const char* perm_string);

void e_cal_component_set_local_state(ECalBackend * backend, ECalComponent* comp);

void e_cal_component_unset_local_state(ECalBackend* backend, ECalComponent* comp);


gboolean e_cal_component_is_local(ECalComponent* comp);

ECalComponentSyncState icomp_get_sync_state(icalcomponent* comp);

time_t e_cal_component_get_dtstamp_as_timet(ECalComponent* comp);

time_t icomp_get_dtstamp_as_timet(icalcomponent* comp);

gboolean e_cal_component_has_deleted_status(ECalComponent* comp);

void icomp_set_sync_state(icalcomponent* icomp, ECalComponentSyncState state);
#endif
