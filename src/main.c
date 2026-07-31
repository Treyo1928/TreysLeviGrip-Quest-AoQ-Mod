/* liblevigrip-quest/src/main.c
 * Quest C port of TreysLeviGrip for AttackOnQuest 0.5.0
 *
 * Controls (default):
 *   Left A button      — toggle left levi grip
 *   Right thumbstick   — tap = flip right grip, hold = swap weapon
 *
 * With SwapControls = true:
 *   Right thumbstick   — tap = swap weapon, hold = flip right grip
 *
 * v1.1.1 multiplayer-rewrite notes:
 *  - Multiplayer swap now calls the game's own NetworkWeaponSwap.Swap(),
 *    which sends the SwapWeapon RPC — other players finally SEE your swap,
 *    and NetworkRightSword.SwordDisabled() runs exactly like vanilla.
 *  - All per-instance work is gated on photonView.IsMine. v1 ran the swap
 *    logic on EVERY player's NetworkWeaponSwap clone (touching other
 *    players' sword objects locally, and running during the room-teardown
 *    window after a kick — the prime suspect for the kick crash).
 *  - Weapon state is read live from rightSword.activeSelf instead of a
 *    static flag that went stale across deaths/scene reloads.
 *  - Hand anchors are null/liveness-checked before rotating, and the flip
 *    animation fraction is clamped (no more one-frame overshoot).
 *  - Config is cached (no per-flip disk reads).
 */

#include <android/log.h>
#include <stdio.h>
#include <stdint.h>
#include "../../AoQ-ModLoader-For-Quest/shared/inline-hook/inlineHook.h"
#include "../../AoQ-ModLoader-For-Quest/shared/utils/utils.h"
#include "../../AoQ-ModLoader-For-Quest/shared/aoqcore/aoq.h"
#include "../../AoQ-ModLoader-For-Quest/shared/modapi/modapi.h"

#define TAG "LeviGrip"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

#define MOD_SO_NAME "liblevigrip.so"

/* ── OVRInput button / controller constants (from dump.cs enums) ──── */
#define OVR_BTN_ONE            0x00000001   /* A (right) / X (left) */
#define OVR_BTN_PRIMARY_TS     0x00008000   /* PrimaryThumbstick = 32768 */
#define OVR_CTRL_LTOUCH        1
#define OVR_CTRL_RTOUCH        2

/* ── RVAs (AoQ 0.5.0, verified with Il2CppDumper) ───────────────────── */
#define RVA_OVRInput_GetDown        0xAF7CAC
#define RVA_OVRInput_Get            0xAF7A08
#define RVA_Transform_Rotate_Vec3   0xFD6DFC
#define RVA_Time_get_time           0xFD3EE0
#define RVA_GO_get_activeSelf       0xC74648
#define RVA_NWS_Swap                0x635910  /* NetworkWeaponSwap.Swap() — does the RPC */

/* Hook targets */
#define ADDR_WeaponSwap_Update          0x771800
#define ADDR_NetworkWeaponSwap_Update   0x63584C
#define ADDR_OVRCameraRig_UpdateAnchors 0x5AB168

/* ── Field offsets (from dump.cs) ───────────────────────────────────── */
/* WeaponSwap:        rightSword 0xC | flareGun 0x14 | timerCanvas 0x18 | player 0x1C */
#define WS_RIGHT_SWORD_OFF   0x0C
#define WS_FLARE_GUN_OFF     0x14
#define WS_TIMER_CANVAS_OFF  0x18
#define WS_PLAYER_OFF        0x1C
/* OVRCameraRig:      leftHandAnchor 0x1C | rightHandAnchor 0x20 (backing fields) */
#define RIG_LEFT_ANCHOR_OFF  0x1C
#define RIG_RIGHT_ANCHOR_OFF 0x20
/* Salute:            saluteRight1 0xC | saluteRight2 0xD */
#define SALUTE_RIGHT1_OFF    0x0C
#define SALUTE_RIGHT2_OFF    0x0D

