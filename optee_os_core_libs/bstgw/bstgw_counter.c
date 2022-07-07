#include <bstgw/bstgw_counter.h>

/* Note: This is just a very dumb stub counter driven by the notifier if there
 *      is no fast / secure counter available, because the fallback to REE time
 *      is super slow. This particularly affects NPF's connection tracking.
 */

static uint32_t ms_counter = 0;

void bstgw_incr_milli_counter(uint32_t ms) {
    // TODO: overflows
    ms_counter += ms;
}

uint32_t bstgw_get_milli_counter(void) {
    return ms_counter;
}
