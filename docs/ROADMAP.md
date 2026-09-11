# Roadmap

The public Half-Life 1 Steam release is **PLAYABLE**. Future work is
compatibility maintenance and additional GoldSrc content, not a second renderer
rewrite.

## Current priorities

- Collect long-play soak reports and fix reproducible crashes or visual/audio
  regressions.
- Keep local logs useful and privacy-safe for community bug reports.
- Validate optional `valve_hd`/`*_hd` content without changing the default asset
  path.
- Improve platform diagnostics when a PS5 firmware update changes an import or
  return code.

## Additional GoldSrc titles

The engine supports the GoldSrc family, but each title still needs compatible
game data and a matching `client.prx` compiled and packaged for that title.
Those per-title clients are separate build inputs; they do not change the
Half-Life release identity `PPSA99996`.

## Contributor workflow

Keep changes small, add a host regression for each contract change, and run:

```sh
make test
make audit
```

For console changes, include firmware, artifact hash, map and the paired local
logs in the pull request. Do not commit SDK files, dumps, game assets or built
SELF/PRX files.
