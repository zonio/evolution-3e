#include <glib.h>
#include <string.h>
#include "utils.h"

char* qp_escape_string(const char* s)
{
  if (s == NULL)
    return NULL;
  char* r = g_malloc(strlen(s)*2+3);
  char* c = r;
  *c = '\'';
  c++;
  for ( ; *s != '\0'; s++)
  {
    if (*s == '\'' || *s == '\\')
    {
      *c = '\\';
      c++;
    }
    *c = *s;
    c++;
  }
  *c = '\'';
  c++;
  *c = '\0';
  return r;
}
