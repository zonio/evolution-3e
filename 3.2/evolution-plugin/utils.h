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

#ifndef __3E_UTILS_H__
#define __3E_UTILS_H__

#define COLOR_COMPONENT_SIZE 1  //bytes

#include <libedataserver/e-source-list.h>
#include "eee-account.h"
#include "eee-accounts-manager.h"

G_BEGIN_DECLS

char *qp_escape_string(const char *s);

gboolean               e_source_group_is_3e(ESourceGroup *group);
gboolean               e_source_is_3e(ESource *source);
gboolean               e_source_is_3e_owned_calendar(ESource *source);
void                   e_source_set_3e_properties(ESource *source,
                                                  const char *calname,
                                                  const char *owner,
                                                  EeeAccount *account,
                                                  const char *perm,
                                                  const char *title,
                                                  const char *color);
ESource *e_source_new_3e(const char *calname,
                         const char *owner,
                         EeeAccount *account,
                         const char *perm,
                         const char *title,
                         const char *color);
ESource *e_source_new_3e_with_attrs(const char *calname,
                                    const char *owner,
                                    EeeAccount *account,
                                    const char *perm,
                                    GSList *attrs);
void                   e_source_set_3e_properties_with_attrs(ESource *source,
                                                             const char *calname,
                                                             const char *owner,
                                                             EeeAccount *account,
                                                             const char *perm,
                                                             GSList *attrs);
ESource *e_source_group_peek_source_by_calname(ESourceGroup *group,
                                               const char *name);
ESAttribute *eee_find_attribute(GSList *attrs,
                                const char *name);
const char *eee_find_attribute_value(GSList *attrs,
                                     const char *name);

G_END_DECLS

#endif
