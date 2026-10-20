/* Host test: the LAN log page never carries raw kernel lines or user ids. */
#include "weblog.h"
#include "anypad/log.h"

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
        "[    106] pad: virtual DualSense ready\n"
        "[    107] vpad: scePadOpen of the user's pad (user 0x113a1065): 0x0\n"
        "[    108] klog| <118>[LoginMgr] eventType=DeviceAdded / UserId=0x113a1065\n"
        "[    109] something new that nobody vetted 0xdeadbeef\n"
        "[    110] klog: released\n"
        "[    111] rest: controller set aside until wake-up\n"
        "[    112] wake: looking for the controller\n";
    char out[1024], tiny[40];

    web_filter(log, out, sizeof out);
    CHECK(strstr(out, "starting") != NULL);
    CHECK(strstr(out, "klog| ugen1.2") != NULL);
    CHECK(strstr(out, "virtual DualSense ready") != NULL);
    CHECK(strstr(out, "0x113a1065") == NULL);
    CHECK(strstr(out, "LOGIN MGR") == NULL);
    CHECK(strstr(out, "LoginMgr") == NULL);
    CHECK(strstr(out, "nobody vetted") == NULL);     /* unknown messages stay off the page */
    CHECK(strstr(out, "klog: released") != NULL);

    CHECK(strstr(out, "rest: controller set aside") != NULL);
    CHECK(strstr(out, "wake: looking for the controller") != NULL);

    web_filter(log, tiny, sizeof tiny);             /* too small: whole lines only, terminated */
    CHECK(strlen(tiny) < sizeof tiny);
    CHECK(strstr(tiny, "0x113a1065") == NULL);

    CHECK(web_private("no bracket") == 1);
    CHECK(web_private("ends]") == 1);
    CHECK(web_private("[1]users: x") == 1);
    CHECK(web_private("[1] usb: /dev/ugen1.2 045e:02ea") == 0);
    CHECK(web_private("[1] usb: held by USER 3") == 1);
    /* The page keeps the end of the log: 700 lines with kernel noise between them. */
    {
        static char page[2048];
        int k;
        log_open("/dev/null");
        for (k = 0; k < 700; k++) {
            log_line("pad: line %d", k);
            log_line("klog: <118>noise %d", k);
        }
        log_line("wake: looking for the controller");
        web_page(page, sizeof page);
        CHECK(strstr(page, "wake: looking for the controller") != NULL);   /* the last line is there */
        CHECK(strstr(page, "pad: line 699") != NULL);
        CHECK(strstr(page, "pad: line 100\n") == NULL);                    /* the old ones went */
        CHECK(strstr(page, "noise") == NULL);
        CHECK(strlen(page) < sizeof page);
        log_close();
    }

    if (g_fail) {
        printf("test_weblog: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("test_weblog: OK\n");
    return 0;
}
