/* Host tests of the GIP protocol: real packets as described by Linux xpad,
 * plus the rest drift measured on a PS5. */
#include "gip.h"

#include <stdio.h>
#include <string.h>

static int g_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

static void test_input_mapping(void)
{
    gip_state g;
    pad_state p;
    uint8_t ack[16];
    int ack_len;
    /* A + LB + d-pad up, LT fully pressed, RT half way, left stick up-right. */
    uint8_t pkt[18] = { 0x20, 0x00, 0x05, 0x0e, 0x10, 0x11, 0xff, 0x03, 0x00, 0x02,
                        0xff, 0x7f, 0xff, 0x7f, 0x00, 0x00, 0x00, 0x80 };

    gip_reset(&g);
    CHECK(gip_parse(&g, pkt, sizeof pkt, ack, &ack_len) == GIP_CHANGED);
    CHECK(ack_len == 0);
    gip_to_pad(&g, &p);
    CHECK(p.buttons == (PAD_CROSS | PAD_L1 | PAD_UP | PAD_L2 | PAD_R2));
    CHECK(p.l2 == 255);
    CHECK(p.r2 == 128);
    CHECK(p.lx >= 216 && p.lx <= 219);  /* full diagonal: ~0.71 on each axis */
    CHECK(p.ly >= 36 && p.ly <= 39);    /* up on Xbox = small y on DualSense */
    CHECK(p.rx == 128);
    CHECK(p.ry == 255);         /* -32768 (down), full */
}

static void test_all_buttons(void)
{
    gip_state g;
    pad_state p;
    uint8_t ack[16];
    int ack_len;
    uint8_t pkt[18] = { 0x20, 0x00, 0x01, 0x0e, 0xf4, 0xff };

    gip_reset(&g);
    gip_parse(&g, pkt, sizeof pkt, ack, &ack_len);
    gip_to_pad(&g, &p);
    /* Menu alone (not View): Options; every other button. */
    CHECK(p.buttons == (PAD_OPTIONS | PAD_CROSS | PAD_CIRCLE | PAD_SQUARE | PAD_TRIANGLE |
                        PAD_UP | PAD_DOWN | PAD_LEFT | PAD_RIGHT | PAD_L1 | PAD_R1 | PAD_L3 | PAD_R3));
    CHECK(p.lx == 128 && p.ly == 128);

    pkt[4] = 0x08;              /* View alone */
    pkt[5] = 0;
    gip_parse(&g, pkt, sizeof pkt, ack, &ack_len);
    gip_to_pad(&g, &p);
    CHECK(p.buttons == PAD_CREATE);

    pkt[4] = 0x0c;              /* View + Menu: touchpad only */
    gip_parse(&g, pkt, sizeof pkt, ack, &ack_len);
    gip_to_pad(&g, &p);
    CHECK(p.buttons == PAD_TOUCHPAD);
}

static void test_guide_and_ack(void)
{
    gip_state g;
    pad_state p;
    uint8_t ack[16];
    int ack_len;
    uint8_t down[6] = { 0x07, 0x30, 0x2a, 0x02, 0x01, 0x5b };
    uint8_t up[6]   = { 0x07, 0x20, 0x2b, 0x02, 0x00, 0x5b };

    gip_reset(&g);
    CHECK(gip_parse(&g, down, sizeof down, ack, &ack_len) == GIP_CHANGED);
    CHECK(ack_len == 13);
    CHECK(ack[0] == 0x01 && ack[2] == 0x2a && ack[5] == 0x07);
    gip_to_pad(&g, &p);
    CHECK(p.buttons == PAD_PS);
    CHECK(p.lx == 128);         /* no 0x20 report yet: sticks centred */

    CHECK(gip_parse(&g, up, sizeof up, ack, &ack_len) == GIP_CHANGED);
    CHECK(ack_len == 0);
    gip_to_pad(&g, &p);
    CHECK(p.buttons == 0);
}

static void test_short_and_unknown(void)
{
    gip_state g;
    uint8_t ack[16];
    int ack_len;
    uint8_t shortin[10] = { 0x20, 0x00, 0x01, 0x0e, 0x10 };
    uint8_t announce[8] = { 0x02, 0x20, 0x01, 0x1c, 0x7e, 0xed, 0x8b, 0x11 };
    uint8_t odd[4] = { 0x42, 0, 0, 0 };

    gip_reset(&g);
    CHECK(gip_parse(&g, shortin, sizeof shortin, ack, &ack_len) == GIP_IGNORED);
    CHECK(!g.have_input);
    CHECK(gip_parse(&g, announce, sizeof announce, ack, &ack_len) == GIP_INFO);
    CHECK(gip_parse(&g, odd, sizeof odd, ack, &ack_len) == GIP_IGNORED);
    CHECK(gip_parse(&g, odd, 2, ack, &ack_len) == GIP_IGNORED);
}