/* ── Function pointer types ─────────────────────────────────────────── */
typedef int   (*OVRInput_GetDown_t)(int btn, int ctrl, void *mi);
typedef int   (*OVRInput_Get_t)    (int btn, int ctrl, void *mi);
typedef void  (*Transform_Rotate_t)(void *self, AoqVec3 eulers, void *mi);
typedef float (*Time_get_time_t)   (void *mi);
typedef int   (*GO_get_active_t)   (void *self);   /* instance — no mi */
typedef void  (*NWS_Swap_t)        (void *self);   /* instance — no mi */

static OVRInput_GetDown_t fn_OVRInput_GetDown = NULL;
static OVRInput_Get_t     fn_OVRInput_Get     = NULL;
static Transform_Rotate_t fn_Transform_Rotate = NULL;
static Time_get_time_t    fn_Time_get_time    = NULL;
static GO_get_active_t    fn_GO_get_active    = NULL;
static NWS_Swap_t         fn_NWS_Swap         = NULL;

/* ── Config (cached; refreshed on button events, never per frame) ───── */
static AoqCfgCache g_cfg = {0};

static float cfg_grip_offset_x  = 82.0f;
static float cfg_grip_offset_z  = 180.0f;
static float cfg_flip_duration  = 0.25f;
static float cfg_hold_duration  = 0.2f;
static int   cfg_swap_controls  = 0;   /* 0 = tap flips / hold swaps (default)
                                          1 = tap swaps / hold flips            */

static void refresh_config(void)
{
    if (aoq_cfg_refresh(&g_cfg, MOD_SO_NAME) != 0) return;
    cfg_grip_offset_x = aoq_cfg_flt (&g_cfg, "GripOffsetX",  82.0f);
    cfg_grip_offset_z = aoq_cfg_flt (&g_cfg, "GripOffsetZ",  180.0f);
    cfg_flip_duration = aoq_cfg_flt (&g_cfg, "FlipDuration", 0.25f);
    cfg_hold_duration = aoq_cfg_flt (&g_cfg, "HoldDuration", 0.2f);
    cfg_swap_controls = aoq_cfg_bool(&g_cfg, "SwapControls", 0);
}

/* ── Runtime helpers ─────────────────────────────────────────────────── */
static float get_time(void) { return fn_Time_get_time ? fn_Time_get_time(NULL) : 0.0f; }

static int go_active(void *go)
{
    return (fn_GO_get_active && aoq_alive(go)) ? fn_GO_get_active(go) : 0;
}

