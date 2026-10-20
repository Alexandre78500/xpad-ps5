/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gip.h"

#include <math.h>
#include <string.h>

#define GIP_OPT_ACK      0x10
#define GIP_OPT_INTERNAL 0x20
#define GIP_CMD_LED      0x0A
#define GIP_CMD_RUMBLE   0x09

#define TRIGGER_PRESSED  120
/* Radial stick deadzone: an Xbox pad at rest drifts by a few thousand
 * (measured on a PS5: -2950 on the left vertical axis). */
#define STICK_DEADZONE   4000    /* out of 1023: L2/R2 count as pressed above this */

static unsigned le16(const uint8_t *p) { return (unsigned)p[0] | (unsigned)p[1] << 8; }

void gip_reset(gip_state *g)
{
    memset(g, 0, sizeof *g);
}

int gip_parse(gip_state *g, const uint8_t *p, int n, uint8_t ack[16], int *ack_len)
{
    *ack_len = 0;
    if (n < 4) return GIP_IGNORED;

    switch (p[0]) {
    case GIP_CMD_INPUT:
        if (n < 18) return GIP_IGNORED;
        g->b4 = p[4];
        g->b5 = p[5];
        g->lt = (uint16_t)le16(p + 6);
        g->rt = (uint16_t)le16(p + 8);
        g->lx = (int16_t)le16(p + 10);
        g->ly = (int16_t)le16(p + 12);
        g->rx = (int16_t)le16(p + 14);
        g->ry = (int16_t)le16(p + 16);
        g->have_input = 1;
        return GIP_CHANGED;

    case GIP_CMD_GUIDE:
        if (n < 5) return GIP_IGNORED;
        /* The controller repeats this packet until it is acknowledged. */
        if (p[1] == (GIP_OPT_ACK | GIP_OPT_INTERNAL)) {
            static const uint8_t tpl[13] = {
                GIP_CMD_ACK, GIP_OPT_INTERNAL, 0x00, 0x09, 0x00,
                GIP_CMD_GUIDE, GIP_OPT_INTERNAL, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00,
            };
            memcpy(ack, tpl, sizeof tpl);
            ack[2] = p[2];
            *ack_len = (int)sizeof tpl;
        }
        g->guide = p[4] & 0x01;
        return GIP_CHANGED;

    case GIP_CMD_ANNOUNCE:
    case GIP_CMD_STATUS:
        return GIP_INFO;

    default:
        return GIP_IGNORED;
    }
}

static int put(uint8_t pkt[16], uint8_t *seq, const uint8_t *tpl, int n)
{
    memcpy(pkt, tpl, (size_t)n);
    pkt[2] = (*seq)++;
    return n;
}

int gip_init_packets(uint16_t vid, uint16_t pid, uint8_t *seq, uint8_t pkt[GIP_INIT_MAX][16], int len[GIP_INIT_MAX])
{
    static const uint8_t power_on[] = { GIP_CMD_POWER, GIP_OPT_INTERNAL, 0, 0x01, 0x00 };
    static const uint8_t s_init[]   = { GIP_CMD_POWER, GIP_OPT_INTERNAL, 0, 0x0f, 0x06 };
    static const uint8_t elite2[]   = { 0x4d, 0x10, 0, 0x02, 0x07, 0x00 };
    static const uint8_t led_on[]   = { GIP_CMD_LED, GIP_OPT_INTERNAL, 0, 0x03, 0x00, 0x01, 0x14 };
    int k = 0;

    len[k] = put(pkt[k], seq, power_on, sizeof power_on); k++;
    if (vid == 0x045e && pid == 0x02ea) { len[k] = put(pkt[k], seq, s_init, sizeof s_init); k++; }
    if (vid == 0x045e && pid == 0x0b00) { len[k] = put(pkt[k], seq, elite2, sizeof elite2); k++; }
    len[k] = put(pkt[k], seq, led_on, sizeof led_on); k++;
    return k;
}

int gip_rumble_packet(uint8_t *seq, uint8_t strong, uint8_t weak, uint8_t out[16])
{
    const uint8_t tpl[13] = {
        GIP_CMD_RUMBLE, 0x00, 0, 0x09, 0x00, 0x0F, 0x00, 0x00,
        (uint8_t)(strong / 2), (uint8_t)(weak / 2), 0x20, 0x00, 0x00,
    };
    return put(out, seq, tpl, sizeof tpl);
}

