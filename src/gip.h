/* The GIP protocol of Xbox One / Series controllers over USB, reduced to what
 * a gamepad needs: power-on, input reports (0x20), the Xbox button (0x07) and
 * its acknowledgement.
 *
 * Packet formats follow the Linux xpad driver (drivers/input/joystick/xpad.c).
 * No system calls here: this file is unit-tested on the host (tests/test_gip.c).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef XPAD_GIP_H
#define XPAD_GIP_H

#include <stdint.h>

#include "anypad/pad.h"

#define GIP_CMD_ACK      0x01
#define GIP_CMD_ANNOUNCE 0x02
#define GIP_CMD_STATUS   0x03
#define GIP_CMD_POWER    0x05
#define GIP_CMD_GUIDE    0x07
#define GIP_CMD_INPUT    0x20

/* USB interface class of a GIP controller. */
#define GIP_IF_CLASS     0xFF
#define GIP_IF_SUBCLASS  0x47
#define GIP_IF_PROTOCOL  0xD0

/* What the controller last sent, raw. */
typedef struct {
    uint8_t b4, b5;             /* button bytes of the 0x20 report */
    uint16_t lt, rt;            /* triggers, 0..1023 */
    int16_t lx, ly, rx, ry;     /* sticks, positive y is up */
    uint8_t guide;              /* Xbox button */
    uint8_t have_input;         /* a 0x20 report has arrived */
} gip_state;

enum {
    GIP_IGNORED = 0,            /* unknown or short packet */
    GIP_CHANGED = 1,            /* the controller state changed */
    GIP_INFO    = 2,            /* announce, status: nothing to forward */
};

void gip_reset(gip_state *g);

/* Reads a received packet. If it asks for an acknowledgement, the ack is
 * written to `ack` (13 bytes at most) and its length to *ack_len; otherwise
 * *ack_len = 0. */
int gip_parse(gip_state *g, const uint8_t *p, int n, uint8_t ack[16], int *ack_len);

/* Start-up packets, in order, for controller vid:pid. Returns how many;
 * pkt[i] holds len[i] bytes. `seq` is incremented for each packet. */
#define GIP_INIT_MAX 4
int gip_init_packets(uint16_t vid, uint16_t pid, uint8_t *seq, uint8_t pkt[GIP_INIT_MAX][16], int len[GIP_INIT_MAX]);

/* Rumble: strong and weak motors 0..255. Writes the packet to out (13 bytes)
 * and returns its length. */
int gip_rumble_packet(uint8_t *seq, uint8_t strong, uint8_t weak, uint8_t out[16]);

/* The GIP interface of a USB configuration descriptor: class ff/47/d0,
 * alternate setting 0, with an interrupt IN and an interrupt OUT endpoint.
 * Returns 1 when found. */
typedef struct {
    int iface;
    uint8_t ep_in, ep_out;
    uint16_t mps_in, mps_out;
} gip_iface;

int gip_find_interface(const uint8_t *cfg, int len, gip_iface *out);

/* The state as a DualSense, by position as on a Sony pad:
 *   A → Cross, B → Circle, X → Square, Y → Triangle,
 *   LB/RB → L1/R1, LT/RT → L2/R2, stick clicks → L3/R3,
 *   Menu → Options, View → Create, Xbox → PS,
 *   View + Menu together → touchpad click. */
void gip_to_pad(const gip_state *g, pad_state *out);

#endif
