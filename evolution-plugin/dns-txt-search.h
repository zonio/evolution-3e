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
