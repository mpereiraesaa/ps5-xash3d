#include "ref_agc_2d_state.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

int main(void)
{
    RefAgc2DState state = {0};
    /* MainUI opaque -> alpha -> additive font -> opaque restore. */
    const int sequence[] = {0, 2, 5, 0, 4, 1, 3, 0x1000, 2, 0};
    int captured[sizeof(sequence) / sizeof(sequence[0])];
    for (unsigned i = 0; i < sizeof(sequence) / sizeof(sequence[0]); ++i) {
        ref_agc_2d_set_render_mode(&state, sequence[i]);
        captured[i] = state.render_mode;
    }
    for (unsigned i = 0; i < sizeof(sequence) / sizeof(sequence[0]); ++i)
        assert(captured[i] == sequence[i]); /* later state cannot mutate draws */
    const int invalid[] = {-1, 6, INT_MIN, INT_MAX};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        ref_agc_2d_set_render_mode(&state, invalid[i]);
        assert(state.render_mode == 0);
    }
    puts("RefAPI requested 2D mode state tests passed");
    return 0;
}
