#ifndef PS5_PAD_AIM_H
#define PS5_PAD_AIM_H
#include <math.h>
#include <stdint.h>
/* Memoryless radial shaping, no temporal smoothing or acceleration. */
static inline void ps5_pad_aim(int16_t *yaw, int16_t *pitch, float dz, float power)
{
    float x=*yaw/32767.0f, y=*pitch/32767.0f, r=sqrtf(x*x+y*y);
    if (!isfinite(dz)) dz=.10f;
    if (!isfinite(power)) power=1.6f;
    dz=fminf(.4f,fmaxf(0,dz)); power=fminf(3,fmaxf(1,power));
    if (r<=dz || r==0) { *yaw=*pitch=0; return; }
    float magnitude=powf((fminf(r,1)-dz)/(1-dz),power);
    *yaw=(int16_t)lrintf(x/r*magnitude*32767);
    *pitch=(int16_t)lrintf(y/r*magnitude*32767);
}
#endif
