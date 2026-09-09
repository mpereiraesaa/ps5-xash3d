/* Diagnostic-only owner-thread state machine; no engine pointers retained. */
#ifndef PS5_RECOVERY_GATE_H
#define PS5_RECOVERY_GATE_H
#include <math.h>
typedef struct { int stage; double since; } PS5RecoveryGate;
enum { PS5_RECOVERY_NONE, PS5_RECOVERY_ARM, PS5_RECOVERY_INJECT,
       PS5_RECOVERY_INACTIVE, PS5_RECOVERY_RELOAD, PS5_RECOVERY_ACTIVE };
static inline int PS5_RecoveryStep(PS5RecoveryGate *g, double now, int active)
{
    if (!g || !isfinite(now) || now < 0.0) return PS5_RECOVERY_NONE;
    switch (g->stage) {
    case 0:
        if (active) { g->stage=1; g->since=now; return PS5_RECOVERY_ARM; }
        break;
    case 1:
        if (!active) { g->stage=0; break; }
        if (now-g->since >= 15.0) { g->stage=2; return PS5_RECOVERY_INJECT; }
        break;
    case 2:
        if (!active) { g->stage=3; g->since=now; return PS5_RECOVERY_INACTIVE; }
        break;
    case 3:
        if (!active && now-g->since >= 10.0) {
            g->stage=4; return PS5_RECOVERY_RELOAD;
        }
        break;
    case 4:
        if (active) { g->stage=5; return PS5_RECOVERY_ACTIVE; }
        break;
    default: break;
    }
    return PS5_RECOVERY_NONE;
}
#endif
