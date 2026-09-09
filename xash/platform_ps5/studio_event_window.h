#ifndef PS5_STUDIO_EVENT_WINDOW_H
#define PS5_STUDIO_EVENT_WINDOW_H
#include <math.h>
#include <stddef.h>

static inline int ps5_studio_event_span(int length, int offset, int count, size_t stride)
{
    return length>=0 && offset>=0 && count>=0 && offset<=length && stride &&
        (size_t)count<=((size_t)length-(size_t)offset)/stride;
}

static inline int ps5_studio_attachment(const float matrix[3][4], const float org[3], float out[3])
{
    float result[3];
    for(int k=0;k<3;++k) {
        result[k]=matrix[k][3];
        for(int j=0;j<3;++j) result[k]+=matrix[k][j]*org[j];
        if(!isfinite(result[k])) return -1;
    }
    for(int k=0;k<3;++k) out[k]=result[k];
    return 0;
}

/* Open start, closed end. A long stalled frame emits each event at most once. */
static inline int ps5_studio_event_due(float frame, float end, float delta,
                                      float period, int first, int looping)
{
    if (!isfinite(frame) || !isfinite(end) || !isfinite(delta) ||
        frame < 0 || delta < 0) return 0;
    float start = first ? -0.01f : end - delta;
    if (frame > start && frame <= end) return 1;
    return !first && looping && period > 0 && isfinite(period) &&
        frame <= period && frame-period > start && frame-period <= end;
}
#endif
