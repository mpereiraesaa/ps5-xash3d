/* GoldSrc client PRX ABI probe. SPDX-License-Identifier: GPL-3.0-or-later. */
#include "mathlib.h"
#include "hud_iface.h"

extern "C" int PS5_ClientPrxEngineTableMask( void )
{
	int mask = 0;
	if( gEngfuncs.pfnGetCvarPointer ) mask |= 1;
	if( gEngfuncs.pfnRegisterVariable ) mask |= 2;
	if( gEngfuncs.Con_Printf ) mask |= 4;
	if( gEngfuncs.pfnGetGameDirectory ) mask |= 8;
	return mask;
}

extern "C" int PS5_ClientPrxEngineTableSmoke( int step )
{
	switch( step )
	{
	case 1:
		return gEngfuncs.pfnGetCvarPointer( "developer" ) != 0;
	case 2:
		return gEngfuncs.pfnGetGameDirectory( ) != 0 &&
			gEngfuncs.pfnGetGameDirectory( )[0] != '\0';
	default:
		return 0;
	}
}
