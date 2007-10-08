/**************************************************************************************************
 *  3E plugin for Evolution Data Server                                                           * 
 *                                                                                                *
 *  Copyright (C) 2007 by Zonio                                                                   *
 *  www.zonio.net                                                                                 *
 *  stanislav.slusny@zonio.net                                                                    *
 *                                                                                                *
 **************************************************************************************************/
#ifndef E_CAL_BACKEND_3E_SYNC
#define E_CAL_BACKEND_3E_SYNC

#include <glib.h>
#include "e-cal-backend-3e.h"

/* server connection */
gboolean e_cal_backend_3e_setup_connection(ECalBackend3e* cb, const char* username, const char* password, gboolean test_conn, GError** err);
gboolean e_cal_backend_3e_open_connection(ECalBackend3e* cb, GError** err);
void e_cal_backend_3e_close_connection(ECalBackend3e* cb);
gboolean e_cal_backend_3e_connection_is_open(ECalBackend3e* cb);
void e_cal_backend_3e_free_connection(ECalBackend3e* cb);

/* calendar info */
gboolean e_cal_backend_3e_calendar_info_load(ECalBackend3e* cb);
gboolean e_cal_backend_3e_calendar_is_owned(ECalBackend3e* cb);
gboolean e_cal_backend_3e_calendar_has_perm(ECalBackend3e* cb, const char* perm);
void e_cal_backend_3e_calendar_set_perm(ECalBackend3e* cb, const char* perm);
gboolean e_cal_backend_3e_calendar_load_perm(ECalBackend3e* cb, GError** err);

ECalComponent* e_cal_sync_find_this_in_cache(ECalBackend3e* cb, ECalComponent* needle);

gboolean e_cal_sync_rpc_deleteObject(ECalBackend3e* cb, ECalComponent* comp, GError** err);

gboolean e_cal_sync_rpc_updateObject(ECalBackend3e* cb, ECalComponent* ccomp, GError** err);

gboolean e_cal_sync_rpc_addObject(ECalBackend3e* cb, ECalComponent* ccomp, GError** err);

gboolean e_cal_sync_server_to_client_sync(ECalBackend* backend, const char* sync_start, const char* sync_stop);

void server_sync_signal(ECalBackend3e* cb);

void e_cal_sync_load_stamp(ECalBackend3e* cb, gchar** sync_stop);

void e_cal_sync_save_stamp(ECalBackend3e* cb, const char* sync_stop);

void e_cal_sync_synchronize(ECalBackend* backend);

ECalComponent* e_cal_sync_find_settings(ECalBackend3e* cb);

gpointer e_cal_sync_main_thread(gpointer data);

gboolean e_cal_sync_total_synchronization(ECalBackend3e* cb, GError** err);

gboolean e_cal_sync_incremental_synchronization(ECalBackend3e* cb, GError** err);

void e_cal_sync_client_changes_insert(ECalBackend3e* cb, ECalComponent* comp);

void e_cal_sync_client_changes_remove(ECalBackend3e* cb, ECalComponent *comp);

void e_cal_sync_rebuild_clients_changes_list(ECalBackend3e* cb);

void e_cal_sync_error_message(ECalBackend* backend, ECalComponent* comp, GError* err);

#endif
