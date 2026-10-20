/* Rest mode: the controller is set aside during rest and picked up again on
 * wake, without stopping the payload. Payload Manager's autoload only runs on a
 * full boot, so a payload that quits on rest never comes back after waking
 * (seen on 2026-10-20, while kstuff and ShadowMountPlus survived the rest).
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef XPAD_REST_H
#define XPAD_REST_H

enum { REST_NOTHING, REST_SLEEP, REST_WAKE };

/* `power` is a power_state() value (anypad/ps5_power.h). Updates *asleep and
 * says what to do. Only a "working" state wakes up: an unknown one changes nothing. */
int rest_transition(int *asleep, int power);

#endif
