/***************************************************************************
 *  3E plugin for Evolution Data Server                                    *
 *                                                                         *
 *  Copyright (C) 2007 by Zonio                                            *
 *  www.zonio.net                                                          *
 *  Stanislav Slusny <stanislav.slusny@zonio.net>                          *
 *  Ondrej Jirman <ondrej.jirman@zonio.net>                                *
 *                                                                         *
 ***************************************************************************/

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
  icaltimezone *default_zone;
  GConfClient *gconf;
  ECalComponent *settings;
  /** @} */

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
