/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "rest.h"
#include "anypad/ps5_power.h"

int rest_transition(int *asleep, int power)
{
    int resting = power == POWER_GOING_TO_REST || power == POWER_IN_REST;

    if (resting && !*asleep) {
        *asleep = 1;
        return REST_SLEEP;
    }
    if (*asleep && power == POWER_WORKING) {
        *asleep = 0;
        return REST_WAKE;
    }
    return REST_NOTHING;
}
