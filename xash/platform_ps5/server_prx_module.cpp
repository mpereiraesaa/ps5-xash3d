/* HLSDK server PRX ABI probe. SPDX-License-Identifier: GPL-3.0-or-later. */
#include "extdll.h"
#include "enginecallback.h"

extern globalvars_t *gpGlobals;

extern "C" int PS5_ServerPrxEngineTableMask( void )
{
	int mask = 0;
	if( g_engfuncs.pfnCVarGetPointer ) mask |= 1;
	if( g_engfuncs.pfnCVarRegister ) mask |= 2;
	if( gpGlobals ) mask |= 4;
	return mask;
}

extern "C" int PS5_ServerPrxEngineTableSmoke( int step )
{
	switch( step )
	{
	case 1:
		return g_engfuncs.pfnCVarGetPointer( "sv_gravity" ) != NULL;
	case 2:
		return g_engfuncs.pfnCVarGetPointer( "developer" ) != NULL;
	default:
		return 0;
	}
}
