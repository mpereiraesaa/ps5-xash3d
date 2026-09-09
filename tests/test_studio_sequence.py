#!/usr/bin/env python3
"""Exercise the actual adapter evaluator with deterministic bone math doubles.

This checks blend routing/validation, not upstream quaternion/RLE correctness.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "xash/platform_ps5/ref_agc_module.c").read_text()
helper = "static int RefAgcStudioSequence(" + source.split(
    "static int RefAgcStudioSequence(", 1)[1].split(
    "static int RefAgcCaptureStudioPose(", 1)[0]
prefix = r'''
#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char byte;
typedef float vec3_t[3];
typedef float vec4_t[4];
#define REF_AGC_LIVE_MAX_STUDIO_BONES 4
typedef struct { int parent, bonecontroller[6]; } mstudiobone_t;
typedef struct { int numbones, boneindex, numbonecontrollers; } studiohdr_t;
typedef struct { int numframes,numblends,motionbone,motiontype; } mstudioseqdesc_t;
typedef struct { float value; } mstudioanim_t;
static void R_StudioCalcBones(int frame,float fraction,mstudiobone_t *bone,
    mstudioanim_t *anim,const float *adj,vec3_t pos,vec4_t rot) {
    (void)bone; (void)adj;
    for(int i=0;i<3;i++) pos[i]=anim->value+frame+fraction;
    rot[0]=rot[1]=rot[2]=0; rot[3]=1;
}
static void R_StudioSlerpBones(int n,vec4_t *q,vec3_t *p,vec4_t *bq,vec3_t *bp,float s) {
    (void)q; (void)bq;
    s=fmaxf(0,fminf(1,s));
    for(int i=0;i<n;i++) for(int j=0;j<3;j++) p[i][j]=p[i][j]*(1-s)+bp[i][j]*s;
}
'''
main = r'''
int main(void) {
    struct { studiohdr_t h; mstudiobone_t bones[1]; } model={0};
    model.h.numbones=1; model.h.boneindex=offsetof(typeof(model),bones);
    model.bones[0].parent=-1;
    for(int k=0;k<6;k++) model.bones[0].bonecontroller[k]=-1;
    mstudioseqdesc_t seq={10,1,0,0};
    mstudioanim_t anim[4]={{0},{10},{20},{30}};
    vec3_t pos[4]; vec4_t rot[4]; float adj[1]={0};
    assert(!RefAgcStudioSequence(&model.h,&seq,anim,2.5f,adj,.25f,.75f,pos,rot));
    assert(fabsf(pos[0][0]-2.5f)<.0001f);
    seq.numblends=2;
    assert(!RefAgcStudioSequence(&model.h,&seq,anim,0,adj,.25f,.75f,pos,rot));
    assert(fabsf(pos[0][0]-2.5f)<.0001f);
    seq.numblends=4;
    assert(!RefAgcStudioSequence(&model.h,&seq,anim,0,adj,.25f,.75f,pos,rot));
    assert(fabsf(pos[0][0]-17.5f)<.0001f);
    seq.motiontype=1;
    assert(!RefAgcStudioSequence(&model.h,&seq,anim,0,adj,.25f,.75f,pos,rot));
    assert(pos[0][0]==0 && fabsf(pos[0][1]-17.5f)<.0001f);
    seq.numblends=3;
    assert(RefAgcStudioSequence(&model.h,&seq,anim,0,adj,0,0,pos,rot));
    seq.numblends=1; model.bones[0].parent=0;
    assert(RefAgcStudioSequence(&model.h,&seq,anim,0,adj,0,0,pos,rot));
    model.bones[0].parent=-1; model.bones[0].bonecontroller[0]=0;
    assert(RefAgcStudioSequence(&model.h,&seq,anim,0,adj,0,0,pos,rot));
    model.bones[0].bonecontroller[0]=-1;
    assert(RefAgcStudioSequence(&model.h,&seq,anim,NAN,adj,0,0,pos,rot));
    assert(RefAgcStudioSequence(&model.h,&seq,NULL,0,adj,0,0,pos,rot));
    puts("production Studio sequence blend routing tests passed");
}
'''
with tempfile.TemporaryDirectory(prefix="studio-sequence-") as name:
    path = Path(name)
    (path / "test.c").write_text(prefix + helper + main)
    subprocess.run([os.environ.get("CC", "cc"), "-std=gnu11", "-Wall", "-Wextra",
                    "-Werror", str(path / "test.c"), "-lm", "-o", str(path / "test")], check=True)
    subprocess.run([str(path / "test")], check=True)
