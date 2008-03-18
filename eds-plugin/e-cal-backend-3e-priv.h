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
  volatile gint sync_request;    /**< Sync state/request. */
  GThread* sync_thread;          /**< Sync thread. */
  GMutex* sync_mutex;            /**< Protects access to the sync_thread. */
  time_t sync_timestamp;         /**< Last sync time (local time). */
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
gboolean e_cal_backend_3e_setup_connection(ECalBackend3e* cb, const char* username, const char* password);
gboolean e_cal_backend_3e_open_connection(ECalBackend3e* cb, GError** err);
void e_cal_backend_3e_close_connection(ECalBackend3e* cb);
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

time_t e_cal_backend_3e_get_sync_timestamp(ECalBackend3e* cb);
void e_cal_backend_3e_set_sync_timestamp(ECalBackend3e* cb, time_t stamp);
void e_cal_backend_3e_periodic_sync_enable(ECalBackend3e* cb);
void e_cal_backend_3e_periodic_sync_disable(ECalBackend3e* cb);
void e_cal_backend_3e_periodic_sync_stop(ECalBackend3e* cb);
void e_cal_backend_3e_do_immediate_sync(ECalBackend3e* cb);

/* component cache state */
void e_cal_component_set_cache_state(ECalComponent* comp, ECalComponentCacheState state);
ECalComponentCacheState e_cal_component_get_cache_state(ECalComponent* comp);
void icalcomponent_set_cache_state(icalcomponent* comp, int state);
int icalcomponent_get_cache_state(icalcomponent* comp);

/* misc */
const char* icalcomponent_get_tzid(icalcomponent* comp);
gboolean icalcomponent_3e_status_is_deleted(icalcomponent* comp);
icalproperty_method icalcomponent_get_itip_method(icalcomponent* comp);
icalcomponent* icalcomponent_get_itip_payload(icalcomponent* comp);
void icalcomponent_collect_recipients(icalcomponent* icomp, const char* sender, GSList** recipients);
void e_cal_backend_notify_gerror_error(ECalBackend * backend, char *message, GError* err);
gboolean e_cal_component_match_id(ECalComponent* comp, ECalComponentId* id);
gboolean e_cal_component_id_compare(ECalComponentId* id1, ECalComponentId* id2);

/* attachment and attachment storage API */

ECalComponent* e_cal_backend_3e_convert_attachment_uris_to_local(ECalBackend3e* cb, ECalComponent* comp);
ECalComponent* e_cal_backend_3e_convert_attachment_uris_to_remote(ECalBackend3e* cb, ECalComponent* comp);
gboolean e_cal_backend_3e_upload_attachments(ECalBackend3e* cb, ECalComponent* comp, GError** err);
gboolean e_cal_backend_3e_download_attachments(ECalBackend3e* cb, ECalComponent* comp, GError** err);

/* message queue API */

gboolean e_cal_backend_3e_push_message(ECalBackend3e* cb, const char* object);
gboolean e_cal_backend_3e_pop_message(ECalBackend3e* cb, const char* object);
const char* e_cal_backend_3e_get_message(ECalBackend3e* cb);
gboolean e_cal_backend_3e_send_message(ECalBackend3e* cb, const char* object, GError** err);
gboolean e_cal_backend_3e_process_message_queue(ECalBackend3e* cb);

#endif
