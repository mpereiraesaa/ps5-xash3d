#include "../src/goldsrc_state_matrix.h"

#include <assert.h>
#include <string.h>

int main(void)
{
    assert(goldsrc_state_matrix_validate() == 0);
    assert(goldsrc_state_matrix_case(GOLDSRC_STATE_MATRIX_CASE_COUNT) == 0);
    for (uint32_t i = 0u; i < GOLDSRC_STATE_MATRIX_CASE_COUNT; ++i) {
        const GoldSrcStateMatrixCase *const item = goldsrc_state_matrix_case(i);
        assert(item && item->name && item->name[0]);
        assert(goldsrc_state_matrix_index(
                   (uint64_t)i * GOLDSRC_STATE_MATRIX_HOLD_FRAMES) == i);
        assert(goldsrc_state_matrix_index(
                   (uint64_t)i * GOLDSRC_STATE_MATRIX_HOLD_FRAMES +
                   GOLDSRC_STATE_MATRIX_HOLD_FRAMES - 1u) == i);
    }
    assert(goldsrc_state_matrix_index(
               (uint64_t)GOLDSRC_STATE_MATRIX_CASE_COUNT *
               GOLDSRC_STATE_MATRIX_HOLD_FRAMES) == 0u);
    assert(strcmp(goldsrc_state_matrix_case(0)->name, "opaque") == 0);
    assert(strcmp(goldsrc_state_matrix_case(8)->name, "lightmap-off") == 0);
    return 0;
}
