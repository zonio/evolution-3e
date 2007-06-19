#ifndef E_CAL_BACKEND_3E_SYNC
#define E_CAL_BACKEND_3E_SYNC

#include "e-cal-backend-3e.h"

ECalComponent* e_cal_sync_find_this_in_cache(ECalBackend3e* cb, ECalComponent* needle);

gboolean e_cal_sync_server_open(ECalBackend3e* cb);

// delete component from server
gboolean e_cal_sync_server_object_delete(ECalBackend3e* cb,
                                         ECalComponent* comp,
                                         gboolean conn_opened);
/* connection already opened */
// update component on server
gboolean e_cal_sync_server_object_update(ECalBackend3e* cb,
                                         ECalComponent* ccomp,
                                         gboolean conn_opened);

// add component on server
gboolean e_cal_sync_server_object_add(ECalBackend3e* cb,
                                      ECalComponent* ccomp,
                                      gboolean conn_opened);
  /* connection already opened */

// create a list with server objects
// if last_synchro_time is NULL, queries all server objects
gboolean e_cal_sync_server_to_client_sync(ECalBackend* backend,
                                          const char* sync_start,
                                          const char* sync_stop);

// synchronizes objects, from client's perspective
// component is only in the client's cache, not on server yet
gboolean e_cal_sync_client_to_server_sync(ECalBackend3e* cb);


// wake up synchronization thread
void server_sync_signal(ECalBackend3e* cb,
                        gboolean stop);


void e_cal_sync_load_stamp(ECalBackend3e* cb,
                           gchar** sync_stop);


void e_cal_sync_save_stamp(ECalBackend3e* cb,
                           const char* sync_stop);


void e_cal_sync_synchronize(ECalBackend* backend);


ECalComponent*
e_cal_sync_find_settings(ECalBackend3e* cb);

// synchro thread main
gpointer e_cal_sync_main_thread(gpointer data);

void
e_cal_sync_total_synchronization(ECalBackend3e* cb);


/* calendar backend functions */

void e_cal_sync_client_changes_insert(ECalBackend3e* cb,
                                      ECalComponent* comp);


void e_cal_sync_client_changes_remove(ECalBackend3e* cb,
                                      ECalComponent *comp);


void rebuild_clients_changes_list(ECalBackend3e* cb);

#endif
