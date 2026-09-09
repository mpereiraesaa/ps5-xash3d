#include "ref_agc_studio_lighting.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    uint16_t gamma[1024];for(unsigned i=0;i<1024;++i) gamma[i]=i;
    RefAgcStudioLighting l={.ambient=32,.shade=128,.color={1,.5f,.25f},
        .direction={0,0,-1},.scale=1,.valid=1};
    float toward[3]={0,0,1},away[3]={0,0,-1},side[3]={1,0,0};
    uint32_t front,back,edge,flat,full,scaled;
    assert(!ref_agc_studio_vertex_light(&l,gamma,toward,0,&front));
    assert(!ref_agc_studio_vertex_light(&l,gamma,away,0,&back));
    assert(!ref_agc_studio_vertex_light(&l,gamma,side,0,&edge));
    assert((front&255)>(edge&255)&&(edge&255)>(back&255));
    assert((front>>24)==255&&((front>>8)&255)<(front&255));
    assert(!ref_agc_studio_vertex_light(&l,gamma,away,1,&flat));
    assert(!ref_agc_studio_vertex_light(&l,gamma,toward,1,&scaled)&&flat==scaled);
    assert(!ref_agc_studio_vertex_light(&l,gamma,away,4,&full));
    assert(full==0xff3f7fffu); /* fullbright ignores shade, retains engine tint */
    l.scale=2;float doubled[3]={0,0,2};
    assert(!ref_agc_studio_vertex_light(&l,gamma,doubled,0,&scaled)&&scaled==front);
    l.scale=1;
    for(unsigned i=0;i<1024;++i) gamma[i]=i/2;
    assert(!ref_agc_studio_vertex_light(&l,gamma,toward,0,&scaled));
    assert((scaled&255)<(front&255));
    gamma[640]=1024;assert(ref_agc_studio_vertex_light(&l,gamma,toward,0,&scaled)<0);
    gamma[640]=320;l.valid=0;assert(ref_agc_studio_vertex_light(&l,gamma,toward,0,&scaled)<0);
    l.valid=1;l.direction[0]=NAN;assert(ref_agc_studio_vertex_light(&l,gamma,toward,0,&scaled)<0);
    l.direction[0]=0;l.shade=INFINITY;assert(ref_agc_studio_vertex_light(&l,gamma,toward,0,&scaled)<0);
    puts("Studio directional/flat/fullbright/gamma/scale lighting tests passed");
}
