#ifndef __HEADER_H
#define __HEADER_H

#include <glib.h>

/**
 * Get list of TXT records found on the DNS server.
 * @param[in] name Domain name.
 * @return Array of strings. Free it using g_strfreev().
 */
gchar * *get_txt_records(const gchar *name);

/**
 * Get 3e server hostname if possible.
 * @param[in] email E-mail.
 * @return hostname:port or NULL.
 */
gchar *get_eee_server_hostname(const gchar *email);

/**
 * Get 3e web interface hostname if possible.
 * @param[in] email Email or domain part of email to search for.
 * @return hostname:port or NULL.
 */
gchar *get_eee_web_hostname(const gchar *email);

#endif
