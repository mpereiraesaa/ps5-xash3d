"""Regression checks for the contextual current-ammo server command."""
import pathlib
import runpy
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
prepare = runpy.run_path(str(ROOT / "xash/tools/prepare_server_client.py"))["prepare"]
SOURCE = (ROOT / "third_party/hlsdk-portable/dlls/client.cpp").read_text()


class CurrentAmmoTests(unittest.TestCase):
    def test_injection_is_deterministic_and_contextual(self):
        result = prepare(SOURCE)
        self.assertEqual(result, prepare(SOURCE))
        self.assertEqual(result.count('FStrEq( pcmd, "givecurrentammo" )'), 1)
        self.assertIn("independent of sv_cheats", result)
        for weapon, ammo in (
            ("weapon_9mmhandgun", "ammo_9mmclip"),
            ("weapon_9mmAR", "ammo_9mmAR"),
            ("weapon_shotgun", "ammo_buckshot"),
            ("weapon_357", "ammo_357"),
            ("weapon_crossbow", "ammo_crossbow"),
            ("weapon_gauss", "ammo_gaussclip"),
            ("weapon_egon", "ammo_egonclip"),
            ("weapon_rpg", "ammo_rpgclip"),
            ("weapon_handgrenade", "weapon_handgrenade"),
            ("weapon_tripmine", "weapon_tripmine"),
            ("weapon_satchel", "weapon_satchel"),
            ("weapon_snark", "weapon_snark"),
            ("weapon_hornetgun", "weapon_hornetgun"),
        ):
            self.assertIn(f'FStrEq( weapon, "{weapon}" )', result)
            self.assertIn(f'ammo = "{ammo}"', result)
        self.assertNotIn('FStrEq( weapon, "weapon_crowbar" )', result)
        for unsupported in ("weapon_crowbar",):
            self.assertNotIn(f'ammo = "{unsupported}"', result)
        self.assertNotIn("impulse 101", result)

    def test_source_drift_fails_closed(self):
        with self.assertRaises(ValueError):
            prepare(SOURCE.replace('\telse if( FStrEq(pcmd, "give" ) )', "", 1))


if __name__ == "__main__":
    unittest.main()
