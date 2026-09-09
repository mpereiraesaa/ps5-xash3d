import pathlib
import runpy
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]
prepare = runpy.run_path(str(ROOT / "xash/tools/prepare_blood_effects.py"))["prepare"]

class BloodPatchTests(unittest.TestCase):
    def test_pinned_source_and_drift(self):
        source = (ROOT / "third_party/xash3d-fwgs/engine/client/cl_tent.c").read_text()
        result = prepare(source)
        self.assertEqual(result, prepare(source))
        self.assertIn('size *= sqrtf(blood_amount);', result)
        self.assertIn('if (splatter > 96) splatter = 96;', result)
        self.assertEqual(result.count('XASH_BLOOD_PRESENTATION schema=1'), 1)
        self.assertEqual(result.count('Cvar_Get("ps5_blood_amount"'), 2)
        tail = 'void GAME_EXPORT R_BreakModel('
        self.assertEqual(result[result.index(tail):], source[source.index(tail):])
        self.assertIn('pTemp->die = cl.time + COM_RandomFloat( 1.0f, 3.0f );', result)
        for bad in ('', source.replace('void CL_InitTempEnts( void )', 'changed'),
                    source.replace('colorIndex += COM_RandomLong( 1, 3 );', 'changed')):
            with self.assertRaises(ValueError):
                prepare(bad)

if __name__ == '__main__':
    unittest.main()
