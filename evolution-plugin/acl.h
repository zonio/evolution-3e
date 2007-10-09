/***************************************************************************
 *  3E plugin for Evolution                                                *
 *                                                                         *
 *  Copyright (C) 2007 by Zonio                                            *
 *  www.zonio.net                                                          *
 *  Ondrej Jirman <ondrej.jirman@zonio.net>                                *
 *                                                                         *
 ***************************************************************************/

#ifndef __ACL_H
#define __ACL_H

#include "eee-accounts-manager.h"

void acl_gui_create(EeeAccountsManager* mgr, EeeAccount* account, ESource* source);
void acl_gui_destroy();

#endif
