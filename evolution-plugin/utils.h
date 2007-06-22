#ifndef __3E_UTILS_H__
#define __3E_UTILS_H__

#include <libedataserver/e-source-list.h>
#include "eee-account.h"
#include "eee-accounts-manager.h"

G_BEGIN_DECLS

char*                  qp_escape_string                                  (const char* s);

gboolean               e_source_group_is_3e                              (ESourceGroup* group);
gboolean               e_source_is_3e                                    (ESource* source);
gboolean               e_source_is_3e_owned_calendar                     (ESource* source);
void                   e_source_set_3e_properties                        (ESource* source, 
                                                                          const char* calname, 
                                                                          const char* owner, 
                                                                          EeeAccount* account, 
                                                                          const char* perm,
                                                                          const char* title, 
                                                                          guint32 color);
ESource*               e_source_new_3e                                   (const char* calname, 
                                                                          const char* owner, 
                                                                          EeeAccount* account, 
                                                                          const char* perm,
                                                                          const char* title, 
                                                                          guint32 color);
ESource*               e_source_new_3e_with_attrs                        (const char* calname, 
                                                                          const char* owner, 
                                                                          EeeAccount* account, 
                                                                          const char* perm,
                                                                          GSList* attrs);
void                   e_source_set_3e_properties_with_attrs             (ESource* source, 
                                                                          const char* calname, 
                                                                          const char* owner, 
                                                                          EeeAccount* account, 
                                                                          const char* perm,
                                                                          GSList* attrs);
ESource*               e_source_group_peek_source_by_calname             (ESourceGroup *group,
                                                                          const char *name);

G_END_DECLS

#endif
