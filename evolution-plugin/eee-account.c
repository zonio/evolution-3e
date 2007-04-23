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
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, EEE_TYPE_ACCOUNT, EeeAccountPriv);
}

static void eee_account_dispose(GObject *object)
{
  EeeAccount *self = EEE_ACCOUNT(object);
  G_OBJECT_CLASS(eee_account_parent_class)->dispose(object);
}

static void eee_account_finalize(GObject *object)
{
  EeeAccount *self = EEE_ACCOUNT(object);
  G_OBJECT_CLASS(eee_account_parent_class)->finalize(object);
}

static void eee_account_class_init(EeeAccountClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  gobject_class->dispose = eee_account_dispose;
  gobject_class->finalize = eee_account_finalize;
  g_type_class_add_private(klass, sizeof(EeeAccountPriv));
}
