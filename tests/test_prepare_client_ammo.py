"""Regression checks for the reproducible client-only weapon cycling patch."""
import pathlib
import runpy
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
prepare = runpy.run_path(str(ROOT / "xash/tools/prepare_client_ammo.py"))["prepare"]


class FastCycleTests(unittest.TestCase):
    def test_pinned_source_and_drift(self):
        source = (ROOT / "third_party/hlsdk-portable/cl_dll/ammo.cpp").read_text()
        result = prepare(source)
        self.assertEqual(result, prepare(source))
        for marker in ("ServerCmd( wsp->szName );", "g_weaponselect = wsp->iId;",
                       "gpLastSel = wsp;"):
            self.assertEqual(result.count(marker), 2)
        self.assertEqual(result.count('CVAR_GET_FLOAT( "hud_fastswitch" ) != 0'), 3)
        for changed in ("", source.replace("gpActiveSel = wsp;", "", 1)):
            with self.assertRaises(ValueError):
                prepare(changed)


if __name__ == "__main__":
    unittest.main()
