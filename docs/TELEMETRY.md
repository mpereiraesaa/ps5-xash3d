# Runtime telemetry

Every launch writes structured `ps5log/1` records to the local trace beside
the save/config overlay. The optional TCP sink mirrors the same records for
live development, but local logging remains authoritative and works offline.

## Required records

| Record | Purpose |
| --- | --- |
| `LOG_BOOT` | Identifies title, firmware, build hash and monotonic start time. |
| `XASH_LOCAL_LOG` | Reports the selected writable log directory and fallback. |
| `LOG_FS_SINKS` | Shows whether local and optional network sinks are active. |
| `RESOURCE_*` | Tracks GPU resource creation, retirement and ownership. |
| `FRAME_*` | Correlates frame serials, command spans and presentation tokens. |
| `INPUT_*` | Counts ScePad samples, button edges and neutralization events. |
| `AUDIO_*` | Reports ring fill, underruns, output errors and worker teardown. |
| `ERROR` | Records a failed operation with subsystem and return code. |
| `LOG_SHUTDOWN` | Confirms ordered PRX/service teardown and final counters. |

Records are newline-delimited JSON. A run must have one boot record and one
shutdown record; a missing shutdown indicates an interrupted or crashed run.
Resource and frame records retain ownership tokens so a report can distinguish
a rendering failure from a cleanup failure.

## Local paths

```text
/download0/xash3d/valve/logs/xash3d.log
/download0/xash3d/valve/logs/xash3d-trace.log
```

`/temp0` is used only when `/download0` cannot be created. Each launch starts a
fresh trace file. When sharing a report, include both files from the same run
and remove credentials, private network paths and unrelated save data.

## Host checks

Run `make test` to exercise schema, ordering and ownership contracts. Run
`make audit` before publication to ensure no private artifacts or generated
runtime binaries entered the repository.
