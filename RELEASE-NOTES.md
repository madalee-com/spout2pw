# Spout2PW 0.3.0

## What's new

### Build system improvements
- **Spout2 sources are now vendored** directly in `subprojects/spout2/` — zero external fetch required, builds work fully offline and CI is reliable.
- **spoutdxtoc is now spout2dx**, statically linked directly into `spout2pw.exe`. No separate DLL needed in the package.
- Package layout simplified: `spout2pw-dlls/x86_64-windows/` holds both `spout2pw.exe` and `spout2pw.dll`.

### Debugging
- **New debug logging system.** Set `SPOUT2PW_DEBUG=1` or pass `-debug` to `spout2pw.sh` to get detailed logs. Logs are completely silent by default.
- Debug output is gated behind the flag — no more noisy stderr spam during normal use.

### Fixes
- **Spurious "Error initializing UNIX library (c0000135)" popup suppressed.** ServiceMain now treats a late unixlib registration as non-fatal, so Proton/Wine startups are clean.
- Fixed missing `-lm` link flag for the math library on some toolchains.
- Removed dead code and unused functions from the source tree.
- Cleaned up package.sh (removed stale `spoutdxtoc.dll` references).

### DXVK logging
- DXVK info-level startup logs are now silenced by default (`DXVK_LOG_LEVEL=none` in the Wine prefix). Note: the underlying cause of DXVK spam was in Wine/Proton/Warudo, not spout2pw — this is a safety net.

### Documentation
- Wiki migrated to the madalee-com fork: https://github.com/madalee-com/spout2pw/wiki
- Updated docs for the new debug system and c0000135 behavior.

## Upgrade notes
- Same installation method as before: extract the tarball and point Steam/umu at `spout2pw.sh`.
- If you were using the old `spoutdxtoc.dll`, it's gone — nothing to migrate, spout2dx is built in.
