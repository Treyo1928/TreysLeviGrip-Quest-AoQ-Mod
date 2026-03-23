# Treys Levi Grip — AoQ Quest Mod

A Quest C mod for **Attack on Quest (AoQ) 0.5.0** that rotates your hand anchors to replicate the levi-style weapon grip. Ported from the PCVR BepInEx version.

Built on the [AoQ-ModLoader-For-Quest](https://github.com/Treyo1928/AoQ-Modloader) framework.

---

## Controls

| Input | Action |
|---|---|
| Left A button | Toggle left hand levi grip |
| Right thumbstick tap | Flip right hand grip |
| Right thumbstick hold (0.2s) | Swap weapon (sword ↔ flare gun) |

---

## Config

All values are editable in-game via **Mods → Configure Mods → Treys Levi Grip**.

| Key | Type | Default | Description |
|---|---|---|---|
| `GripOffsetX` | float | `82.0` | X rotation of the grip offset in degrees |
| `GripOffsetZ` | float | `180.0` | Z rotation of the grip offset in degrees |
| `FlipDuration` | float | `0.25` | Duration of the flip animation in seconds |
| `HoldDuration` | float | `0.2` | Seconds to hold the right thumbstick before swapping weapon |

Config is stored at:
```
/sdcard/Android/data/com.AoQ.AttackOnQuest/files/modconfigs/levigrip.json
```

---

## Install

**Prerequisites:** The game must be running the patched APK from [AoQ-ModLoader-For-Quest](https://github.com/Treyo1928/AoQ-Modloader).

1. Download `liblevigrip.so` from the [Releases](../../releases) page.
2. Push it to your headset:
```bash
adb push liblevigrip.so /sdcard/Android/data/com.AoQ.AttackOnQuest/files/mods/
```
3. Restart the game. The mod will appear in the **Mods** panel in the main menu.

---

## Build from Source

**Prerequisites:** Android NDK r26d

```bash
git clone <this-repo>
cd liblevigrip-quest
bash build.sh
```

To build and push to a connected headset in one step:
```bash
bash build.sh --push
```

To also open a filtered logcat session after pushing:
```bash
bash build.sh --push --logs
```

The output binary is at `libs/armeabi-v7a/liblevigrip.so`.

---

## Verify It's Working

```bash
adb logcat -s LeviGrip,AoQModManager,QuestHook
```

You should see:
```
LeviGrip: loading...
LeviGrip: hooks installed!
```
