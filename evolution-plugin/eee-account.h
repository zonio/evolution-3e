#ifndef __EEE_ACCOUNT_H__
#define __EEE_ACCOUNT_H__

#include <glib-object.h>
#include "interface/ESClient.xrc.h"
#include "interface/ESAdmin.xrc.h"

#define EEE_PASSWORD_COMPONENT "3E Account"

/** 3E user's account.
 *
 * This structure is also used to represent accounts of the owners of subscribed
 * shared calendars. So strictly speaking, EeeAccount may not be related to
 * EAccount. UID is then set to NULL.
 *
 * Calendars grouped under this account may be accessd through different account.
 */

#define EEE_TYPE_ACCOUNT            (eee_account_get_type())
#define EEE_ACCOUNT(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), EEE_TYPE_ACCOUNT, EeeAccount))
#define EEE_ACCOUNT_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),  EEE_TYPE_ACCOUNT, EeeAccountClass))
#define IS_EEE_ACCOUNT(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), EEE_TYPE_ACCOUNT))
#define IS_EEE_ACCOUNT_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),  EEE_TYPE_ACCOUNT))
#define EEE_ACCOUNT_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),  EEE_TYPE_ACCOUNT, EeeAccountClass))

typedef struct _EeeAccount EeeAccount;
typedef struct _EeeAccountClass EeeAccountClass;
typedef struct _EeeAccountPriv EeeAccountPriv;

#include "eee-calendar.h"

struct _EeeAccount
{
  GObject parent;
  EeeAccountPriv* priv;

  int accessible;             /**< Account is accessible (we can login). */
  char* email;                /**< Username of the account owner. Used to login to the 3E server.
                                   This string is used to match EeeAccount against EAccount. */
  char* password;             /**< This is password used to authenticate to an account. */
  char* server;               /**< 3E server hostname:port. */
  int synced;
};

struct _EeeAccountClass
{
  GObjectClass parent_class;
};

G_BEGIN_DECLS

EeeAccount* eee_account_new();
void eee_account_add_calendar(EeeAccount* account, EeeCalendar* cal);
GSList* eee_account_peek_calendars(EeeAccount* account);
EeeCalendar* eee_account_peek_calendar_by_name(EeeAccount* account, const char* name);
xr_client_conn* eee_account_connect(EeeAccount* account);

GType eee_account_get_type() G_GNUC_CONST;

G_END_DECLS

#endif /* __EEE_ACCOUNT_H__ */
