#include "recovery_gate.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    PS5RecoveryGate g={0};
    assert(PS5_RecoveryStep(&g,NAN,1)==0);
    assert(PS5_RecoveryStep(&g,1,0)==0);
    assert(PS5_RecoveryStep(&g,2,1)==PS5_RECOVERY_ARM);
    assert(PS5_RecoveryStep(&g,3,0)==0);
    assert(PS5_RecoveryStep(&g,10,1)==PS5_RECOVERY_ARM);
    assert(PS5_RecoveryStep(&g,24.9,1)==0);
    assert(PS5_RecoveryStep(&g,25,1)==PS5_RECOVERY_INJECT);
    assert(PS5_RecoveryStep(&g,25,1)==0); /* recursive clock / no duplicate */
    assert(PS5_RecoveryStep(&g,26,0)==PS5_RECOVERY_INACTIVE);
    assert(PS5_RecoveryStep(&g,35.9,0)==0);
    assert(PS5_RecoveryStep(&g,36,0)==PS5_RECOVERY_RELOAD);
    assert(PS5_RecoveryStep(&g,37,0)==0);
    assert(PS5_RecoveryStep(&g,38,1)==PS5_RECOVERY_ACTIVE);
    assert(PS5_RecoveryStep(&g,100,0)==0);
    assert(PS5_RecoveryStep(&g,200,1)==0);
    puts("recovery gate state-machine tests passed");
    return 0;
}
