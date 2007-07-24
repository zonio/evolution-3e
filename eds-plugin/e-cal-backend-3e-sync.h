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

ECalComponent* e_cal_sync_find_this_in_cache(ECalBackend3e* cb, ECalComponent* needle);

gboolean e_cal_sync_server_open(ECalBackend3e* cb, GError** err);

gboolean e_cal_sync_server_object_delete(ECalBackend3e* cb, ECalComponent* comp,
                                         gboolean conn_opened, GError** err);

gboolean e_cal_sync_server_object_update(ECalBackend3e* cb, ECalComponent* ccomp,
                                         gboolean conn_opened, GError** err);

gboolean e_cal_sync_server_object_add(ECalBackend3e* cb, ECalComponent* ccomp, gboolean conn_opened,
                                      GError** err);

gboolean e_cal_sync_server_to_client_sync(ECalBackend* backend, const char* sync_start,
                                          const char* sync_stop);

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

void rebuild_clients_changes_list(ECalBackend3e* cb);

void e_cal_sync_error_message(ECalBackend* backend, ECalComponent* comp, GError* err);

#endif
