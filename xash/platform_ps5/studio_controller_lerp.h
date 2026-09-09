/* SPDX-License-Identifier: GPL-3.0-or-later
 * Scalar interpolation matching pinned ref/gl/gl_studio.c. */
#ifndef PS5_STUDIO_CONTROLLER_LERP_H
#define PS5_STUDIO_CONTROLLER_LERP_H
#include <math.h>
#include <stdint.h>
static inline float ps5_studio_controller_fraction(double time, float animtime,
                                                   float previous, int interpolate)
{
    if (!isfinite(time) || !isfinite(animtime) || !isfinite(previous)) return NAN;
    if (!interpolate || animtime < previous + 0.01f) return 1.0f;
    /* Reference permits extrapolation up to 2, including negative fractions. */
    float d = (float)((time - animtime) / 0.1f);
    return d > 2.0f ? 2.0f : d;
}
static inline float ps5_studio_blend(uint8_t current, uint8_t previous, float d)
{
    return (current*d + previous*(1.0f-d))/255.0f;
}
static inline float ps5_studio_controller(uint8_t current, uint8_t previous,
                                         float d, float start, float end, int loop)
{
    if (!isfinite(d) || !isfinite(start) || !isfinite(end)) return NAN;
    if (loop) {
        int a=current, b=previous;
        if (a-b > 128 || b-a > 128) {
            a=(a+128)%256; b=(b+128)%256;
            return (a*d+b*(1.0f-d)-128)*(360.0f/256.0f)+start;
        }
        return (a*d+b*(1.0f-d))*(360.0f/256.0f)+start;
    }
    float t=ps5_studio_blend(current,previous,d);
    t=fmaxf(0.0f,fminf(1.0f,t));
    return (1.0f-t)*start+t*end;
}
#endif
