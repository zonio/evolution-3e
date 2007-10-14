/***************************************************************************
 *  3E plugin for Evolution Data Server                                    *
 *                                                                         *
 *  Copyright (C) 2007 by Zonio                                            *
 *  www.zonio.net                                                          *
 *  Stanislav Slusny <stanislav.slusny@zonio.net>                          *
 *  Ondrej Jirman <ondrej.jirman@zonio.net>                                *
 *                                                                         *
 ***************************************************************************/

#ifndef __E_CAL_BACKEND_3E_PRIV__
#define __E_CAL_BACKEND_3E_PRIV__

#include "config.h"
#include <string.h>
#include <libedata-cal/e-cal-backend-cache.h>
#include "e-cal-backend-3e.h"
#include "interface/ESClient.xrc.h"

/** Private 3E calendar backend data.
 *
 * This is shared by 3 sets of functions, @ref eds_conn, @ref eds_sync and EDS
 * 3E Backend itself.
 */
struct _ECalBackend3ePrivate
{
  /** @addtogroup eds_conn */
  /** @{ */
  gboolean is_open;              /**< Connection is open. */
  xr_client_conn *conn;          /**< Connection object. */
  char *server_uri;              /**< Server URI (automatically detected from DNS TXT). */
  char *username;                /**< Username for the 3E account. */
  char *password;                /**< Password for the 3E account. */
  gboolean last_conn_failed;     /**< TRUE if last connection failed. */
  GStaticRecMutex conn_mutex;
  /** @} */

  /** @addtogroup eds_cal */
  /** @{ */
  char *calname;                 /**< Calendar name. */
  char *owner;                   /**< Calendar owner. */
  char *perm;                    /**< Calendar permission. */
  char *calspec;                 /**< Calspec ('owner:name'), just for convenience. */
  /** @} */

  /** @addtogroup eds_back */
  /** @{ */
  CalMode mode;                  /**< Calendar mode (CAL_MODE_REMOTE, CAL_MODE_LOCAL). */
  gboolean is_loaded;            /**< Calendar is in loaded state. (connection set up, calinfo loaded) */
  ECalBackendCache *cache;       /**< Calendar cache object. */
  GStaticRWLock cache_lock;      /**< RW mutex for backend cache object. */
  icaltimezone *default_zone;    /**< Temporary store for this session's default timezone. */
  gboolean sync_immediately;     /**< If TRUE, e_cal_backend_3e_sync_cache_to_server() is run after cache mod operations. */
  /** @} */

  /** @addtogroup eds_sync */
  /** @{ */
  guint sync_id;
  /** @} */
};

/** @addtogroup eds_misc */
/** @{ */
typedef enum {
  E_CAL_COMPONENT_CACHE_STATE_NONE = 0,
  E_CAL_COMPONENT_CACHE_STATE_CREATED,
  E_CAL_COMPONENT_CACHE_STATE_MODIFIED,
  E_CAL_COMPONENT_CACHE_STATE_REMOVED
} ECalComponentCacheState;
/** @} */

/* server connection */
gboolean e_cal_backend_3e_setup_connection(ECalBackend3e* cb, const char* username, const char* password, gboolean test_conn, GError** err);
gboolean e_cal_backend_3e_open_connection(ECalBackend3e* cb, GError** err);
void e_cal_backend_3e_close_connection(ECalBackend3e* cb);
gboolean e_cal_backend_3e_connection_is_open(ECalBackend3e* cb);
void e_cal_backend_3e_free_connection(ECalBackend3e* cb);

/* calendar info */
gboolean e_cal_backend_3e_calendar_info_load(ECalBackend3e* cb);
gboolean e_cal_backend_3e_calendar_is_owned(ECalBackend3e* cb);
gboolean e_cal_backend_3e_calendar_is_online(ECalBackend3e* cb);
gboolean e_cal_backend_3e_calendar_needs_immediate_sync(ECalBackend3e* cb);
gboolean e_cal_backend_3e_calendar_has_perm(ECalBackend3e* cb, const char* perm);
void e_cal_backend_3e_calendar_set_perm(ECalBackend3e* cb, const char* perm);
gboolean e_cal_backend_3e_calendar_load_perm(ECalBackend3e* cb);

/* cache wrappers */
gboolean e_cal_backend_3e_cache_put_component (ECalBackend3e* cb, ECalBackendCache *cache, ECalComponent *comp);
gboolean e_cal_backend_3e_cache_remove_component (ECalBackend3e* cb, ECalBackendCache *cache, const char *uid, const char *rid);
ECalComponent *e_cal_backend_3e_cache_get_component (ECalBackend3e* cb, ECalBackendCache *cache, const char *uid, const char *rid);
GList *e_cal_backend_3e_cache_get_components (ECalBackend3e* cb, ECalBackendCache *cache);
GSList *e_cal_backend_3e_cache_get_components_by_uid (ECalBackend3e* cb, ECalBackendCache *cache, const char *uid);
const icaltimezone *e_cal_backend_3e_cache_get_timezone (ECalBackend3e* cb, ECalBackendCache *cache, const char *tzid);
gboolean e_cal_backend_3e_cache_put_timezone (ECalBackend3e* cb, ECalBackendCache *cache, const icaltimezone *zone);

/* sync API */
gboolean e_cal_backend_3e_sync_cache_to_server(ECalBackend3e* cb);
gboolean e_cal_backend_3e_sync_server_to_cache(ECalBackend3e* cb);
void e_cal_backend_3e_periodic_sync_enable(ECalBackend3e* cb);
void e_cal_backend_3e_periodic_sync_disable(ECalBackend3e* cb);

/* component cache state */
void e_cal_component_set_cache_state(ECalComponent* comp, ECalComponentCacheState state);
ECalComponentCacheState e_cal_component_get_cache_state(ECalComponent* comp);
void icalcomponent_set_cache_state(icalcomponent* comp, int state);
int icalcomponent_get_cache_state(icalcomponent* comp);

/* misc */
const char* icalcomponent_get_tzid(icalcomponent* comp);
gboolean icalcomponent_3e_status_is_deleted(icalcomponent* comp);
void icalcomponent_collect_recipients(icalcomponent* icomp, const char* organizer, GSList** recipients);
void e_cal_backend_notify_gerror_error(ECalBackend * backend, char *message, GError* err);
gboolean e_cal_component_match_id(ECalComponent* comp, ECalComponentId* id);
gboolean e_cal_component_id_compare(ECalComponentId* id1, ECalComponentId* id2);

#endif
