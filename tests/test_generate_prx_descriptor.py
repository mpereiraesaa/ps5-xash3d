#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        exports = directory / "exports.txt"
        source = directory / "module.c"
        version_script = directory / "module.map"
        exports.write_text("GiveFnptrsToDll\nGetEntityAPI2\n_ZN10CBaseDelay10DelayThinkEv\n", encoding="utf-8")
        result = subprocess.run(
            [sys.executable, "-B", str(ROOT / "xash/tools/generate_prx_descriptor.py"),
             "--module", "server", "--exports", str(exports),
             "--extra", "PS5_ServerPrxEngineTableMask",
             "--source", str(source), "--version-script", str(version_script)],
            text=True, capture_output=True, check=False)
        assert result.returncode == 0, result.stderr
        generated = source.read_text(encoding="utf-8")
        exports_map = version_script.read_text(encoding="utf-8")
        assert "__init_array_start" in generated
        assert "__init_array_end" in generated
        assert "ps5_prx_run_initializers( );" in generated
        assert "ps5_prx_run_finalizers( );" in generated
        assert "if( ps5_server_prx_started ) return 0;" in generated
        assert "if( !ps5_server_prx_started ) return 0;" in generated
        assert "return 3;" in generated
        assert "_ZN10CBaseDelay10DelayThinkEv;" in exports_map
        # Exercise the generated descriptor with the production resolver,
        # including a name surviving module unload/reload at a new address.
        fixture = directory / "roundtrip.c"
        fixture.write_text('''
#include "prx_loader_ps5.h"
#include <assert.h>
#include <string.h>
void GiveFnptrsToDll(void) {}
void GetEntityAPI2(void) {}
void PS5_ServerPrxEngineTableMask(void) {}
void _ZN10CBaseDelay10DelayThinkEv(void) {}
static void relocated_callback(void) {}
extern const ps5_prx_descriptor_t server_prx_exports;
int main(void) {
    ps5_prx_module_t module = {.handle=1, .descriptor=&server_prx_exports};
    const void *callback = (const void *)&_ZN10CBaseDelay10DelayThinkEv;
    const char *name = PS5_PrxNameForAddress(&module, callback);
    assert(name && !strcmp(name, "_ZN10CBaseDelay10DelayThinkEv"));
    char saved[128]; strcpy(saved, name);
    assert(PS5_PrxGetProc(&module, saved) == callback);
    assert(!PS5_PrxGetProc(&module, "missing"));
    module.handle = 0;
    assert(!PS5_PrxGetProc(&module, saved));
    struct { ps5_prx_descriptor_header_t header; ps5_prx_export_t entry; }
        replacement = {{PS5_PRX_DESCRIPTOR_MAGIC, 1, 1},
                       {saved, (const void *)&relocated_callback}};
    module.handle = 2;
    module.descriptor = (const ps5_prx_descriptor_t *)&replacement;
    assert(PS5_PrxGetProc(&module, saved) == (const void *)&relocated_callback);
    assert(!PS5_PrxNameForAddress(&module, callback));
    return 0;
}
''')
        binary = directory / "roundtrip"
        subprocess.run(["cc", "-std=gnu11", "-I" + str(ROOT / "xash/platform_ps5"),
                        str(source), str(fixture),
                        str(ROOT / "xash/platform_ps5/prx_loader_ps5.c"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary)], check=True)
        assert "PS5_ServerPrxEngineTableMask;" in exports_map
        assert "server_prx_exports;" in exports_map
        duplicate = subprocess.run(
            [sys.executable, "-B", str(ROOT / "xash/tools/generate_prx_descriptor.py"),
             "--module", "server", "--exports", str(exports),
             "--extra", "Probe", "--extra", "Probe",
             "--source", str(source), "--version-script", str(version_script)],
            text=True, capture_output=True, check=False)
        assert duplicate.returncode != 0
    print("PRX descriptor generator tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