static uint8_t axis(float v, int invert)
{
    int u = (int)lrintf(v * 127.5f + 127.5f);      /* -1..1 → 0..255 */

    if (v == 0.0f) return 128;                      /* the exact centre, whichever way */
    if (u < 0) u = 0;
    if (u > 255) u = 255;
    return (uint8_t)(invert ? 255 - u : u);
}

/* The stick (x, y) scaled to -1..1 outside the deadzone, keeping full travel. */
static void stick(int16_t x, int16_t y, int invert_y, uint8_t *ox, uint8_t *oy)
{
    float fx = (float)x, fy = (float)y, mag = sqrtf(fx * fx + fy * fy), k;

    if (mag <= STICK_DEADZONE) {
        *ox = *oy = 128;
        return;
    }
    k = (mag > 32767.0f ? 1.0f : (mag - STICK_DEADZONE) / (32767.0f - STICK_DEADZONE)) / mag;
    *ox = axis(fx * k, 0);
    *oy = axis(fy * k, invert_y);
}

void gip_to_pad(const gip_state *g, pad_state *out)
{
    uint32_t b = 0;
    int view = g->b4 & 0x08, menu = g->b4 & 0x04;

    pad_state_reset(out);
    if (g->have_input) {
        if (view && menu) b |= PAD_TOUCHPAD;
        else {
            if (menu) b |= PAD_OPTIONS;
            if (view) b |= PAD_CREATE;
        }
        if (g->b4 & 0x10) b |= PAD_CROSS;
        if (g->b4 & 0x20) b |= PAD_CIRCLE;
        if (g->b4 & 0x40) b |= PAD_SQUARE;
        if (g->b4 & 0x80) b |= PAD_TRIANGLE;
        if (g->b5 & 0x01) b |= PAD_UP;
        if (g->b5 & 0x02) b |= PAD_DOWN;
        if (g->b5 & 0x04) b |= PAD_LEFT;
        if (g->b5 & 0x08) b |= PAD_RIGHT;
        if (g->b5 & 0x10) b |= PAD_L1;
        if (g->b5 & 0x20) b |= PAD_R1;
        if (g->b5 & 0x40) b |= PAD_L3;
        if (g->b5 & 0x80) b |= PAD_R3;
        if (g->lt > TRIGGER_PRESSED) b |= PAD_L2;
        if (g->rt > TRIGGER_PRESSED) b |= PAD_R2;
        out->l2 = (uint8_t)((g->lt > 1023 ? 1023 : g->lt) >> 2);
        out->r2 = (uint8_t)((g->rt > 1023 ? 1023 : g->rt) >> 2);
        stick(g->lx, g->ly, 1, &out->lx, &out->ly);     /* DualSense: y grows downwards */
        stick(g->rx, g->ry, 1, &out->rx, &out->ry);
    }
    if (g->guide) b |= PAD_PS;
    out->buttons = b;
}

int gip_find_interface(const uint8_t *cfg, int len, gip_iface *out)
{
    int off = 0, in_gip = 0;

    memset(out, 0, sizeof *out);
    while (off + 2 <= len) {
        int dl = cfg[off], type = cfg[off + 1];
        if (dl < 2 || off + dl > len) break;
        if (type == 0x04 && dl >= 9) {                  /* interface */
            if (in_gip && out->ep_in && out->ep_out) return 1;
            in_gip = cfg[off + 3] == 0 && cfg[off + 5] == GIP_IF_CLASS &&
                     cfg[off + 6] == GIP_IF_SUBCLASS && cfg[off + 7] == GIP_IF_PROTOCOL;
            if (in_gip) {
                memset(out, 0, sizeof *out);
                out->iface = cfg[off + 2];
            }
        } else if (type == 0x05 && dl >= 7 && in_gip) { /* endpoint */
            uint8_t addr = cfg[off + 2];
            uint16_t mps = (uint16_t)le16(cfg + off + 4) & 0x7FF;
            if ((cfg[off + 3] & 0x03) == 0x03) {        /* interrupt */
                if ((addr & 0x80) && !out->ep_in) { out->ep_in = addr; out->mps_in = mps; }
                if (!(addr & 0x80) && !out->ep_out) { out->ep_out = addr; out->mps_out = mps; }
            }
        }
        off += dl;
    }
    if (in_gip && out->ep_in && out->ep_out) return 1;
    memset(out, 0, sizeof *out);
    return 0;
}
