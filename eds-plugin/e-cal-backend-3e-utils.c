/**************************************************************************************************
 *  3E plugin for Evolution Data Server                                                           * 
 *                                                                                                *
 *  Copyright (C) 2007 by Zonio                                                                   *
 *  www.zonio.net                                                                                 *
 *  stanislav.slusny@zonio.net                                                                    *
 *                                                                                                *
 **************************************************************************************************/

#include "e-cal-backend-3e.h"
#include "e-cal-backend-3e-priv.h"
#include "e-cal-backend-3e-utils.h"

void e_cal_backend_notify_gerror_error(ECalBackend * backend, char *message, GError* err)
{
  ECalBackend3e *cb;
  ECalBackend3ePrivate *priv;

  if (err == NULL)
    return;

  T("backend=%p, message=%s, err=%p", backend, message, err);

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  char *error_message = g_strdup_printf("%s (%d: %s).", message, err->code, err->message);

  e_cal_backend_notify_error(backend, error_message);
  g_free(error_message);
}

// set property in ical component
void icomp_x_prop_set(icalcomponent *comp, const char *key, const char *value)
{
  icalproperty *iter;

  g_return_if_fail(comp != NULL);
  g_return_if_fail(key != NULL);
  g_return_if_fail(value != NULL);

  for (iter = icalcomponent_get_first_property(comp, ICAL_X_PROPERTY);
       iter;
       iter = icalcomponent_get_next_property(comp, ICAL_X_PROPERTY))
  {
    const char *str = icalproperty_get_x_name (iter);

    if (!strcmp (str, key))
    {
      icalcomponent_remove_property(comp, iter);
      icalproperty_free(iter);
      break;
    }
  }

  iter = icalproperty_new_x(value);
  icalproperty_set_x_name(iter, key);
  icalcomponent_add_property(comp, iter);
}

// get value of ical property
const char * icomp_x_prop_get (icalcomponent *comp, const char *key)
{
	icalproperty *xprop;

  g_return_val_if_fail(comp != NULL, NULL);
  g_return_val_if_fail(key != NULL, NULL);
	
	/* Find the old one first */
	xprop = icalcomponent_get_first_property (comp, ICAL_X_PROPERTY);

	while (xprop)
  {
		const char *str = icalproperty_get_x_name (xprop);
		
		if (!strcmp (str, key))
			break;

		xprop = icalcomponent_get_next_property (comp, ICAL_X_PROPERTY);
	}

	if (xprop)
		return icalproperty_get_value_as_string (xprop);	
	
	return NULL;
}

// extract X-3e-STATUS property
gboolean icomp_get_deleted_status(icalcomponent* comp)
{
  /* extract deleted flag */
  const gchar* x_deleted_string = "X-3E-STATUS";
  const char* status;

  g_return_val_if_fail(comp != NULL, FALSE);
 
  status = icomp_x_prop_get(comp, x_deleted_string);

  if (status && !strcmp(status, "deleted"))
    return TRUE;

  return FALSE;
}

const char* icomp_get_uid(icalcomponent* comp)
{
  icalproperty* iproperty;

  g_return_val_if_fail(comp != NULL, NULL);

  iproperty = icalcomponent_get_first_property(comp, ICAL_UID_PROPERTY);

  if (iproperty)
    return g_strdup(icalproperty_as_ical_string(iproperty));

  return NULL;
}

gboolean e_cal_component_has_deleted_status(ECalComponent* comp)
{
  /* extract deleted flag */
  icalcomponent *icomp;

  g_return_val_if_fail(comp != NULL, FALSE);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(comp), FALSE);
 
  icomp = e_cal_component_get_icalcomponent(comp);
  return icomp_get_deleted_status(icomp);
}

void icomp_set_sync_state(icalcomponent* icomp, ECalComponentSyncState state)
{
  char *state_string;

  state_string = g_strdup_printf ("%d", state);
  icomp_x_prop_set(icomp, "X-EEE-SYNC-STATE", state_string);
  g_free (state_string);
}

