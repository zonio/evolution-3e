#include "eee-account.h"

struct _EeeAccountPriv
{
};

EeeAccount* eee_account_new()
{
  EeeAccount *obj = g_object_new(EEE_TYPE_ACCOUNT, NULL);
  return obj;
}

G_DEFINE_TYPE(EeeAccount, eee_account, G_TYPE_OBJECT);

static void eee_account_init(EeeAccount *self)
{
  self->priv = g_new0(EeeAccountPriv, 1);
}

static void eee_account_dispose(GObject *object)
{
  EeeAccount *self = EEE_ACCOUNT(object);

  G_OBJECT_CLASS(eee_account_parent_class)->dispose(object);
}

static void eee_account_finalize(GObject *object)
{
  EeeAccount *self = EEE_ACCOUNT(object);

  g_free(self->priv);
  g_signal_handlers_destroy(object);

  G_OBJECT_CLASS(eee_account_parent_class)->finalize(object);
}

static void eee_account_class_init(EeeAccountClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

  gobject_class->dispose = eee_account_dispose;
  gobject_class->finalize = eee_account_finalize;
}
