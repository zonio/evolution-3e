/* Copyright (c) 1996 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND INTERNET SOFTWARE CONSORTIUM DISCLAIMS
 * ALL WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL INTERNET SOFTWARE
 * CONSORTIUM BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 * PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS
 * SOFTWARE.
 */

/* Copyright 1996, 2000 by the Massachusetts Institute of Technology.
 *
 * Permission to use, copy, modify, and distribute this
 * software and its documentation for any purpose and without
 * fee is hereby granted, provided that the above copyright
 * notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting
 * documentation, and that the name of M.I.T. not be used in
 * advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.
 * M.I.T. makes no representations about the suitability of
 * this software for any purpose.  It is provided "as is"
 * without express or implied warranty.
 */

/* ripped off from hesiod */

#include <stdlib.h>
#include <errno.h>
#include <resolv.h>
#include <string.h>

#include "dns-txt-search.h"

static char * *_parse_result(const unsigned char *abuf, int alen)
{
    HEADER *hp;
    unsigned const char *p, *eom, *eor;
    char *dst, * *list;
    int ancount, qdcount, i, j, skip, type, class, len, n;

    /*
     * Parse the header of the result.
     */
    hp = (HEADER *)abuf;
    ancount = ntohs(hp->ancount);
    qdcount = ntohs(hp->qdcount);
    p = abuf + sizeof(HEADER);
    eom = abuf + alen;

    /*
     * Skip questions, trying to get to the answer section which follows.
     */
    for (i = 0; i < qdcount; i++)
    {
        skip = dn_skipname(p, eom);
        if (skip < 0 || p + skip + QFIXEDSZ > eom)
        {
            errno = EMSGSIZE;
            return NULL;
        }
        p += skip + QFIXEDSZ;
    }

    /*
     * Allocate space for the text record answers.
     */
    list = g_malloc((ancount + 1) * sizeof(char *));
    if (!list)
    {
        errno = ENOMEM;
        return NULL;
    }

    /*
     * Parse the answers.
     */
    j = 0;
    for (i = 0; i < ancount; i++)
    {
        /*
         * Parse the header of this answer.
         */
        skip = dn_skipname(p, eom);
        if (skip < 0 || p + skip + 10 > eom)
        {
            break;
        }
        type = p[skip + 0] << 8 | p[skip + 1];
        class = p[skip + 2] << 8 | p[skip + 3];
        len = p[skip + 8] << 8 | p[skip + 9];
        p += skip + 10;
        if (p + len > eom)
        {
            errno = EMSGSIZE;
            break;
        }

        /*
         * Skip entries of the wrong type.
         */
        if (type != T_TXT)
        {
            p += len;
            continue;
        }

        /*
         * Allocate space for this answer.
         */
        list[j] = g_malloc(len);
        if (!list[j])
        {
            errno = ENOMEM;
            break;
        }
        dst = list[j++];

        /*
         * Copy answer data into the allocated area.
         */
        eor = p + len;
        while (p < eor)
        {
            n = (unsigned char)*p++;
            if (p + n > eor)
            {
                errno = EMSGSIZE;
                break;
            }
            memcpy(dst, p, n);
            p += n;
            dst += n;
        }
        if (p < eor)
        {
            errno = EMSGSIZE;
            break;
        }
        *dst = 0;
    }

    /*
     * If we didn't terminate the loop normally, something went wrong.
     */
    if (i < ancount)
    {
        for (i = 0; i < j; i++)
        {
            g_free(list[i]);
        }
        g_free(list);
        return NULL;
    }

    if (j == 0)
    {
        errno = ENOENT;
        free(list);
        return NULL;
    }

    list[j] = NULL;
    return list;
}

char * *get_txt_records(const char *name)
{
    unsigned char qbuf[PACKETSZ], abuf[1024];
    int n;

    if ((_res.options & RES_INIT) == 0 && res_init() == -1)
    {
        return NULL;
    }

    n = res_mkquery(QUERY, name, C_IN, T_TXT, NULL, 0, NULL, qbuf, PACKETSZ);
    if (n < 0)
    {
        errno = EMSGSIZE;
        return NULL;
    }

    n = res_send(qbuf, n, abuf, sizeof(abuf));
    if (n < 0)
    {
        errno = ECONNREFUSED;
        return NULL;
    }

    return _parse_result(abuf, n);
}

char *get_eee_server_hostname(const char *email)
{
    char *domain = strchr(email, '@');
    char * *txt_list;
    guint i;

    if (!domain) // invalid email address
    {
        return NULL;
    }

    txt_list = get_txt_records(++domain);
    if (txt_list == NULL)
    {
        g_debug("** EEE ** 3e server hostname can't be determined for '%s'. Your admin forgot to setup 3E TXT records in DNS?", email);
        return NULL;
    }

    for (i = 0; i < g_strv_length(txt_list); i++)
    {
        // parse TXT records if any
        if (g_str_has_prefix(txt_list[i], "eee server="))
        {
            char *server = g_strstrip(g_strdup(txt_list[i] + sizeof("eee server=") - 1));
            //XXX: check format (hostname:port)
            return server;
        }
    }
    g_strfreev(txt_list);
    return NULL;
}