// set internal synchro state
void e_cal_component_set_sync_state(ECalComponent *comp, ECalComponentSyncState state)
{
  icalcomponent *icomp;

  g_return_if_fail(comp != NULL);
  g_return_if_fail(E_IS_CAL_COMPONENT(comp));

  icomp = e_cal_component_get_icalcomponent(comp);
  icomp_set_sync_state(icomp, state);
}

void e_cal_component_set_local_state(ECalBackend * backend, ECalComponent* comp)
{
  icalcomponent              *icomp;
  ECalComponentText          comp_summary;
  const gchar                      *text;
  ECalBackend3e*              cb;
  ECalBackend3ePrivate*       priv;

  g_return_if_fail(comp != NULL);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(E_IS_CAL_COMPONENT(comp));

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  if (!e_cal_component_is_local(comp))
  {
    icomp = e_cal_component_get_icalcomponent(comp);
    icomp_x_prop_set(icomp, "X-EEE-LOCAL", "1");
    e_cal_component_get_summary(comp, &comp_summary);
    text = comp_summary.value;
    /* FIXME: memory leak ? */
    char* oldstr = e_cal_component_get_as_string(comp);
    comp_summary.value = g_strdup_printf("EEE-LOCAL-EEE %s", text);
    comp_summary.altrep = NULL;
    e_cal_component_set_summary(comp, &comp_summary);
    char* newstr = e_cal_component_get_as_string(comp);
    e_cal_backend_cache_put_component(priv->cache, comp);
    e_cal_backend_notify_object_modified(backend, oldstr, newstr);
    g_free(oldstr);
    g_free(newstr);
  }
  else
    g_debug("LOCAL already set!");
}

void e_cal_component_unset_local_state(ECalBackend* backend, ECalComponent* comp)
{
  icalcomponent *icomp;
  ECalComponentText          comp_summary;
  const gchar* text;
  ECalBackend3e*              cb;
  ECalBackend3ePrivate*       priv;

  g_return_if_fail(comp != NULL);
  g_return_if_fail(backend != NULL);
  g_return_if_fail(E_IS_CAL_COMPONENT(comp));

  cb = E_CAL_BACKEND_3E(backend);
  priv = cb->priv;

  e_cal_component_get_summary(comp, &comp_summary);
  text = comp_summary.value;

  //FXIME: memory leak ?
  if (g_str_has_prefix(text, "EEE-LOCAL-EEE"))
  {
    comp_summary.value = g_strdup(text + 13);
    comp_summary.altrep = NULL;
  }

  icomp = e_cal_component_get_icalcomponent(comp);
  icomp_x_prop_set(icomp, "X-EEE-LOCAL", "0");

  e_cal_component_set_summary(comp, &comp_summary);
  e_cal_backend_cache_put_component(priv->cache, comp);
}

time_t e_cal_component_get_dtstamp_as_timet(ECalComponent* comp)
{
  struct icaltimetype     itt;

  g_return_val_if_fail(comp != NULL, 0);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(comp), 0);

  e_cal_component_get_dtstamp(comp, &itt);
  return icaltime_as_timet_with_zone(itt, icaltimezone_get_utc_timezone());
}

time_t icomp_get_dtstamp_as_timet(icalcomponent* comp)
{
  struct icaltimetype     itt;

  g_return_val_if_fail(comp != NULL, 0);

  itt = icalcomponent_get_dtstamp(comp);
  return icaltime_as_timet_with_zone(itt, icaltimezone_get_utc_timezone());
}

void e_cal_component_set_stamp(ECalComponent *comp, const gchar* stamp)
{
  GSList                  *comments;
  ECalComponentText       *summary;

  g_return_if_fail(comp != NULL);
  g_return_if_fail(stamp != NULL);
  g_return_if_fail(E_IS_CAL_COMPONENT(comp));

  e_cal_component_get_comment_list(comp, &comments);

  if (!comments)
  {
    summary = g_new0(ECalComponentText, 1);
    comments = g_slist_insert(comments, summary, 0);
  }
  else
  {
//    g_free(comments->data);
  }

  summary = comments->data;
  summary->value = g_strdup(stamp);
  summary->altrep = NULL;

  e_cal_component_set_comment_list(comp, comments);
}

