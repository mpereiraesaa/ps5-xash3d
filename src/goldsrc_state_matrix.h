#ifndef PS5_XASH3D_GOLDSRC_STATE_MATRIX_H
#define PS5_XASH3D_GOLDSRC_STATE_MATRIX_H

#include "goldsrc_render_state.h"

#include <stdint.h>

enum {
    GOLDSRC_STATE_MATRIX_CASE_COUNT = 9,
    GOLDSRC_STATE_MATRIX_HOLD_FRAMES = 300,
};

typedef struct GoldSrcStateMatrixCase {
    const char *name;
    GoldSrcRenderState state;
    float render_color[4];
    float fog_color_density[4];
} GoldSrcStateMatrixCase;

const GoldSrcStateMatrixCase *goldsrc_state_matrix_case(uint32_t index);
uint32_t goldsrc_state_matrix_index(uint64_t frame_index);
int goldsrc_state_matrix_validate(void);

#endif
