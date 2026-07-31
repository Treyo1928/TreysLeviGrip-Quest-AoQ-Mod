# Changelog

## v1.1.2

### Fixed
- Right-grip flip can no longer be retriggered mid-animation (a fast double-tap
  restarted the lerp from a half-rotated pose, leaving the grip at a wrong
  angle) — now guarded the same way as the left grip.
- Thumbstick handling bails out safely if the OVRInput functions failed to
  resolve, and a release exactly at the hold threshold now counts as a hold
  instead of firing a tap (via the shared `aoq_tap_hold` fix).

### Inherited from the shared libraries (rebuild against modloader v1.3.0)
- Hook engine: trampoline instruction-cache flush (fixes random startup
  crashes on some devices), relocation fixes, and failed hook installs now
  log an error instead of silently doing nothing.
- aoqcore: per-frame helpers are destroyed-object-safe; lazy init can no
  longer be permanently poisoned by an early call.
- Config layer: atomic saves, truncation guard, JSON depth cap.

Use with the AoQ-Modloader **v1.3.0** patched APK. Full details in its
`CHANGELOG.md`.

> **Version note:** if your in-game mod list previously showed "2.0.0", that
> string was an error in an old build — there was never a 2.x release. The
> latest actual release before this one is v1.1.1 below.

## v1.1.1

Multiplayer fixes and a crash fix.

### Fixed
- **Crash when kicked from (or leaving) a multiplayer room.** The multiplayer
  weapon-swap hook ran on every player's object every frame, including the brief
  room-teardown window after a kick, where it touched half-destroyed objects.
  It's now gated on `photonView.IsMine` and every object is liveness-checked
  before use.
- **Other players couldn't see your weapon swap.** The mod swapped weapons with
  local calls; it now goes through the game's own networked swap, so the change
  replicates to everyone (and runs the game's sword-disable logic exactly like
  vanilla).
- Weapon state no longer desyncs after dying or restarting a level — it's read
  live from the game instead of tracked in a flag that survived scene reloads.
- Single-player swap now also clears the salute pose, matching vanilla.
- The grip flip no longer overshoots by a frame at the end of its animation.

### Changed
- Config is cached, so flipping/swapping no longer reads the config file from
  disk on the input path.
- Rebuilt on the shared `aoqcore` library.
- Requires the AoQ-Modloader **v1.1.0** patched APK.
- Push mods to `/sdcard/DCIM/AoQMods/mods/`.

### Controls (unchanged)
- Left A: toggle left grip · Right thumbstick tap: flip right grip · hold: swap
  weapon. `SwapControls` config inverts tap/hold.
