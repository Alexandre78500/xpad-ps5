/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "weblog.h"

#include <string.h>

/* The page is reachable from the LAN, so it works from an allowlist: only
 * this payload's own messages, by their prefix, are shown. The raw kernel
 * lines copied while the virtual pad is identified ("klog: <...>") are not
 * among them, and any line that mentions a user (the console's user ids, in
 * any form) is left out too. Everything stays in the log file on the console. */
static const char *const k_shown[] = {
    "xpad-ps5 ", "usb: ", "usb seen: ", "usbd: ", "pad: ", "dev: ", "klog| ",
    "klog: local server", "klog: /dev/klog ", "klog: released",
    "vpad: unavailable", "vpad: trying again", "stopping: ", "stopped",
};

static int mentions_user(const char *p)
{
    for (; *p; p++)
        if ((p[0] | 0x20) == 'u' && (p[1] | 0x20) == 's' && (p[2] | 0x20) == 'e' && (p[3] | 0x20) == 'r')
            return 1;
    return 0;
}

int web_private(const char *line)
{
    const char *p = strchr(line, ']');
    size_t i;

    if (!p) return 1;
    for (p++; *p == ' '; p++) {}
    if (mentions_user(p)) return 1;
    for (i = 0; i < sizeof k_shown / sizeof k_shown[0]; i++)
        if (strncmp(p, k_shown[i], strlen(k_shown[i])) == 0) return 0;
    return 1;
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
