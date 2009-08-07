/* 
 * Author: Ondrej Jirman <ondrej.jirman@zonio.net>
 *
 * Copyright 2007-2008 Zonio, s.r.o.
 * 
 * This file is part of evolution-3e.
 *
 * Libxr is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 2 of the License, or (at your option) any
 * later version.
 *
 * Libxr is distributed in the hope that it will be useful, but WITHOUT ANY
 * WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
 * A PARTICULAR PURPOSE.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with evolution-3e.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <stdlib.h>
#include <libedataserverui/e-passwords.h>

#include "dns-txt-search.h"
#include "utils.h"
#include "eee-account.h"

#define CALENDAR_SOURCES "/apps/evolution/calendar/sources"
#define EEE_KEY "/apps/evolution/calendar/eee/"

struct _EeeAccountPriv
{
  xr_client_conn* conn;
  gboolean is_authorized;
  GSList *cals;
};

EeeAccount* eee_account_new(const char* name)
{
  EeeAccount *self = g_object_new(EEE_TYPE_ACCOUNT, NULL);
  self->name = g_strdup(name);
  return self;
}

EeeAccount* eee_account_new_copy(EeeAccount* ref)
{
  EeeAccount *self = g_object_new(EEE_TYPE_ACCOUNT, NULL);
  eee_account_copy(self, ref);
  return self;
}

void eee_account_copy(EeeAccount* self, EeeAccount* ref)
{
  g_return_if_fail(IS_EEE_ACCOUNT(self));
  g_return_if_fail(IS_EEE_ACCOUNT(ref));

  g_free(self->name);
  self->name = g_strdup(ref->name);
  g_free(self->server);
  self->server = g_strdup(ref->server);
  self->state = ref->state;
}

void eee_account_set_state(EeeAccount* self, int state)
{
  g_return_if_fail(IS_EEE_ACCOUNT(self));

  self->state = state;
}

static const char* state_to_str(int state)
{
  if (state == EEE_ACCOUNT_STATE_ONLINE)
    return "ONLINE";
  if (state == EEE_ACCOUNT_STATE_NOTAVAIL)
    return "NOTAVAIL";
  if (state == EEE_ACCOUNT_STATE_DISABLED)
    return "DISABLED";
  return "???";
}

void eee_account_dump(EeeAccount* self)
{
  g_return_if_fail(IS_EEE_ACCOUNT(self));

  g_debug("EeeAccount(name=%s, server=%s, state=%s)", self->name, self->server, state_to_str(self->state));
}

gboolean eee_account_find_server(EeeAccount* self)
{
  if (self->server)
    return TRUE;
  self->server = get_eee_server_hostname(self->name);
  if (self->server)
  {
    //g_debug("** EEE ** Found 3E server '%s' for account '%s'.", self->server, self->name);
    return TRUE;
  }
  else
  {
    g_warning("** EEE ** 3E server NOT found for account '%s'.", self->name);
    return FALSE;
  }
}

/* communication functions */

