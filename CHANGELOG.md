# Changelog

## v2.0.0

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
