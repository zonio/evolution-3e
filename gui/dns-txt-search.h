#ifndef __HEADER_H
#define __HEADER_H

#include <glib.h>

/** Get list of TXT records found on the DNS server.
 *
 * @param name Domain name.
 *
 * @return Array of strings. Free it using g_strfreev().
 */
char** get_txt_records(const char *name);

#endif

