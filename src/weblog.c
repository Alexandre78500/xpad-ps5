/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "weblog.h"

#include <string.h>

/* The page is reachable from the LAN, so it only carries this payload's own
 * lines and the USB-filtered kernel lines ("klog| "). The raw kernel lines
 * copied while the virtual pad is identified ("klog: ") and the console's
 * user ids ("users: ") stay in the log file on the console. */
int web_private(const char *line)
{
    const char *p = strchr(line, ']');

    if (!p) return 0;
    for (p++; *p == ' '; p++) {}
    return strncmp(p, "klog: ", 6) == 0 || strncmp(p, "users: ", 7) == 0 ||
           strncmp(p, "vpad: main user", 15) == 0 || strstr(p, "bound to user") != NULL;
}

void web_filter(const char *in, char *out, size_t n)
{
    size_t used = 0;

    out[0] = 0;
    while (*in) {
        const char *end = strchr(in, '\n');
        size_t len = end ? (size_t)(end - in) + 1 : strlen(in);
        char line[256];
        size_t k = len < sizeof line - 1 ? len : sizeof line - 1;

        memcpy(line, in, k);
        line[k] = 0;
        if (!web_private(line) && used + len < n) {
            memcpy(out + used, in, len);
            used += len;
            out[used] = 0;
        }
        in += len;
    }
}
