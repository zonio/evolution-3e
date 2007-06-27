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

/** Get 3E server hostname if possible.
 *
 * @param email E-mail.
 *
 * @return hostname:port or NULL.
 */
char* get_eee_server_hostname(const char* email);

#endif
