#!/usr/bin/env python3
"""Execute the production diagnostic scheduler with host engine stubs."""
import pathlib
import subprocess
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
source = (ROOT / "xash/platform_ps5/sys_ps5.c").read_text()
probe = source.split("static void PS5_HudProbeTick( double now )", 1)[1].split("\n#endif", 1)[0]
stub = r'''
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef int qboolean;
enum { false, true, ca_active, FCVAR_ARCHIVE = 16, FFADE_IN = 0, FFADE_MODULATE = 2 };
typedef struct { char *string; int flags; } convar_t;
typedef struct { int fadeFlags, fader, fadeg, fadeb, fadealpha;
    float fadeSpeed; double fadeReset, fadeEnd; } screenfade_t;
static struct { int state; } cls;
static struct { double time; } cl;
static struct { screenfade_t fade; } clgame;
static char value[64] = "2.0";
static convar_t variable = { value, FCVAR_ARCHIVE | 32 };
static int missing, records, errors, labels, observed_stage = -1;
static double clock_now;
static void PS5_HudProbeTick(double);
#define ClearBits(a,b) ((a) &= ~(b))
#define Q_strncpy(a,b,c) snprintf((a),(c),"%s",(b))
static convar_t *Cvar_FindVar(const char *name) {
    assert(!strcmp(name,"con_fontrender")); return missing ? NULL : &variable;
}
static void Cvar_DirectSet(convar_t *v, const char *s) {
    snprintf(value,sizeof(value),"%s",s); PS5_HudProbeTick(clock_now);
}
static void Cvar_DirectSetValue(convar_t *v, float n) {
    snprintf(value,sizeof(value),"%.0f",n); PS5_HudProbeTick(clock_now);
}
static void Con_Printf(const char *fmt, ...) {
    va_list ap; va_start(ap,fmt);
    if (strstr(fmt,"PROBE_STAGE")) { observed_stage=va_arg(ap,int); ++records; }
    if (strstr(fmt,"PROBE_ERROR")) ++errors;
    va_end(ap); PS5_HudProbeTick(clock_now);
}
static void Con_NPrintf(int idx, const char *fmt, ...) { ++labels; }
'''
main = r'''
static void tick(double now) { clock_now=now; cl.time=now; PS5_HudProbeTick(now); }
int main(int argc, char **argv) {
    missing = argc > 1;
    tick(1); assert(!records && !labels);
    cls.state=ca_active;
    tick(100); tick(109); assert(!records && !labels);
    tick(110);
    if (missing) { assert(errors==1 && !records); tick(1000); assert(errors==1); return 0; }
    assert(records==1 && observed_stage==0 && !strcmp(value,"0"));
    assert(!(variable.flags & FCVAR_ARCHIVE));
    tick(124); assert(records==1);
    tick(125); assert(observed_stage==1 && !strcmp(value,"1"));
    tick(1000); assert(observed_stage==2 && !strcmp(value,"2")); /* no skipped stage */
    tick(1014); assert(observed_stage==2);
    clgame.fade.fader=17;
    tick(1015); assert(observed_stage==3 && !strcmp(value,"2.0"));
    assert(variable.flags==(FCVAR_ARCHIVE|32));
    assert(clgame.fade.fadeFlags==FFADE_IN && clgame.fade.fadealpha==192);
    assert(clgame.fade.fadeEnd==1025 && clgame.fade.fadeReset==1018);
    tick(1030); assert(observed_stage==4 && (clgame.fade.fadeFlags & FFADE_MODULATE));
    tick(1045); assert(observed_stage==5 && clgame.fade.fader==17);
    tick(1060); assert(observed_stage==6 && records==7);
    int old_labels=labels; tick(2000); assert(records==7 && labels==old_labels);
    assert(!errors); return 0;
}
'''
with tempfile.TemporaryDirectory(prefix="hud-probe-test-") as temp:
    test = pathlib.Path(temp) / "probe.c"
    test.write_text(stub + "static void PS5_HudProbeTick(double now)" + probe + main)
    exe = str(pathlib.Path(temp) / "probe")
    subprocess.run(["cc", "-std=c99", "-fsanitize=address,undefined", str(test), "-o", exe], check=True)
    subprocess.run([exe], check=True)
    subprocess.run([exe, "missing"], check=True)
print("HUD probe scheduling, recursion, restoration and failure tests passed")
