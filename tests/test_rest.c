/* Rest sets the controller aside and waking picks it up, without stopping the payload. */
#include "rest.h"
#include "anypad/ps5_power.h"

#include <stdio.h>

static int g_fail;

#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); g_fail++; } } while (0)

int main(void)
{
    int asleep = 0;

    CHECK(rest_transition(&asleep, POWER_WORKING) == REST_NOTHING);
    CHECK(rest_transition(&asleep, POWER_UNKNOWN) == REST_NOTHING && !asleep);
    CHECK(rest_transition(&asleep, POWER_GOING_TO_REST) == REST_SLEEP && asleep);
    CHECK(rest_transition(&asleep, POWER_IN_REST) == REST_NOTHING);          /* once only */
    CHECK(rest_transition(&asleep, POWER_UNKNOWN) == REST_NOTHING && asleep); /* no blind wake-up */
    CHECK(rest_transition(&asleep, POWER_WORKING) == REST_WAKE && !asleep);
    CHECK(rest_transition(&asleep, POWER_WORKING) == REST_NOTHING);
    CHECK(rest_transition(&asleep, POWER_IN_REST) == REST_SLEEP);            /* straight to rest */
    if (g_fail) {
        printf("test_rest: %d failure(s)\n", g_fail);
        return 1;
    }
    printf("test_rest: OK\n");
    return 0;
}
