#include "ref_agc_2d_state.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>

int main(void)
{
    RefAgc2DState state = {0};
    ref_agc_2d_reset(&state);
    for (unsigned i = 0; i < 4u; ++i)
        assert(state.color[i] == 255u);
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
    ref_agc_2d_set_mode(&state, 1);
    assert(state.in_2d && state.alpha_test);
    ref_agc_2d_after_fill(&state, 10, 20, 30, 40);
    assert(state.render_mode == 4 && state.alpha_test);
    assert(state.color[0] == 10 && state.color[3] == 40);
    ref_agc_2d_set_mode(&state, 1);
    assert(state.color[0] == 10); /* repeated enable is a no-op */
    ref_agc_2d_set_render_mode(&state, 5);
    assert(!state.alpha_test);
    ref_agc_2d_after_fill(&state, 50, 60, 70, 80);
    assert(state.render_mode == 0 && !state.alpha_test);
    ref_agc_2d_set_mode(&state, 0);
    assert(!state.in_2d && state.color[0] == 50);
    ref_agc_2d_set_render_mode(&state, 0x1000);
    ref_agc_2d_set_mode(&state, 1);
    assert(state.alpha_test && state.render_mode == 0x1000);
    assert(state.color[0] == 255); /* entering 2D does not disable blending */
    puts("RefAPI requested 2D mode state tests passed");
    return 0;
}
