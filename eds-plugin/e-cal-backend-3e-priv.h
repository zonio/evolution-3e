/**************************************************************************************************
 *  3E plugin for Evolution Data Server                                                           * 
 *                                                                                                *
 *  Copyright (C) 2007 by Zonio                                                                   *
 *  www.zonio.net                                                                                 *
 *  stanislav.slusny@zonio.net                                                                    *
 *                                                                                                *
 **************************************************************************************************/

#ifndef E_CAL_BACKEND_3E_PRIV
#define E_CAL_BACKEND_3E_PRIV

#include <config.h>

#include <gconf/gconf-client.h>
#include <libedata-cal/e-cal-backend-cache.h>

#include "interface/ESClient.xrc.h"

/** @addtogroup eds_sync */
/** @{ */
typedef enum {
	SYNC_SLEEP,    /**< Sync is paused (offline mode). */
	SYNC_WORK,     /**< Sync is active (online mode). */    
} SyncMode;
/** @} */

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
  /** @} */

  /* Calendar */
  gboolean is_loaded;
  gboolean is_owned;
  char *calname;
  char *owner;
  gboolean has_write_permission;
  char *calspec;
  GConfClient *gconf;
  guint source_changed_perm;
  CalMode mode;
  ECalBackendCache *cache;
  ECalComponent *settings;
  icaltimezone *default_zone;

  /** @addtogroup eds_sync */
  /** @{ */
  gboolean sync_terminated;      /**< Flag used to stop sync thread. */
  GCond *sync_cond;              /**< Thread notification system. */
  GMutex *sync_mutex;            /**< Mutex guarding cache data. */
  GThread *sync_thread;          /**< Sync thread. */
  SyncMode sync_mode;            /**< Sync mode (SYNC_SLEEP/SYNC_WORK). */
  char *sync_stamp;              /**< Last synchronization timestamp (XXX: format?). */
  GList *sync_clients_changes;   /**< XXX: Huh, what's this for?. Shouldn't it be tracked in cache? */
  /** @} */
};

#endif
