# Development workflow

## Stable product tree

`main` is the reviewable demo, not a scratch area. Reusable renderer and
platform contracts live in `src/` and `include/`; the PS5 adapter lives in
`native/`. Every change to those contracts must keep `make all` green.

Historical `stage-*` copies are not part of this repository. They duplicate
code, hide drift and make it difficult to know which implementation was
tested.

## Isolated experiments

Create each risky renderer experiment in a sibling Git worktree. This gives it
its own build directory and working tree while sharing the repository history:

```sh
git switch main
git worktree add ../ps5-xash3d-exp-<topic> -b exp/<topic>
cd ../ps5-xash3d-exp-<topic>
make all
```

The experiment imports the existing `src/`, `include/` and `native/` code from
its branch; it must not copy those directories into a numbered stage. Commit
small changes on `exp/<topic>`, validate them independently, and merge only the
reviewed commits into `main`.

When finished, first confirm that no uncommitted work remains, then remove the
worktree from the primary checkout:

```sh
git worktree list
git worktree remove ../ps5-xash3d-exp-<topic>
git branch -d exp/<topic>
```

## Required gates

Before merging or publishing:

```sh
make all
git diff --check
git status --short
```

Native changes additionally require a clean `make native-release`, artifact
hashing and one hardware launch with matching TCP telemetry. Longer soaks are
required when synchronization, ownership, memory layout or command emission
changes. Generated `.deps/`, `build/`, `dist/` and `release/` content remains
ignored and disposable.

Multi-file bundles are promoted with one exact transaction rather than
independent FTP overwrites:

```sh
python3 xash/tools/deploy_engine_bundle.py --host <console-ip> \
  --local-root dist/engine-boot/PPSA99996 \
  --module filesystem_stdio.prx --module server.prx --module menu.prx \
  --journal /absolute/private/deploy.jsonl --apply
```

The helper first disables ftpsrv's connection-local SELF transformation, then
uploads hidden, content-tagged staging files and streams every staged SELF,
PRX and regular asset back through FTP. Both size and SHA-256 must match the
exact local bytes before promotion. It retains exact prior paths until every
rename succeeds, rolls back a partial promotion, and deletes backups only after
commit. A cleanup failure
never rolls back an already committed live bundle; it is journaled and leaves
the explicit backup for manual recovery. Omit `--apply` to print the immutable
local/remote/hash plan without changing the console.

For a menu-to-c1a0 regression, build with `BSP_INPUT` pointing to
`valve/maps/c1a0.bsp`. Deploy the complete native stack:

```sh
python3 xash/tools/deploy_engine_bundle.py --host <console-ip> \
  --local-root dist/engine-boot/PPSA99996 \
  --module filesystem_stdio.prx --module server.prx --module menu.prx \
  --module client.prx --module ref_agc.prx --module libc.prx \
  --asset map.ps5bsp --asset model.ps5mdl \
  --journal /absolute/private/deploy.jsonl --apply
```

The preflight rejects incomplete AGC module/asset sets and requires the staged
BSP SHA-256 to occur in the plaintext renderer SELF's compiled metadata before
opening FTP. Raw readback then verifies every staged file. This prevents mixing
a renderer compiled against one proof bundle with another installed bundle.
It does not replace the paired runtime and visual validation. Preserve the
successful `build/` and `dist/` together; host tests no longer delete build
evidence. `make clean` remains the explicit cleanup operation.

The private laboratory may retain source notes and hardware evidence, but its
generated stage directories and title packages are never inputs to this repo.