static gboolean remove_acl(xr_client_conn* conn, const char* calname)
{
  GError* err = NULL;
  GSList* iter;

  GSList* perms = ESClient_getUserPermissions(conn, calname, &err);
  if (err)
  {
    g_warning("** EEE ** Failed to store settings for calendar '%s'. (%d:%s)", calname, err->code, err->message);
    goto err0;
  }

  for (iter = perms; iter; iter = iter->next)
  {
    ESPermission* perm = iter->data;
    ESClient_setUserPermission(conn, calname, perm->user, "none", &err);
    if (err)
    {
      g_warning("** EEE ** Failed to update permission for calendar '%s'. (%d:%s)", calname, err->code, err->message);
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

static gboolean add_wildcard(xr_client_conn* conn, const char* calname)
{
  GError* err = NULL;
  ESClient_setUserPermission(conn, calname, "*", "read", &err);
  if (err)
  {
    g_warning("** EEE ** Failed to update permission for calendar '%s'. (%d:%s)", calname, err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }
  return TRUE;
}

static gboolean update_acl(xr_client_conn* conn, const char* calname, GSList* new_perms)
{
  GError* err = NULL;
  GSList* iter;
  GSList* iter2;
  gboolean retval = TRUE;

  GSList* perms = ESClient_getUserPermissions(conn, calname, &err);
  if (err)
  {
    g_warning("** EEE ** Failed to store settings for calendar '%s'. (%d:%s)", calname, err->code, err->message);
    return FALSE;
  }

  new_perms = g_slist_copy(new_perms);
  /* for each existing perm */
  for (iter = perms; iter; iter = iter->next)
  {
    ESPermission* perm = iter->data;
    gboolean not_found = TRUE;

    /* find matching new perm */
    for (iter2 = new_perms; iter2; iter2 = iter2->next)
    {
      ESPermission* new_perm = iter2->data;

      if (!strcmp(perm->user, new_perm->user))
      {
        /* perm is already set, check if it differs */
        if (strcmp(perm->perm, new_perm->perm))
        {
          ESClient_setUserPermission(conn, calname, new_perm->user, new_perm->perm, &err);
          if (err)
          {
            g_warning("** EEE ** Failed to update permission for calendar '%s'. (%d:%s)", calname, err->code, err->message);
            g_clear_error(&err);
            retval = FALSE;
          }
        }
        new_perms = g_slist_remove_link(new_perms, iter2);
        not_found = FALSE;
        break;
      }
    }

    /* if existing perm was not found in new perms, unset it */
    if (not_found)
    {
      ESClient_setUserPermission(conn, calname, perm->user, "none", &err);
      if (err)
      {
        g_warning("** EEE ** Failed to update permission for calendar '%s'. (%d:%s)", calname, err->code, err->message);
        g_clear_error(&err);
        retval = FALSE;
      }
    }
  }

  /* set remaining new perms */
  for (iter = new_perms; iter; iter = iter->next)
  {
    ESPermission* perm = iter->data;

    ESClient_setUserPermission(conn, calname, perm->user, perm->perm, &err);
    if (err)
    {
      g_warning("** EEE ** Failed to update permission for calendar '%s'. (%d:%s)", calname, err->code, err->message);
      g_clear_error(&err);
      retval = FALSE;
    }
  }

  g_slist_free(new_perms);

  return retval;
}

gboolean eee_account_calendar_acl_set_private(EeeAccount* self, const char* calname)
{
  if (calname == NULL || !eee_account_auth(self))
    return FALSE;
  return remove_acl(self->priv->conn, calname);
}

gboolean eee_account_calendar_acl_set_public(EeeAccount* self, const char* calname)
{
  if (calname == NULL || !eee_account_auth(self))
    return FALSE;
  return remove_acl(self->priv->conn, calname) && add_wildcard(self->priv->conn, calname);
}

gboolean eee_account_calendar_acl_set_shared(EeeAccount* self, const char* calname, GSList* new_perms)
{
  if (calname == NULL || !eee_account_auth(self))
    return FALSE;
  return update_acl(self->priv->conn, calname, new_perms);
}

gboolean eee_account_load_calendars(EeeAccount* self, GSList** cals)
{
  GError* err = NULL;

  if (!eee_account_auth(self))
    return FALSE;

  eee_account_free_calendars_list(self->priv->cals);

  self->priv->cals = ESClient_getCalendars(self->priv->conn, "", &err);
  if (err)
  {
    g_warning("** EEE ** Failed to get calendars for account '%s'. (%d:%s)", self->name, err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }

  if (cals)
    *cals = self->priv->cals;

  return TRUE;
}

GSList* eee_account_peek_calendars(EeeAccount* self)
{
  return self->priv->cals;
}

/* creates a query to match the users whose realname matches parameter realname */
static char* create_users_query (EeeAccount* self, const char* realname)
{
  if (realname == NULL || !eee_account_auth(self))
    return NULL;
  char* query = g_strdup_printf ("match_user_attribute_like('realname', %s)", realname);
  GError* err = NULL;
  GSList* users = ESClient_getUsers(self->priv->conn, query, &err);
  g_free(query);
  if (err)
  {
    g_warning("** EEE ** Failed to get user list for account '%s'. (%d:%s)", self->name, err->code, err->message);
    g_clear_error(&err);
    return NULL;
  }
  GString* result = g_string_new("");
  GSList* iter;
  for (iter = users; iter; iter = iter->next)
  {
    ESUser* user = iter->data;
	if (iter != users)
	  g_string_append (result, " OR ");
	g_string_append_printf (result, "match_username('%s')", user->username);
  }
  g_slist_foreach(users, (GFunc)ESUser_free, NULL);
  g_slist_free(users);
  if (strcmp("",result->str) == 0)
    return NULL;
  return g_string_free(result, FALSE);
}

gboolean eee_account_search_shared_calendars(EeeAccount* self, const char* query_string, GSList** cals)
{
  char* query = NULL;
  gboolean retval;

  if (query_string != NULL && query_string[0] != '\0')
  {
    char* query_string_like = g_strdup_printf("*%s*", query_string);
	char* escaped_query = qp_escape_string(query_string_like);
	char* users_query = create_users_query (self, escaped_query);
	users_query = users_query ? g_strdup_printf("%s OR ",users_query) : "";
    query = g_strdup_printf(
      "%2$smatch_username_like(%1$s)"
      " OR match_calendar_name_like(%1$s)"
      " OR match_calendar_attribute_like('title', %1$s)",
      escaped_query, users_query);
    g_free(escaped_query);
    g_free(query_string_like);
  }

  retval = eee_account_get_shared_calendars(self, query ? query : "", cals);

  g_free(query);
  return retval;
}

gboolean eee_account_get_shared_calendars(EeeAccount* self, const char* query, GSList** cals)
{
  GError* err = NULL;

  if (query == NULL || cals == NULL || !eee_account_auth(self))
    return FALSE;

  *cals = ESClient_getSharedCalendars(self->priv->conn, query, &err);
  if (err)
  {
    g_warning("** EEE ** Failed to get calendars for account '%s'. (%d:%s)", self->name, err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }

  return TRUE;
}

void eee_account_free_calendars_list(GSList* l)
{
  if (l == NULL)
    return;
  g_slist_foreach(l, (GFunc)ESCalendar_free, NULL);
  g_slist_free(l);
}

gboolean eee_account_get_user_attributes(EeeAccount* self, const char* username, GSList** attrs)
{
  GError* err = NULL;

  if (username == NULL || attrs == NULL || !eee_account_auth(self))
    return FALSE;

  *attrs = ESClient_getUserAttributes(self->priv->conn, username, &err);
  if (err)
  {
    g_warning("** EEE ** Failed to get calendars for account '%s'. (%d:%s)", self->name, err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }

  return TRUE;
}

void eee_account_free_attributes_list(GSList* l)
{
  if (l == NULL)
    return;
  g_slist_foreach(l, (GFunc)ESAttribute_free, NULL);
  g_slist_free(l);
}

gboolean eee_account_set_calendar_attribute(EeeAccount* self, const char* owner, const char* calname, const char* name, const char* value, gboolean is_public)
{
  GError* err = NULL;

  if (owner == NULL || calname == NULL || name == NULL || !eee_account_auth(self))
    return FALSE;

  char* calspec = g_strdup_printf("%s:%s", owner, calname);
  ESClient_setCalendarAttribute(self->priv->conn, calspec, name, value ? value : "", is_public, &err);
  g_free(calspec);

  if (err)
  {
    g_warning("** EEE ** Failed to get calendars for account '%s'. (%d:%s)", self->name, err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }

  return TRUE;
}

gboolean eee_account_update_calendar_settings(EeeAccount* self, const char* owner, const char* calname, const char* title, const char* color)
{
  gboolean rs = TRUE;

  rs &= eee_account_set_calendar_attribute(self, owner, calname, "title", title, TRUE);
  rs &= eee_account_set_calendar_attribute(self, owner, calname, "color", color, FALSE);

  return rs;
}

static char* generate_calname()
{
  GRand* rand = g_rand_new();
  return g_strdup_printf("%08x", g_rand_int(rand));
}

gboolean eee_account_create_new_calendar(EeeAccount* self, char** calname)
{
  GError* err = NULL;

  if (calname == NULL || !eee_account_auth(self))
    return FALSE;

  while (1)
  {
    *calname = generate_calname();
    ESClient_createCalendar(self->priv->conn, *calname, &err);
    if (err == NULL || err->code != ES_XMLRPC_ERROR_CALENDAR_EXISTS)
      break;

    // try again
    g_free(*calname);
    *calname = NULL;
  }

  if (err)
  {
    g_warning("** EEE ** internal error, can't create calendar (%d:%s)", err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }

  return TRUE;
}

gboolean eee_account_unsubscribe_calendar(EeeAccount* self, const char* owner, const char* calname)
{
  GError* err = NULL;

  if (owner == NULL || calname == NULL || !eee_account_auth(self))
    return FALSE;

  char* calspec = g_strdup_printf("%s:%s", owner, calname);
  ESClient_unsubscribeCalendar(self->priv->conn, calspec, &err);
  g_free(calspec);

  if (err)
  {
    g_warning("** EEE ** internal error, can't unsuibscribe calendar (%d:%s)", err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }

  return TRUE;
}

gboolean eee_account_subscribe_calendar(EeeAccount* self, const char* owner, const char* calname)
{
  GError* err = NULL;

  if (owner == NULL || calname == NULL || !eee_account_auth(self))
    return FALSE;

  char* calspec = g_strdup_printf("%s:%s", owner, calname);
  ESClient_subscribeCalendar(self->priv->conn, calspec, &err);
  g_free(calspec);

  if (err)
  {
    g_warning("** EEE ** internal error, can't subscribe calendar (%d:%s)", err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }

  return TRUE;
}

gboolean eee_account_delete_calendar(EeeAccount* self, const char* calname)
{
  GError* err = NULL;

  if (calname == NULL || !eee_account_auth(self))
    return FALSE;

  ESClient_deleteCalendar(self->priv->conn, calname, &err);

  if (err)
  {
    g_warning("** EEE ** internal error, can't delete calendar (%d:%s)", err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }

  return TRUE;
}

/** Load list of users from the self on the server to the GtkListStore
 * excluding self owner and optional list of users.
 *
 * @param self EeeAccount
 * @param prefix User name prefix.
 * @param exclude_users List of emails of users to exclude.
 * @param model GtkListStore with at least two columns: 
 *   - 0 = G_TYPE_STRING (username)
 *   - 1 = G_TYPE_STRING (realname)
 *   - 2 = EEE_TYPE_ACCOUNT (self object passed as @b self parameter)
 */
gboolean eee_account_load_users(EeeAccount* self, char* prefix, GSList* exclude_users, GtkListStore* model)
{
  GError* err = NULL;
  GSList *users, *iter;
  GtkTreeIter titer_user;

  if (!eee_account_auth(self))
    return FALSE;

  if (prefix == NULL || prefix[0] == '\0')
  {
    users = ESClient_getUsers(self->priv->conn, "", &err);
  }
  else
  {
    char* prefix_like = g_strdup_printf("*%s*", prefix);
    char* escaped_prefix = qp_escape_string(prefix_like);
    char* query = g_strdup_printf("match_username_like(%s)", escaped_prefix);
    g_free(escaped_prefix);
	g_free(prefix_like);
    users = ESClient_getUsers(self->priv->conn, query, &err);
    g_free(query);
  }
  if (err)
  {
    g_warning("** EEE ** Failed to get users list for user '%s'. (%d:%s)", self->name, err->code, err->message);
    g_clear_error(&err);
    return FALSE;
  }

  for (iter = users; iter; iter = iter->next)
  {
    ESUser* user = iter->data;
    const char* realname = eee_find_attribute_value(user->attrs, "realname");

    if (!strcmp(self->name, user->username))
      continue;
    if (exclude_users && g_slist_find_custom(exclude_users, user->username, (GCompareFunc)strcmp))
      continue;

    gtk_list_store_append(model, &titer_user);
    gtk_list_store_set(model, &titer_user, 
      0, user->username, 
      1, realname ? realname : "???",
      2, self, 
      -1);
  }

  g_slist_foreach(users, (GFunc)ESUser_free, NULL);
  g_slist_free(users);

  return TRUE;
}

gboolean eee_account_auth(EeeAccount* self)
{
  GError* err = NULL;
  guint32 flags = E_PASSWORDS_REMEMBER_FOREVER|E_PASSWORDS_SECRET;
	GConfClient* gconf;
  gboolean remember = TRUE;
  char *fail_msg = ""; 
  char *password;
  int retry_limit = 3;
  gboolean rs;
  char* key;

  if (!eee_account_connect(self))
    return FALSE;
  if (self->priv->is_authorized)
    return TRUE;

  key = g_strdup_printf("eee://%s", self->name);
  password = e_passwords_get_password(EEE_PASSWORD_COMPONENT, key);

  while (retry_limit--)
  {
    if (password == NULL)
    {
			gconf = gconf_client_get_default();
			char *path = g_strdup_printf(EEE_KEY "accounts/%s/remember_password", self->name);
			remember = gconf_client_get_bool(gconf, path, NULL);
      char* prompt = g_strdup_printf("%sEnter password for your 3E calendar account: %s.", fail_msg, self->name);
      // key must have uri format or unpatched evolution segfaults in
      // ep_get_password_keyring()
      password = e_passwords_ask_password(prompt, EEE_PASSWORD_COMPONENT, key, prompt, flags, &remember, NULL);
			gconf_client_set_bool(gconf, path, remember, NULL);
			g_free(path);
			g_object_unref(gconf);
      g_free(prompt);
      if (password == NULL)
        goto err;
    }

    rs = ESClient_authenticate(self->priv->conn, self->name, password, &err);
    g_free(password);
    password = NULL;
    if (!err && rs == TRUE)
    {
      self->priv->is_authorized = TRUE;
      g_free(key);
      return TRUE;
    }

    if (err)
    {
      g_warning("** EEE ** Authentization failed for user '%s'. (%d:%s)", self->name, err->code, err->message);
      if (err->code == ES_XMLRPC_ERROR_AUTH_FAILED)
      {
        g_clear_error(&err);
        fail_msg = "Invalid password. ";
      }
      else
      {
        g_clear_error(&err);
        goto err;
      }
    }
    else
    {
      g_warning("** EEE ** Authentization failed for user '%s' without error.", self->name);
      fail_msg = "Invalid password. ";
    }

    e_passwords_forget_password(EEE_PASSWORD_COMPONENT, key);
    flags |= E_PASSWORDS_REPROMPT;
  }

 err:
  g_free(key);
  return FALSE;
}

xr_client_conn* eee_account_connect(EeeAccount* self)
{
  GError* err = NULL;
  char* server_uri;

  g_return_val_if_fail(IS_EEE_ACCOUNT(self), NULL);

  if (self->server == NULL)
    return NULL;

  if (self->priv->conn)
    return self->priv->conn;

  //g_debug("** EEE ** Connecting to 3E server: server=%s user=%s", self->server, self->name);

  self->priv->conn = xr_client_new(&err);
  if (err)
  {
    g_warning("** EEE ** Can't create client interface. (%d:%s)", err->code, err->message);
    goto err0;
  }

  if (getenv("EEE_EVO_DISABLE_SSL"))
    server_uri = g_strdup_printf("http://%s/ESClient", self->server);
  else
    server_uri = g_strdup_printf("https://%s/ESClient", self->server);
  
  xr_client_open(self->priv->conn, server_uri, &err);
  g_free(server_uri);
  if (err)
  {
    g_warning("** EEE ** Can't open connection to the server. (%d:%s)", err->code, err->message);
    goto err1;
  }

  return self->priv->conn;

 err1:
  xr_client_free(self->priv->conn);
 err0:
  g_clear_error(&err);
  self->priv->conn = NULL;
  return NULL;
}

void eee_account_disconnect(EeeAccount* self)
{
  g_return_if_fail(IS_EEE_ACCOUNT(self));

  if (self->priv->conn)
    xr_client_free(self->priv->conn);
  self->priv->conn = NULL;
  self->priv->is_authorized = FALSE;
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

  G_OBJECT_CLASS(eee_account_parent_class)->dispose(object);
}

static void eee_account_finalize(GObject *object)
{
  EeeAccount *self = EEE_ACCOUNT(object);

  g_free(self->name);
  g_free(self->server);
  if (self->priv->conn)
    xr_client_free(self->priv->conn);
  g_slist_foreach(self->priv->cals, (GFunc)ESCalendar_free, NULL);
  g_slist_free(self->priv->cals);

  G_OBJECT_CLASS(eee_account_parent_class)->finalize(object);
}

static void eee_account_class_init(EeeAccountClass *klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
  gobject_class->dispose = eee_account_dispose;
  gobject_class->finalize = eee_account_finalize;
  g_type_class_add_private(klass, sizeof(EeeAccountPriv));
}