const gchar* e_cal_component_get_stamp(ECalComponent* comp)
{
  GSList                  *comments;
  ECalComponentText       *summary;

  g_return_val_if_fail(comp != NULL, NULL);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(comp), NULL);

  e_cal_component_get_comment_list(comp, &comments);

  if (!comments)
    return NULL;

  summary = comments->data;

  return summary->value;
}

ECalComponentSyncState icomp_get_sync_state(icalcomponent* icomp)
{
  const char          *state_string;
  char                *endptr;
  int                 int_state = E_CAL_COMPONENT_LOCALLY_CREATED;

  g_return_val_if_fail(icomp != NULL, GNOME_Evolution_Calendar_OtherError);

  state_string = icomp_x_prop_get(icomp, "X-EEE-SYNC-STATE");
  if (state_string)
    int_state = atoi(state_string);

  switch (int_state)
  {
    case E_CAL_COMPONENT_IN_SYNCH:
    case E_CAL_COMPONENT_LOCALLY_CREATED:
    case E_CAL_COMPONENT_LOCALLY_MODIFIED:
    case E_CAL_COMPONENT_LOCALLY_DELETED:
      return int_state;
    default:
      return E_CAL_COMPONENT_LOCALLY_CREATED;
  }
}

// get internal synchro state
ECalComponentSyncState e_cal_component_get_sync_state(ECalComponent* comp)
{
  icalcomponent       *icomp;

  g_return_val_if_fail(comp != NULL, E_CAL_COMPONENT_IN_SYNCH);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(comp), E_CAL_COMPONENT_IN_SYNCH);

  icomp = e_cal_component_get_icalcomponent(comp);
  return icomp_get_sync_state(icomp);
}

gboolean e_cal_component_is_local(ECalComponent* comp)
{
  icalcomponent       *icomp;
  const char          *state_string;
  char                *endptr;
  int                 int_state ;

  g_return_val_if_fail(comp != NULL, TRUE);
  g_return_val_if_fail(E_IS_CAL_COMPONENT(comp), TRUE);

  icomp = e_cal_component_get_icalcomponent(comp);
  state_string = icomp_x_prop_get(icomp, "X-EEE-LOCAL");

  if (!state_string)
    return FALSE;

  int_state = g_ascii_strtoull(state_string, &endptr, 0);

  // unknown state: do not synchronize
  if (endptr == state_string || (int_state < 0 || int_state > 1))
    return FALSE;

  return int_state == 1;
}

void e_cal_component_get_ids(ECalComponent* comp, const gchar** uid, const gchar** rid)
{
  g_return_if_fail(E_IS_CAL_COMPONENT(comp));
  g_return_if_fail(uid != NULL);
  g_return_if_fail(rid != NULL);

  e_cal_component_get_uid(comp, uid);
  *rid = e_cal_component_get_recurid_as_string(comp);
}

gint e_cal_component_compare(gconstpointer ptr1, gconstpointer ptr2)
{
  ECalComponent         *comp1, *comp2;
  const gchar           *uid1, *uid2;
  const gchar           *rid1, * rid2;
  gint                  result;

  comp1 = (ECalComponent*)ptr1;
  comp2 = (ECalComponent*)ptr2;

  e_cal_component_get_uid(comp1, &uid1);
  rid1 = e_cal_component_get_recurid_as_string(comp1);

  e_cal_component_get_uid(comp2, &uid2);
  rid2 = e_cal_component_get_recurid_as_string(comp2);

  result = g_ascii_strcasecmp(uid1, uid2);
  if (!result)
  {
    if (!rid1)
    {
      if (!rid2)
        result = 0;
      else
        result = -1;
    }
    else
    {
      if (!rid2)
        result = 1;
      else
        result = g_ascii_strcasecmp(rid1, rid2);
    }
  }

  return result;
}

gboolean e_cal_has_write_permission(const char* perm_string)
{
  return strcmp(perm_string, "write") == 0;
}