static AoqVec3 lerp3(AoqVec3 a, AoqVec3 b, float t)
{
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return (AoqVec3){
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

/* ── Grip / flip state ───────────────────────────────────────────────── */
static int   left_levi_grip  = 0, right_levi_grip = 0;
static int   left_flip_anim  = 0, right_flip_anim  = 0;
static float left_flip_start = 0, right_flip_start  = 0;

static void flip_right(void)
{
    /* Ignore retriggers mid-animation — same guard the left grip has.
     * Without it a quick double-tap restarted the lerp from a half-rotated
     * pose and the grip ended up at a wrong angle. */
    if (right_flip_anim) return;
    refresh_config();
    right_flip_anim  = 1;
    right_levi_grip  = !right_levi_grip;
    right_flip_start = get_time();
}

/* ── Single-player weapon swap (replicates WeaponSwap.Update body) ──── */
static void sp_swap(void *self)
{
    void *rightSword  = AOQ_FIELD(self, WS_RIGHT_SWORD_OFF,  void *);
    void *flareGun    = AOQ_FIELD(self, WS_FLARE_GUN_OFF,    void *);
    void *timerCanvas = AOQ_FIELD(self, WS_TIMER_CANVAS_OFF, void *);

    if (go_active(rightSword)) {
        aoq_go_set_active(rightSword,  0);
        aoq_go_set_active(timerCanvas, 0);
        aoq_go_set_active(flareGun,    1);
        /* Vanilla also clears the salute pose flags when holstering */
        void *player = AOQ_FIELD(self, WS_PLAYER_OFF, void *);
        void *salute = aoq_alive(player)
                     ? aoq_go_get_component_named(player, "", "Salute") : NULL;
        if (salute) {
            AOQ_FIELD(salute, SALUTE_RIGHT1_OFF, uint8_t) = 0;
            AOQ_FIELD(salute, SALUTE_RIGHT2_OFF, uint8_t) = 0;
        }
    } else {
        aoq_go_set_active(rightSword,  1);
        aoq_go_set_active(timerCanvas, 1);
        aoq_go_set_active(flareGun,    0);
    }
}

/* ── Multiplayer weapon swap — let the game do it (RPC + SwordDisabled) */
static void mp_swap(void *self)
{
    if (fn_NWS_Swap) fn_NWS_Swap(self);
}

/* ── Shared tap/hold dispatch for both Update hooks ─────────────────── */
static void handle_thumbstick(void *self, AoqTapHold *st, void (*swap)(void *))
{
    if (!fn_OVRInput_GetDown || !fn_OVRInput_Get) return;
    int down = fn_OVRInput_GetDown(OVR_BTN_PRIMARY_TS, OVR_CTRL_RTOUCH, NULL);
    int held = fn_OVRInput_Get   (OVR_BTN_PRIMARY_TS, OVR_CTRL_RTOUCH, NULL);
    if (down) refresh_config();

    int ev = aoq_tap_hold(st, down, held, get_time(), cfg_hold_duration);
    if (ev == AOQ_INPUT_NONE) return;

    /* Default: tap flips, hold swaps. SwapControls inverts that. */
    int do_swap = cfg_swap_controls ? (ev == AOQ_INPUT_TAP) : (ev == AOQ_INPUT_HOLD);
    if (do_swap) { LOGI("Swapping weapon");       swap(self); }
    else         { LOGI("Flipping right handle"); flip_right(); }
}

/* ── WeaponSwap.Update (single player, full replacement) ────────────── */
static AoqTapHold ws_th = {0};

MAKE_HOOK(WeaponSwap_Update, ADDR_WeaponSwap_Update, void, void *self)
{
    /* Original never called — it swaps instantly on thumbstick-down, which
     * conflicts with our tap/hold scheme. sp_swap() replicates its body. */
    handle_thumbstick(self, &ws_th, sp_swap);
}

/* ── NetworkWeaponSwap.Update (multiplayer, full replacement) ───────── */
static AoqTapHold nws_th = {0};

MAKE_HOOK(NetworkWeaponSwap_Update, ADDR_NetworkWeaponSwap_Update, void, void *self)
{
    /* Vanilla gates on photonView.IsMine — so do we. Without this the hook
     * ran for every player's clone and kept running on half-dead objects
     * during room teardown (kick/leave). */
    if (!aoq_is_mine(self)) return;
    handle_thumbstick(self, &nws_th, mp_swap);
}

/* ── OVRCameraRig.UpdateAnchors (postfix) ───────────────────────────── */

static void apply_grip(void *anchor, int grip, int *anim, float anim_start,
                       AoqVec3 offset, AoqVec3 anim_target, float now)
{
    if (!aoq_alive(anchor)) return;

    if (grip && !*anim) {
        fn_Transform_Rotate(anchor, offset, NULL);
    } else if (*anim) {
        AoqVec3 zero  = { 0.0f, 0.0f, 0.0f };
        AoqVec3 start = grip ? zero        : anim_target;
        AoqVec3 end   = grip ? anim_target : zero;
        float frac = cfg_flip_duration > 0.0f
                   ? (now - anim_start) / cfg_flip_duration : 1.0f;
        fn_Transform_Rotate(anchor, lerp3(start, end, frac), NULL);
        if (frac >= 1.0f) *anim = 0;
    }
}

MAKE_HOOK(OVRCameraRig_UpdateAnchors, ADDR_OVRCameraRig_UpdateAnchors,
          void, void *self, int updateEye, int updateHand)
{
    OVRCameraRig_UpdateAnchors(self, updateEye, updateHand);   /* run original first */

    if (!fn_Transform_Rotate || !fn_OVRInput_GetDown) return;

    void  *leftAnchor  = AOQ_FIELD(self, RIG_LEFT_ANCHOR_OFF,  void *);
    void  *rightAnchor = AOQ_FIELD(self, RIG_RIGHT_ANCHOR_OFF, void *);
    float  t           = get_time();

    /* Left A button (Button.One = 1, LTouch = 1) toggles left grip */
    if (fn_OVRInput_GetDown(OVR_BTN_ONE, OVR_CTRL_LTOUCH, NULL) && !left_flip_anim) {
        refresh_config();
        left_flip_anim  = 1;
        left_levi_grip  = !left_levi_grip;
        left_flip_start = t;
    }

    AoqVec3 offset = { cfg_grip_offset_x, 0.0f, cfg_grip_offset_z };
    /* Right animation path uses offset.z - 360 so the flip goes through -180 */
    AoqVec3 right_offset = { cfg_grip_offset_x, 0.0f, cfg_grip_offset_z - 360.0f };

    apply_grip(leftAnchor,  left_levi_grip,  &left_flip_anim,  left_flip_start,
               offset, offset, t);
    apply_grip(rightAnchor, right_levi_grip, &right_flip_anim, right_flip_start,
               offset, right_offset, t);
}

/* ── Entry point ─────────────────────────────────────────────────────── */
__attribute__((constructor)) void lib_main(void)
{
    LOGI("loading...");

    aoqmm_register(MOD_SO_NAME, "Treys Levi Grip", "1.1.2", "Treyo1928",
                   "Rotates hand anchors for a levi-style grip. "
                   "Left A = toggle left grip, right thumbstick tap = flip right, hold = swap weapon.");

    aoqmm_ensure_config(MOD_SO_NAME,
        "{\n"
        "  \"entries\": [\n"
        "    {\"key\":\"GripOffsetX\",\"type\":\"float\",\"value\":82.0,"
            "\"description\":\"X rotation of the grip offset in degrees.\"},\n"
        "    {\"key\":\"GripOffsetZ\",\"type\":\"float\",\"value\":180.0,"
            "\"description\":\"Z rotation of the grip offset in degrees.\"},\n"
        "    {\"key\":\"FlipDuration\",\"type\":\"float\",\"value\":0.25,"
            "\"description\":\"Duration of the flip animation in seconds.\"},\n"
        "    {\"key\":\"HoldDuration\",\"type\":\"float\",\"value\":0.2,"
            "\"description\":\"Seconds to hold right thumbstick to trigger the hold action.\"},\n"
        "    {\"key\":\"SwapControls\",\"type\":\"bool\",\"value\":false,"
            "\"description\":\"false = tap flips grip / hold swaps weapon (default). "
            "true = tap swaps weapon / hold flips grip.\"}\n"
        "  ]\n"
        "}\n"
    );

    aoq_init();
    refresh_config();

    fn_OVRInput_GetDown = (OVRInput_GetDown_t) getRealOffset(RVA_OVRInput_GetDown);
    fn_OVRInput_Get     = (OVRInput_Get_t)     getRealOffset(RVA_OVRInput_Get);
    fn_Transform_Rotate = (Transform_Rotate_t) getRealOffset(RVA_Transform_Rotate_Vec3);
    fn_Time_get_time    = (Time_get_time_t)    getRealOffset(RVA_Time_get_time);
    fn_GO_get_active    = (GO_get_active_t)    getRealOffset(RVA_GO_get_activeSelf);
    fn_NWS_Swap         = (NWS_Swap_t)         getRealOffset(RVA_NWS_Swap);

    INSTALL_HOOK(WeaponSwap_Update);
    INSTALL_HOOK(NetworkWeaponSwap_Update);
    INSTALL_HOOK(OVRCameraRig_UpdateAnchors);

    LOGI("hooks installed!");
}
