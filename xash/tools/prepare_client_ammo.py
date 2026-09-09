#!/usr/bin/env python3
"""Generate the pinned client ammo source with opt-in immediate weapon cycling."""
import pathlib
import sys


def prepare(source):
    old = "\t\t\t\t\tgpActiveSel = wsp;\n\t\t\t\t\treturn;"
    new = """\t\t\t\t\tgpActiveSel = wsp;
                    // PS5: opt-in cycling without synthesizing an attack.
                    if ( CVAR_GET_FLOAT( "hud_fastswitch" ) != 0 )
                    {
                        ServerCmd( wsp->szName );
                        g_weaponselect = wsp->iId;
                        gpLastSel = wsp;
                        gpActiveSel = NULL;
                    }
\t\t\t\t\treturn;"""
    if source.count(old) != 2:
        raise ValueError("pinned Next/PrevWeapon selection sites changed")
    return source.replace(old, new)


if __name__ == "__main__":
    pathlib.Path(sys.argv[2]).write_text(prepare(pathlib.Path(sys.argv[1]).read_text()))
