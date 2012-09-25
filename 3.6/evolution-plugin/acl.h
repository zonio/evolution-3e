/*
 * Zonio 3e calendar plugin
 *
 * Copyright (C) 2008-2012 Zonio s.r.o <developers@zonio.net>
 *
 * This file is part of evolution-3e.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ACL_H
#define __ACL_H

#include "eee-accounts-manager.h"

struct acl_context
{
    ESource *source;
    EeeAccount *account;

    GtkBuilder *builder;
    GtkWidget *win;
    GtkWidget *rb_private;
    GtkWidget *rb_public;
    GtkWidget *rb_shared;
    GtkWidget *users_frame;
    GtkWidget *users_menu;
    GtkWidget *user_entry;
    GtkListStore *acl_model;
    GtkListStore *users_model;
    GtkTreeView *tview;

    // initial state
    int initial_mode;
    GArray *initial_perms;
};

gboolean store_acl (struct acl_context *);
struct acl_context * acl_gui_create(EeeAccountsManager *mgr, EeeAccount *account, ESource *source);
void acl_gui_destroy();

#endif