static void test_deadzone(void)
{
    gip_state g;
    pad_state p;
    uint8_t ack[16];
    int ack_len;
    /* Rest position measured on a PS5: LX 199, LY -2950, RX 828, RY -689. */
    uint8_t rest[18] = { 0x20, 0x00, 0x03, 0x0e, 0, 0, 0, 0, 0, 0, 0xc7, 0x00, 0x7a, 0xf4, 0x3c, 0x03, 0x4f, 0xfd };
    uint8_t right[18] = { 0x20, 0x00, 0x04, 0x0e, 0, 0, 0, 0, 0, 0, 0xff, 0x7f, 0, 0, 0, 0, 0, 0 };
    uint8_t left[18] = { 0x20, 0x00, 0x05, 0x0e, 0, 0, 0, 0, 0, 0, 0x00, 0x80, 0, 0, 0, 0, 0, 0 };

    gip_reset(&g);
    gip_parse(&g, rest, sizeof rest, ack, &ack_len);
    gip_to_pad(&g, &p);
    CHECK(p.lx == 128 && p.ly == 128 && p.rx == 128 && p.ry == 128);
    gip_parse(&g, right, sizeof right, ack, &ack_len);
    gip_to_pad(&g, &p);
    CHECK(p.lx == 255 && p.ly == 128);
    gip_parse(&g, left, sizeof left, ack, &ack_len);
    gip_to_pad(&g, &p);
    CHECK(p.lx == 0 && p.ly == 128);
}

static void test_init_packets(void)
{
    uint8_t pkt[GIP_INIT_MAX][16], seq = 0;
    int len[GIP_INIT_MAX], n;

    n = gip_init_packets(0x045e, 0x02d1, &seq, pkt, len);
    CHECK(n == 2);
    CHECK(len[0] == 5 && pkt[0][0] == 0x05 && pkt[0][1] == 0x20 && pkt[0][2] == 0 && pkt[0][3] == 1 && pkt[0][4] == 0);
    CHECK(pkt[1][0] == 0x0a && pkt[1][2] == 1);
    CHECK(seq == 2);

    n = gip_init_packets(0x045e, 0x02ea, &seq, pkt, len);       /* One S: one extra packet */
    CHECK(n == 3 && pkt[1][3] == 0x0f && pkt[1][4] == 0x06);
}

static void test_find_interface(void)
{
    /* Configuration of an Xbox One controller 045e:02d1 (lsusb -v). */
    static const uint8_t cfg[] = {
        0x09, 0x02, 0x40, 0x00, 0x03, 0x01, 0x00, 0xa0, 0xfa,
        0x09, 0x04, 0x00, 0x00, 0x02, 0xff, 0x47, 0xd0, 0x00,
        0x07, 0x05, 0x01, 0x03, 0x40, 0x00, 0x04,
        0x07, 0x05, 0x81, 0x03, 0x40, 0x00, 0x04,
        0x09, 0x04, 0x01, 0x00, 0x00, 0xff, 0x47, 0xd0, 0x00,
        0x09, 0x04, 0x01, 0x01, 0x02, 0xff, 0x47, 0xd0, 0x00,
        0x07, 0x05, 0x02, 0x01, 0xe4, 0x00, 0x01,
        0x07, 0x05, 0x82, 0x01, 0x40, 0x00, 0x01,
    };
    static const uint8_t hid_only[] = {
        0x09, 0x02, 0x22, 0x00, 0x01, 0x01, 0x00, 0xa0, 0xfa,
        0x09, 0x04, 0x00, 0x00, 0x01, 0x03, 0x00, 0x00, 0x00,
        0x07, 0x05, 0x81, 0x03, 0x40, 0x00, 0x04,
    };
    gip_iface gi;

    CHECK(gip_find_interface(cfg, sizeof cfg, &gi) == 1);
    CHECK(gi.iface == 0 && gi.ep_in == 0x81 && gi.ep_out == 0x01 && gi.mps_in == 64);
    CHECK(gip_find_interface(hid_only, sizeof hid_only, &gi) == 0);
    CHECK(gip_find_interface(cfg, 20, &gi) == 0);              /* truncated */
}

int main(void)
{
    test_input_mapping();
    test_all_buttons();
    test_guide_and_ack();
    test_short_and_unknown();
    test_deadzone();
    test_init_packets();
    test_find_interface();
    if (g_fail) {
        printf("test_gip: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("test_gip: OK\n");
    return 0;
}
