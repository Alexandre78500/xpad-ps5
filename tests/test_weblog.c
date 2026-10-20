/* Host test: the LAN log page never carries raw kernel lines or user ids. */
#include "weblog.h"

#include <stdio.h>
#include <string.h>

static int g_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

int main(void)
{
    const char *log =
        "[    100] xpad-ps5 1.0.1: starting\n"
        "[    101] users: initial 0xffffffff, foreground 0x113a1065, signed in 0x113a1065\n"
        "[    102] vpad: main user 0x113a1065\n"
        "[    103] klog| ugen1.2: <Microsoft, Controller> at usbus1\n"
        "[    104] klog: <118>#LOGIN MGR# [Assign] DeviceId:0x5030d / UserId:0x113a1065\n"
        "[    105] slot 0: device 0x5030d bound to user 0x113a1065: 0\n"
        "[    106] pad: virtual DualSense ready\n";
    char out[1024], tiny[40];

    web_filter(log, out, sizeof out);
    CHECK(strstr(out, "starting") != NULL);
    CHECK(strstr(out, "klog| ugen1.2") != NULL);
    CHECK(strstr(out, "virtual DualSense ready") != NULL);
    CHECK(strstr(out, "0x113a1065") == NULL);
    CHECK(strstr(out, "LOGIN MGR") == NULL);

    web_filter(log, tiny, sizeof tiny);             /* too small: whole lines only, terminated */
    CHECK(strlen(tiny) < sizeof tiny);
    CHECK(strstr(tiny, "0x113a1065") == NULL);

    CHECK(web_private("no bracket") == 0);
    CHECK(web_private("ends]") == 0);
    CHECK(web_private("[1]users: x") == 1);
    if (g_fail) {
        printf("test_weblog: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("test_weblog: OK\n");
    return 0;
}
