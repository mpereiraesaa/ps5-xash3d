#!/usr/bin/env python3
"""Generate the server client-command source with the PS5 ammo helper.

The HLSDK is pinned as a submodule. Keep the platform-specific command in a
generated translation unit so the submodule remains untouched and source drift
fails closed at build time.
"""
import pathlib
import sys


MARKER = '\telse if( FStrEq(pcmd, "give" ) )'
INSERT = '''\telse if( FStrEq( pcmd, "givecurrentammo" ) )
\t{
\t\t// PS5: give reserve ammo only for the weapon currently equipped.
\t\t// This fixed, local helper is intentionally independent of sv_cheats;
\t\t// it never accepts an arbitrary classname or grants unrelated items.
\t\tCBasePlayer *pPlayer = GetClassPtr( (CBasePlayer *)pev );
\t\tconst char *ammo = NULL;
\t\tif( pPlayer->m_pActiveItem )
\t\t{
\t\t\tconst char *weapon = pPlayer->m_pActiveItem->pszName();
\t\t\tif( FStrEq( weapon, "weapon_9mmhandgun" ) ) ammo = "ammo_9mmclip";
\t\t\telse if( FStrEq( weapon, "weapon_9mmAR" ) ) ammo = "ammo_9mmAR";
\t\t\telse if( FStrEq( weapon, "weapon_shotgun" ) ) ammo = "ammo_buckshot";
\t\t\telse if( FStrEq( weapon, "weapon_357" ) ) ammo = "ammo_357";
\t\t\telse if( FStrEq( weapon, "weapon_crossbow" ) ) ammo = "ammo_crossbow";
\t\t\telse if( FStrEq( weapon, "weapon_gauss" ) ) ammo = "ammo_gaussclip";
\t\t\telse if( FStrEq( weapon, "weapon_egon" ) ) ammo = "ammo_egonclip";
\t\t\telse if( FStrEq( weapon, "weapon_rpg" ) ) ammo = "ammo_rpgclip";
\t\t\t// Throwable/projectile weapons have no ammo_* entity in HL1;
\t\t\t// giving their weapon pickup invokes AddDuplicate and increments
\t\t\t// the corresponding reserve without changing the active weapon.
\t\t\telse if( FStrEq( weapon, "weapon_handgrenade" ) ) ammo = "weapon_handgrenade";
\t\t\telse if( FStrEq( weapon, "weapon_tripmine" ) ) ammo = "weapon_tripmine";
\t\t\telse if( FStrEq( weapon, "weapon_satchel" ) ) ammo = "weapon_satchel";
\t\t\telse if( FStrEq( weapon, "weapon_snark" ) ) ammo = "weapon_snark";
\t\t\telse if( FStrEq( weapon, "weapon_hornetgun" ) ) ammo = "weapon_hornetgun";
\t\t}
\t\tif( ammo )
\t\t\tpPlayer->GiveNamedItem( ammo );
\t}
'''


def prepare(source: str) -> str:
    if source.count(MARKER) != 1:
        raise ValueError("pinned ClientCommand give site changed")
    return source.replace(MARKER, INSERT + MARKER, 1)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(f"usage: {sys.argv[0]} INPUT OUTPUT")
    source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
    pathlib.Path(sys.argv[2]).write_text(prepare(source), encoding="utf-8")
