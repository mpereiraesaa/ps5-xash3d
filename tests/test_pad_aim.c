#include "pad_aim.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
int main(void) {
    int16_t x=0,y=0; ps5_pad_aim(&x,&y,.1f,1.6f); assert(!x&&!y);
    x=2000;y=-2000;ps5_pad_aim(&x,&y,.1f,1.6f);assert(!x&&!y);
    x=32767;y=0;ps5_pad_aim(&x,&y,.1f,1.6f);assert(x==32767&&!y);
    x=-32768;y=0;ps5_pad_aim(&x,&y,.1f,1.6f);assert(x==-32767&&!y);
    x=32767;y=32767;ps5_pad_aim(&x,&y,.1f,1.6f);assert(x==y&&x>23000&&x<23200);
    int prev=0;
    for(int v=0;v<=32767;v++) { x=v;y=0;ps5_pad_aim(&x,&y,.1f,1.6f);assert(x>=prev&&!y);prev=x; }
    x=16384;y=0;ps5_pad_aim(&x,&y,.1f,1.6f);assert(x>0&&x<16384);
    int16_t positive=x;x=-16384;y=0;ps5_pad_aim(&x,&y,.1f,1.6f);assert(x==-positive);
    x=20000;y=10000;ps5_pad_aim(&x,&y,NAN,INFINITY);assert(x>0&&y>0&&abs(x-2*y)<=1);
    puts("PASS radial aim: neutral, deadzone, endpoints, diagonal, monotonicity, symmetry, invalid settings");
    return 0;
}
