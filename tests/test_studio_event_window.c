#include "studio_event_window.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    assert(ps5_studio_event_span(100,20,4,20));
    assert(!ps5_studio_event_span(99,20,4,20));
    assert(!ps5_studio_event_span(100,-1,0,20));
    assert(!ps5_studio_event_span(100,20,-1,20));
    assert(!ps5_studio_event_span(100,20,2147483647,20));
    float m[3][4]={{0,-1,0,10},{1,0,0,20},{0,0,1,30}};
    float org[3]={1,2,3},out[3]={0};
    assert(!ps5_studio_attachment(m,org,out));
    assert(out[0]==8 && out[1]==21 && out[2]==33);
    m[2][3]=NAN;
    assert(ps5_studio_attachment(m,org,out)==-1);
    assert(out[0]==8 && out[1]==21 && out[2]==33);
    assert(ps5_studio_event_due(0,0,0,9,1,0));
    assert(!ps5_studio_event_due(0,0,0,9,0,0));
    assert(ps5_studio_event_due(3,3,1,9,0,0));
    assert(!ps5_studio_event_due(2,3,1,9,0,0));
    assert(!ps5_studio_event_due(4,3,1,9,0,0));
    assert(ps5_studio_event_due(8,.5f,2,9,0,1));
    assert(ps5_studio_event_due(0,.5f,2,9,0,1));
    assert(!ps5_studio_event_due(8,.5f,2,9,0,0));
    assert(!ps5_studio_event_due(8,.5f,2,9,1,1));
    for(int i=0;i<9;i++) assert(ps5_studio_event_due(i,.5f,20,9,0,1));
    assert(!ps5_studio_event_due(1,NAN,1,9,0,1));
    assert(!ps5_studio_event_due(1,2,-1,9,0,1));
    puts("Studio event windows passed: boundaries, first frame, wrap, stall, invalid time");
    return 0;
}
