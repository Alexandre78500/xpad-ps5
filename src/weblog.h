/* What the read-only log page may show. SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef XPAD_WEBLOG_H
#define XPAD_WEBLOG_H

#include <stddef.h>

/* 1 if a log line stays off the page: anything but this payload's own
 * messages, and any line that mentions a user. */
int  web_private(const char *line);
/* Copies the lines of `in` that the page may show into `out` (n bytes, always terminated). */
void web_filter(const char *in, char *out, size_t n);
/* The page: the showable lines of the in-memory log (anypad/log.c). When they
 * do not all fit in n bytes, the oldest go, never the latest. */
void web_page(char *out, size_t n);

#endif
