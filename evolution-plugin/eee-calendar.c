#include "eee-accounts-manager.h"
#include "eee-calendar.h"

struct _EeeCalendarPriv
{
  int stuffings;
};

EeeCalendar* eee_calendar_new()
{
  EeeCalendar *obj = g_object_new(EEE_TYPE_CALENDAR, NULL);
  return obj;
}

void eee_calendar_set_name(EeeCalendar* cal, const char* name)
{
  g_return_if_fail(IS_EEE_CALENDAR(cal));
  g_free(cal->name);
  cal->name = g_strdup(name);
}

void eee_calendar_set_perm(EeeCalendar* cal, const char* perm)
{
  g_return_if_fail(IS_EEE_CALENDAR(cal));
  g_free(cal->perm);
  cal->perm = g_strdup(perm);
}

void eee_calendar_set_relative_uri(EeeCalendar* cal, const char* uri)
{
  g_return_if_fail(IS_EEE_CALENDAR(cal));
  g_free(cal->relative_uri);
  cal->relative_uri = g_strdup(uri);
}

void eee_calendar_set_access_account(EeeCalendar* cal, EeeAccount* account)
{
  g_return_if_fail(IS_EEE_CALENDAR(cal));
  g_return_if_fail(IS_EEE_ACCOUNT(account));

  if (cal->access_account)
    g_object_unref(cal->access_account);
  cal->access_account = g_object_ref(account);
}

void eee_calendar_set_owner_account(EeeCalendar* cal, EeeAccount* account)
{
  g_return_if_fail(IS_EEE_CALENDAR(cal));
  g_return_if_fail(IS_EEE_ACCOUNT(account));

  if (cal->owner_account)
    g_object_unref(cal->owner_account);
  cal->owner_account = g_object_ref(account);
}

gboolean eee_calendar_store_settings(EeeCalendar* cal)
{
  xr_client_conn* conn;
  GError* err = NULL;

  g_return_val_if_fail(IS_EEE_CALENDAR(cal), FALSE);

  conn = eee_account_connect(cal->access_account);
  if (conn == NULL)
    return FALSE;

  char* settings_str = eee_settings_encode(cal->settings);
  char* calspec = g_strdup_printf("%s:%s", cal->owner_account->email, cal->name);
  ESClient_updateCalendarSettings(conn, calspec, settings_str, &err);
  g_free(settings_str);
  g_free(calspec);
  xr_client_free(conn);

  if (err)
  {
    g_debug("** EEE ** Failed to store settings for calendar '%s'. (%d:%s)", eee_settings_get_title(cal->settings), err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }

  return TRUE;
}

static gboolean remove_acl(EeeCalendar* cal, xr_client_conn* conn)
{
  GError* err = NULL;
  GSList* iter;

  GSList* perms = ESClient_getPermissions(conn, cal->name, &err);
  if (err)
  {
    g_debug("** EEE ** Failed to store settings for calendar '%s'. (%d:%s)", eee_settings_get_title(cal->settings), err->code, err->message);
    goto err0;
  }

  for (iter = perms; iter; iter = iter->next)
  {
    ESPermission* perm = iter->data;
    ESClient_setPermission(conn, cal->name, perm->user, "none", &err);
    if (err)
    {
      g_debug("** EEE ** Failed to update permission for calendar '%s'. (%d:%s)", eee_settings_get_title(cal->settings), err->code, err->message);
      goto err1;
    }
  }

  return TRUE;
 err1:
  g_slist_foreach(perms, (GFunc)ESPermission_free, NULL);
  g_slist_free(perms);
 err0:
  g_clear_error(&err);
  return FALSE;
}

static gboolean add_wildcard(EeeCalendar* cal, xr_client_conn* conn)
{
  GError* err = NULL;
  ESClient_setPermission(conn, cal->name, "*", "read", &err);
  if (err)
  {
    g_debug("** EEE ** Failed to update permission for calendar '%s'. (%d:%s)", eee_settings_get_title(cal->settings), err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }
  return TRUE;
}

static gboolean add_acl(EeeCalendar* cal, xr_client_conn* conn, GSList* new_perms)
{
  GError* err = NULL;
  GSList* iter;

  for (iter = new_perms; iter; iter = iter->next)
  {
    ESPermission* perm = iter->data;
    ESClient_setPermission(conn, cal->name, perm->user, perm->perm, &err);
    if (err)
    {
      g_debug("** EEE ** Failed to update permission for calendar '%s'. (%d:%s)", eee_settings_get_title(cal->settings), err->code, err->message);
      g_clear_error(&err);
      return FALSE;
    }
  }
  return TRUE;
}

gboolean eee_calendar_set_private(EeeCalendar* cal)
{
  xr_client_conn* conn;
  GError* err = NULL;
  gboolean retval;

  g_return_val_if_fail(IS_EEE_CALENDAR(cal), FALSE);

  conn = eee_account_connect(cal->access_account);
  if (conn == NULL)
    return FALSE;

  retval = remove_acl(cal, conn);

  xr_client_free(conn);
  return retval;
}

gboolean eee_calendar_set_public(EeeCalendar* cal)
{
  xr_client_conn* conn;
  GError* err = NULL;
  gboolean retval;

  g_return_val_if_fail(IS_EEE_CALENDAR(cal), FALSE);

  conn = eee_account_connect(cal->access_account);
  if (conn == NULL)
    return FALSE;

  retval = remove_acl(cal, conn) && add_wildcard(cal, conn);

  xr_client_free(conn);
  return retval;
}

gboolean eee_calendar_set_shared(EeeCalendar* cal, GSList* new_perms)
{
  xr_client_conn* conn;
  GError* err = NULL;
  gboolean retval;

  g_return_val_if_fail(IS_EEE_CALENDAR(cal), FALSE);

  conn = eee_account_connect(cal->access_account);
  if (conn == NULL)
    return FALSE;

  //XXX: this is broken because we need to change permissions more gently
  // remove acl will automatically unsubscribe all users subscribed to our
  // calendar
  retval = remove_acl(cal, conn) && add_acl(cal, conn, new_perms);

  xr_client_free(conn);
  return retval;
}

/* GObject foo */

G_DEFINE_TYPE(EeeCalendar, eee_calendar, G_TYPE_OBJECT);

static void eee_calendar_init(EeeCalendar *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE(self, EEE_TYPE_CALENDAR, EeeCalendarPriv);
  self->settings = eee_settings_new(NULL);
}

static void eee_calendar_dispose(GObject *object)
{
  EeeCalendar *self = EEE_CALENDAR(object);
  g_debug("dispose calendar %p", object);
  if (self->settings)
    g_object_unref(self->settings);
  if (self->owner_account)
    g_object_unref(self->owner_account);
  if (self->access_account)
    g_object_unref(self->access_account);
  G_OBJECT_CLASS(eee_calendar_parent_class)->dispose(object);
}

static void eee_calendar_finalize(GObject *object)
{
  EeeCalendar *self = EEE_CALENDAR(object);
  g_debug("finalize calendar %p", object);
  g_free(self->name);
  g_free(self->perm);
  g_free(self->relative_uri);
  G_OBJECT_CLASS(eee_calendar_parent_class)->finalize(object);
}

static void eee_calendar_class_init(EeeCalendarClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  gobject_class->dispose = eee_calendar_dispose;
  gobject_class->finalize = eee_calendar_finalize;
  g_type_class_add_private(klass, sizeof(EeeCalendarPriv));
}
