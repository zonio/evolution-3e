#include <string.h>
#include <stdlib.h>
#include <e-util/e-error.h>
#include <libedataserverui/e-passwords.h>

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

  account->priv->calendars = g_slist_append(account->priv->calendars, cal);
}

GSList* eee_account_peek_calendars(EeeAccount* account)
{
  g_return_val_if_fail(IS_EEE_ACCOUNT(account), NULL);

  return account->priv->calendars;
}

/** Find EeeCalendar object by name.
 *
 * @param account EeeAccount object.
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

/* 3e server access methods */

static gboolean authenticate_to_account(EeeAccount* account, xr_client_conn* conn)
{
  GError* err = NULL;
  guint32 flags = E_PASSWORDS_REMEMBER_FOREVER|E_PASSWORDS_SECRET;
  gboolean remember = TRUE;
  char *fail_msg = ""; 
  char *password;
  int retry_limit = 3;
  gboolean rs;

  while (retry_limit--)
  {
    // get password
    password = e_passwords_get_password(EEE_PASSWORD_COMPONENT, account->email);
    if (!password)
    {
      // no?, ok ask for it
      char* prompt = g_strdup_printf("%sEnter password for your 3E calendar account (%s).", fail_msg, account->email);
      // key must have uri format or unpatched evolution segfaults in
      // ep_get_password_keyring()
      char* key = g_strdup_printf("eee://%s", account->email);
      g_debug("e_passwords_ask_password(key=%s)", key);
      password = e_passwords_ask_password(prompt, EEE_PASSWORD_COMPONENT, key, prompt, flags, &remember, NULL);
      g_free(key);
      g_free(prompt);
      if (!password) 
        goto err;
    }

    // try to authenticate
    rs = ESClient_auth(conn, account->email, password, &err);
    if (!err && rs == TRUE)
    {
      g_free(account->password);
      account->password = password;
      return TRUE;
    }
    g_free(password);

    // process error
    if (err)
    {
      g_debug("** EEE ** Authentization failed for user '%s'. (%d:%s)", account->email, err->code, err->message);
      if (err->code == 1)
        fail_msg = "User not found. ";
      else if (err->code == 6)
        fail_msg = "Invalid password. ";
      g_clear_error(&err);
    }
    else
    {
      g_debug("** EEE ** Authentization failed for user '%s'.", account->email);
      fail_msg = "";
    }

    // forget password and retry
    e_passwords_forget_password(EEE_PASSWORD_COMPONENT, account->email);
    flags |= E_PASSWORDS_REPROMPT;
  }

  e_error_run(NULL, "mail:eee-auth-error", account->email, NULL);
 err:
  g_free(account->password);
  account->password = NULL;
  return FALSE;
}

xr_client_conn* eee_account_connect(EeeAccount* account)
{
  xr_client_conn* conn;
  GError* err = NULL;
  char* server_uri;

  g_debug("** EEE ** Connecting to 3E server: server=%s user=%s", account->server, account->email);
  conn = xr_client_new(&err);
  if (err)
  {
    g_debug("** EEE ** Can't create client interface. (%d:%s)", err->code, err->message);
    goto err0;
  }
  if (getenv("EEE_EVO_DISABLE_SSL"))
    server_uri = g_strdup_printf("http://%s/ESClient", account->server);
  else
    server_uri = g_strdup_printf("https://%s/ESClient", account->server);
  xr_client_open(conn, server_uri, &err);
  g_free(server_uri);
  if (err)
  {
    g_debug("** EEE ** Can't open connection to the server. (%d:%s)", err->code, err->message);
    goto err1;
  }
  if (!authenticate_to_account(account, conn))
    goto err1;

  return conn;

 err1:
  xr_client_free(conn);
 err0:
  g_clear_error(&err);
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
  g_debug("dispose account %p", object);
  g_slist_foreach(self->priv->calendars, (GFunc)g_object_unref, NULL);
  g_slist_free(self->priv->calendars);
  self->priv->calendars = NULL;
  G_OBJECT_CLASS(eee_account_parent_class)->dispose(object);
}

static void eee_account_finalize(GObject *object)
{
  EeeAccount *self = EEE_ACCOUNT(object);
  g_debug("dispose account %p", object);
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
