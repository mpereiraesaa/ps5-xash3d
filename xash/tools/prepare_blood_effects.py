#!/usr/bin/env python3
"""Generate cl_tent with a bounded, reversible PS5 blood presentation setting."""
import pathlib
import sys


def prepare(source):
    start = source.index("void GAME_EXPORT R_BloodSprite(")
    end = source.index("void GAME_EXPORT R_BreakModel(", start)
    body = source[start:end]
    anchor = "\tcolorIndex += COM_RandomLong( 1, 3 );"
    splatter = "\t\tint splatter = size + ( COM_RandomLong( 1, 8 ) + COM_RandomLong( 1, 8 ));"
    if body.count(anchor) != 1 or body.count(splatter) != 1:
        raise ValueError("pinned R_BloodSprite sites changed")
    body = body.replace(anchor, '''\t// PS5 presentation only: 1 restores the original blood effect.
    float blood_amount = Cvar_Get("ps5_blood_amount", "1.5", FCVAR_ARCHIVE,
        "Blood presentation multiplier (1 original, max 3); no damage changes")->value;
    if (!(blood_amount >= 1.0f && blood_amount <= 3.0f)) blood_amount = 1.0f;
    float original_size = size;
    size *= sqrtf(blood_amount);
    Con_Reportf("XASH_BLOOD_PRESENTATION schema=1 amount=%.2f size=%.2f original_size=%.2f\\n",
        blood_amount, size, original_size);
''' + anchor)
    body = body.replace(splatter, '''\t\tint splatter = (int)((int)(original_size +
            (COM_RandomLong(1, 8) + COM_RandomLong(1, 8))) * blood_amount);
        if (splatter > 96) splatter = 96;''')
    result = source[:start] + body + source[end:]
    init = "void CL_InitTempEnts( void )\n{"
    if result.count(init) != 1:
        raise ValueError("pinned temp entity initialization changed")
    return result.replace(init, init + '\n    Cvar_Get("ps5_blood_amount", "1.5", FCVAR_ARCHIVE, "Blood presentation multiplier (1 original, max 3)");')


if __name__ == "__main__":
    pathlib.Path(sys.argv[2]).write_text(prepare(pathlib.Path(sys.argv[1]).read_text()))
