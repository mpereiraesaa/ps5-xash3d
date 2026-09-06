#include "../src/ps5_viewport_scissor.h"

#include <assert.h>
#include <stddef.h>

int main(void)
{
    ps5_agc_register full[PS5_VIEWPORT_SCISSOR_REGISTER_COUNT];
    const Ps5RectU32 full_rect = {0u, 0u, 1920u, 1080u};
    assert(ps5_viewport_scissor_build(
        full, &full_rect, &full_rect, 1920u, 1080u) == 0);
    assert(full[0].offset == 0x10fu && full[0].value == 0x44700000u);
    assert(full[1].offset == 0x110u && full[1].value == 0x44700000u);
    assert(full[2].offset == 0x111u && full[2].value == 0xc4070000u);
    assert(full[3].offset == 0x112u && full[3].value == 0x44070000u);
    assert(full[4].offset == 0x113u && full[4].value == 0x3f800000u);
    assert(full[5].offset == 0x114u && full[5].value == 0u);
    assert(full[6].offset == 0x090u && full[6].value == 0x80000000u);
    assert(full[7].offset == 0x091u && full[7].value == 0x04380780u);

    ps5_agc_register inset[PS5_VIEWPORT_SCISSOR_REGISTER_COUNT];
    const Ps5RectU32 viewport = {320u, 180u, 1280u, 720u};
    const Ps5RectU32 scissor = {400u, 220u, 1120u, 640u};
    assert(ps5_viewport_scissor_build(
        inset, &viewport, &scissor, 1920u, 1080u) == 0);
    assert(inset[0].value == 0x44200000u);
    assert(inset[1].value == 0x44700000u);
    assert(inset[2].value == 0xc3b40000u);
    assert(inset[3].value == 0x44070000u);
    assert(inset[6].value == 0x80dc0190u);
    assert(inset[7].value == 0x035c05f0u);

    Ps5RectU32 invalid = {1900u, 0u, 40u, 10u};
    assert(ps5_viewport_scissor_build(
        inset, &invalid, &full_rect, 1920u, 1080u) == -1);
    invalid = (Ps5RectU32){0u, 0u, 0u, 10u};
    assert(ps5_viewport_scissor_build(
        inset, &invalid, &full_rect, 1920u, 1080u) == -1);
    assert(ps5_viewport_scissor_build(
        NULL, &full_rect, &full_rect, 1920u, 1080u) == -1);
    return 0;
}
