#ifndef E_CAL_BACKEND_3E_PRIV
#define E_CAL_BACKEND_3E_PRIV

#include <config.h>

#include <gconf/gconf-client.h>
#include <libedata-cal/e-cal-backend-cache.h>

#include "interface/ESClient.xrc.h"

typedef enum {
	SYNC_SLEEP,
	SYNC_WORK,
	SYNC_DIE
} SyncMode;

struct _ECalBackend3ePrivate
{
  /* Remote connection info */
  char                                   *server_uri;
  xr_client_conn                         *conn;
  gboolean                                is_open;
  gboolean                                is_loaded;
  char                                   *username;
  char                                   *password;
  char                                   *calname;
  char                                   *owner;
  char                                   *calspec;
  GConfClient                            *gconf;

  /* Local/remote mode */
  CalMode                                 mode;

  /* The file cache */
  ECalBackendCache                       *cache;

  ECalComponent                          *settings;

  /* synch thread variables */
  GCond                                  *sync_cond;
  GMutex                                 *sync_mutex;
  GThread                                *sync_thread;
  SyncMode                                sync_mode;
  char                                   *sync_stamp;
  GList                                  *sync_clients_changes;

  /*
   * The calendar's default timezone, used for resolving DATE and
   * floating DATE-TIME values. 
   */
  icaltimezone                           *default_zone;
};

#endif
