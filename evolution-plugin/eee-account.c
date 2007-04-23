#include "eee-account.h"

struct _EeeAccountPriv
{
  GSList* calendars;          /**< List of EeeCalendar objects owned by this account. */
};

EeeAccount* eee_account_new()
{
  EeeAccount *obj = g_object_new(EEE_TYPE_ACCOUNT, NULL);
  return obj;
}

void eee_account_add_calendar(EeeAccount* account, EeeCalendar* cal)
{
  g_return_if_fail(IS_EEE_CALENDAR(cal));
  g_return_if_fail(IS_EEE_ACCOUNT(account));
  account->priv->calendars = g_slist_append(account->priv->calendars, g_object_ref(cal));
}

GSList* eee_account_peek_calendars(EeeAccount* account)
{
  g_return_val_if_fail(IS_EEE_ACCOUNT(account), NULL);
  return account->priv->calendars;
}

/** Find EeeCalendar object by name.
 *
 * @param acc EeeAccount object.
 * @param name Name.
 *
 * @return Matching EeeCalendar object or NULL.
 */
EeeCalendar* eee_account_peek_calendar_by_name(EeeAccount* account, const char* name)
{
  GSList* iter;

  g_return_val_if_fail(IS_EEE_ACCOUNT(account), NULL);
  g_return_val_if_fail(name != NULL, NULL);

  for (iter = eee_account_peek_calendars(account); iter; iter = iter->next)
  {
    EeeCalendar* cal = iter->data;
    if (cal->name && !strcmp(cal->name, name))
      return cal;
  }

  return NULL;
}

/* GObject foo */

G_DEFINE_TYPE(EeeAccount, eee_account, G_TYPE_OBJECT);

static void eee_account_init(EeeAccount *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, EEE_TYPE_ACCOUNT, EeeAccountPriv);
}

static void eee_account_dispose(GObject *object)
{
  EeeAccount *self = EEE_ACCOUNT(object);
  g_slist_foreach(self->priv->calendars, (GFunc)g_object_unref, NULL);
  g_slist_free(self->priv->calendars);
  self->priv->calendars = NULL;
  G_OBJECT_CLASS(eee_account_parent_class)->dispose(object);
}

static void eee_account_finalize(GObject *object)
{
  EeeAccount *self = EEE_ACCOUNT(object);
  g_free(self->email);
  g_free(self->password);
  g_free(self->server);
  G_OBJECT_CLASS(eee_account_parent_class)->finalize(object);
}

static void eee_account_class_init(EeeAccountClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  gobject_class->dispose = eee_account_dispose;
  gobject_class->finalize = eee_account_finalize;
  g_type_class_add_private(klass, sizeof(EeeAccountPriv));
}
