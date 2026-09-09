#include "studio_controller_lerp.h"
#include <assert.h>
#include <stdio.h>
#define NEAR(a,b) assert(fabsf((a)-(b)) < 0.0001f)
int main(void)
{
    NEAR(ps5_studio_controller_fraction(1.05,1.0f,0.9f,1),0.5f);
    NEAR(ps5_studio_controller_fraction(2,1.0f,0.9f,1),2.0f);
    NEAR(ps5_studio_controller_fraction(0.95,1.0f,0.9f,1),-0.5f);
    NEAR(ps5_studio_controller_fraction(1.05,1.0f,1.0f,1),1.0f);
    NEAR(ps5_studio_controller_fraction(1.05,1.0f,0.9f,0),1.0f);
    assert(isnan(ps5_studio_controller_fraction(NAN,1,0,1)));
    NEAR(ps5_studio_controller(255,0,0.5f,-30,30,0),0);
    NEAR(ps5_studio_controller(255,0,2,-30,30,0),30);
    NEAR(ps5_studio_controller(255,0,-1,-30,30,0),-30);
    NEAR(ps5_studio_controller(2,254,0.5f,0,360,1),0);
    NEAR(ps5_studio_controller(254,2,0.5f,0,360,1),0);
    NEAR(ps5_studio_controller(128,0,0.5f,10,0,1),100);
    NEAR(ps5_studio_blend(255,0,0.5f),0.5f);
    NEAR(ps5_studio_blend(255,0,2),2);
    assert(isnan(ps5_studio_controller(0,0,NAN,0,1,0)));
    puts("Studio controller interpolation tests passed");
    return 0;
}
