#ifndef __EEE_ACCOUNT_H__
#define __EEE_ACCOUNT_H__

#include <glib-object.h>
#include <gtk/gtk.h>
#include <libedataserver/e-source-list.h>
#include "interface/ESClient.xrc.h"
#include "interface/ESAdmin.xrc.h"

#define EEE_PASSWORD_COMPONENT "3E Account"

#define EEE_TYPE_ACCOUNT            (eee_account_get_type())
#define EEE_ACCOUNT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), EEE_TYPE_ACCOUNT, EeeAccount))
#define EEE_ACCOUNT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  EEE_TYPE_ACCOUNT, EeeAccountClass))
#define IS_EEE_ACCOUNT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), EEE_TYPE_ACCOUNT))
#define IS_EEE_ACCOUNT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  EEE_TYPE_ACCOUNT))
#define EEE_ACCOUNT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),  EEE_TYPE_ACCOUNT, EeeAccountClass))

typedef struct _EeeAccount EeeAccount;
typedef struct _EeeAccountClass EeeAccountClass;
typedef struct _EeeAccountPriv EeeAccountPriv;

struct _EeeAccount
{
  GObject parent;

  char* name;           /**< Account name (usually e-mail address). Used to login to the 3E server. */
  char* server;         /**< 3E server hostname:port. */
  int disabled;         /**< Account is disabled. */

  EeeAccountPriv* priv;
};

struct _EeeAccountClass
{
  GObjectClass parent_class;
};

G_BEGIN_DECLS

GType eee_account_get_type() G_GNUC_CONST;


EeeAccount*       eee_account_new                 (const char* name);
EeeAccount*       eee_account_new_copy            (EeeAccount* ref);
void              eee_account_copy                (EeeAccount* self, 
                                                   EeeAccount* ref);
void              eee_account_disable             (EeeAccount* self);

/* communication functions */

gboolean          eee_account_auth                (EeeAccount* self);
xr_client_conn*   eee_account_connect             (EeeAccount* self);
void              eee_account_disconnect          (EeeAccount* self);
gboolean          eee_account_find_server         (EeeAccount* self);
gboolean          eee_account_load_calendars      (EeeAccount* self, GSList** cals);
GSList*           eee_account_peek_calendars      (EeeAccount* self);
gboolean          eee_account_load_users          (EeeAccount* self,
                                                   char* prefix, 
                                                   GSList* exclude_users, 
                                                   GtkListStore* model);

gboolean          eee_account_calendar_acl_set_private  (EeeAccount* self, 
                                                         const char* calname);
gboolean          eee_account_calendar_acl_set_public   (EeeAccount* self,
                                                         const char* calname);
gboolean          eee_account_calendar_acl_set_shared   (EeeAccount* self, 
                                                         const char* calname, 
                                                         GSList* new_perms);
gboolean          eee_account_set_calendar_attribute    (EeeAccount* self, 
                                                         const char* owner, 
                                                         const char* calname, 
                                                         const char* name, 
                                                         const char* value, 
                                                         gboolean is_public);
gboolean          eee_account_update_calendar_settings  (EeeAccount* self, 
                                                         const char* owner, 
                                                         const char* calname, 
                                                         const char* title, 
                                                         guint32 color);
gboolean          eee_account_create_new_calendar       (EeeAccount* self, 
                                                         char** calname);
gboolean          eee_account_unsubscribe_calendar      (EeeAccount* self, 
                                                         const char* owner, 
                                                         const char* calname);
gboolean          eee_account_subscribe_calendar        (EeeAccount* self, 
                                                         const char* owner, 
                                                         const char* calname);
gboolean          eee_account_delete_calendar           (EeeAccount* self, 
                                                         const char* calname);

gboolean          eee_account_get_shared_calendars_by_username_prefix
                                                        (EeeAccount* self, 
                                                         const char* prefix, 
                                                         GSList** cals);
gboolean          eee_account_get_shared_calendars      (EeeAccount* self, 
                                                         const char* query, 
                                                         GSList** cals);
void              eee_account_free_calendars_list       (GSList* l);

G_END_DECLS

#endif /* __EEE_ACCOUNT_H__ */
