#include "e-cal-backend-3e-utils.h"

void
e_cal_backend_notify_gerror_error(ECalBackend * backend,
                                  char *message,
                                  GError* err)
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
void
icomp_x_prop_set(icalcomponent *comp,
                 const char *key,
                 const char *value)
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
const char *
icomp_x_prop_get (icalcomponent *comp,
                  const char *key)
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
gboolean
icomp_get_deleted_status(icalcomponent* comp)
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

const char*
icomp_get_uid(icalcomponent* comp)
{
  icalproperty* iproperty;

  g_return_val_if_fail(comp != NULL, NULL);

  iproperty = icalcomponent_get_first_property(comp, ICAL_UID_PROPERTY);

  if (iproperty)
    return g_strdup(icalproperty_as_ical_string(iproperty));

  return NULL;
}

// set internal synchro state
void
e_cal_component_set_sync_state(ECalComponent *comp,
                               ECalComponentSyncState state)
{
  char *state_string;
  icalcomponent *icomp;

  g_return_if_fail(comp != NULL);

  icomp = e_cal_component_get_icalcomponent(comp);
  state_string = g_strdup_printf ("%d", state);
  icomp_x_prop_set(icomp, "X-EEE-SYNC-STATE", state_string);
  g_free (state_string);
}

void
e_cal_component_set_stamp(ECalComponent *comp,
                          const gchar* stamp)
{
  GSList                  *comments;
  ECalComponentText       *summary;

  g_return_if_fail(comp != NULL);
  g_return_if_fail(stamp != NULL);

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

const gchar*
e_cal_component_get_stamp(ECalComponent* comp)
{
  GSList                  *comments;
  ECalComponentText       *summary;

  g_return_val_if_fail(comp != NULL, NULL);

  e_cal_component_get_comment_list(comp, &comments);

  if (!comments)
    return NULL;

  summary = comments->data;

  return summary->value;
}

// get internal synchro state
ECalComponentSyncState
e_cal_component_get_sync_state(ECalComponent* comp)
{
  icalcomponent       *icomp;
  const char          *state_string;
  char                *endptr;
  int                 int_state ;

  g_return_val_if_fail(comp != NULL, E_CAL_COMPONENT_IN_SYNCH);

  icomp = e_cal_component_get_icalcomponent(comp);
  state_string = icomp_x_prop_get(icomp, "X-EEE-SYNC-STATE");
  int_state = g_ascii_strtoull(state_string, &endptr, 0);

  // unknown state: do not synchronize
  if (endptr == state_string || (int_state < 0 || int_state > E_CAL_COMPONENT_LOCALLY_MODIFIED))
    int_state = E_CAL_COMPONENT_IN_SYNCH;

  return (ECalComponentSyncState)int_state;
}

void
e_cal_component_get_ids(ECalComponent* comp,
                        const gchar** uid,
                        const gchar** rid)
{
  e_cal_component_get_uid(comp, uid);
  *rid = e_cal_component_get_recurid_as_string(comp);
}

gint
e_cal_component_compare(gconstpointer ptr1,
                        gconstpointer ptr2)
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

