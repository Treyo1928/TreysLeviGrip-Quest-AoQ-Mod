/* liblevigrip-quest/src/main.c
 * Quest C port of TreysLeviGrip for AttackOnQuest 0.5.0
 *
 * Controls:
 *   Left A button      — toggle left levi grip
 *   Right thumbstick   — tap = flip right grip, hold = swap weapon
 */

#include <android/log.h>
#include "../../AoQ-ModLoader-For-Quest/shared/inline-hook/inlineHook.h"
#include "../../AoQ-ModLoader-For-Quest/shared/utils/utils.h"
#include "../../AoQ-ModLoader-For-Quest/shared/modapi/modapi.h"
#include "../../AoQ-ModLoader-For-Quest/modmanager/modconfig.h"

#define TAG "LeviGrip"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

/* ── Unity types ────────────────────────────────────────────────────── */
typedef struct { float x, y, z; } Vector3;

/* ── OVRInput button / controller constants (from dump.cs enums) ──── */
#define OVR_BTN_ONE            0x00000001   /* A (right) / X (left) */
#define OVR_BTN_PRIMARY_TS     0x00008000   /* PrimaryThumbstick = 32768 */
#define OVR_CTRL_LTOUCH        1
#define OVR_CTRL_RTOUCH        2

/* ── Function pointer types ─────────────────────────────────────────── */
typedef int   (*OVRInput_GetDown_t)(int btn, int ctrl, void *mi);
typedef int   (*OVRInput_Get_t)    (int btn, int ctrl, void *mi);
typedef void  (*Transform_Rotate_t)(void *self, Vector3 eulers, void *mi);
typedef float (*Time_get_time_t)   (void *mi);
typedef void  (*SetActive_t)       (void *self, int value);   /* instance — no mi */
typedef void  (*SwordDisabled_t)   (void *self);              /* instance — no mi */

static OVRInput_GetDown_t fn_OVRInput_GetDown = NULL;
static OVRInput_Get_t     fn_OVRInput_Get     = NULL;
static Transform_Rotate_t fn_Transform_Rotate = NULL;
static Time_get_time_t    fn_Time_get_time    = NULL;
static SetActive_t        fn_SetActive        = NULL;
static SwordDisabled_t    fn_SwordDisabled    = NULL;

/* ── Config (cached; refreshed on each flip / weapon swap) ─────────── */
static float cfg_grip_offset_x = 82.0f;
static float cfg_grip_offset_z = 180.0f;
static float cfg_flip_duration = 0.25f;
static float cfg_hold_duration = 0.2f;

static void reload_config(void)
{
    ModConfig cfg;
    if (load_config("liblevigrip.so", &cfg) != 0) return;
    ModCfgEntry *e;
    if ((e = get_entry(&cfg, "GripOffsetX")))  cfg_grip_offset_x = (float)e->value_num;
    if ((e = get_entry(&cfg, "GripOffsetZ")))  cfg_grip_offset_z = (float)e->value_num;
    if ((e = get_entry(&cfg, "FlipDuration"))) cfg_flip_duration = (float)e->value_num;
    if ((e = get_entry(&cfg, "HoldDuration"))) cfg_hold_duration = (float)e->value_num;
}

/* ── Runtime helpers ─────────────────────────────────────────────────── */
static float get_time(void) { return fn_Time_get_time ? fn_Time_get_time(NULL) : 0.0f; }

static void set_active(void *obj, int v)
{
    if (fn_SetActive && obj) fn_SetActive(obj, v);
}

static Vector3 lerp3(Vector3 a, Vector3 b, float t)
{
    return (Vector3){
        a.x + (b.x - a.x) * t,
        a.y + (b.y - a.y) * t,
        a.z + (b.z - a.z) * t
    };
}

/* ── Grip / flip state ───────────────────────────────────────────────── */
static int   left_levi_grip  = 0, right_levi_grip = 0;
static int   left_flip_anim  = 0, right_flip_anim  = 0;
static float left_flip_start = 0, right_flip_start  = 0;

/* Track which weapon is active (1 = sword, 0 = flare gun).
   Initialised to 1 — game default on level start.               */
static int ws_sword_active  = 1;   /* WeaponSwap instance */
static int nws_sword_active = 1;   /* NetworkWeaponSwap instance */

static void flip_right(void)
{
    reload_config();
    right_flip_anim  = 1;
    right_levi_grip  = !right_levi_grip;
    right_flip_start = get_time();
}

/* ── WeaponSwap.Update (full replacement) ───────────────────────────── */
/*   Fields: rightSword 0xC | leftSword 0x10 | flareGun 0x14
 *           timerCanvas 0x18 | player 0x1C                             */
static float ws_press_start = 0;
static int   ws_pressing    = 0;

MAKE_HOOK(WeaponSwap_Update, 0x771800, void, void *self)
{
    /* Original never called — full replacement */
    float t = get_time();

    if (fn_OVRInput_GetDown(OVR_BTN_PRIMARY_TS, OVR_CTRL_RTOUCH, NULL)) {
        ws_press_start = t;
        ws_pressing    = 1;
    }
    if (fn_OVRInput_Get(OVR_BTN_PRIMARY_TS, OVR_CTRL_RTOUCH, NULL)) {
        if (ws_pressing && (t - ws_press_start) > cfg_hold_duration) {
            ws_pressing = 0;
            LOGI("Swapping weapon");
            void *rightSword  = *(void **)((char *)self + 0x0C);
            void *timerCanvas = *(void **)((char *)self + 0x18);
            void *flareGun    = *(void **)((char *)self + 0x14);
            if (ws_sword_active) {
                set_active(rightSword,  0);
                set_active(timerCanvas, 0);
                set_active(flareGun,    1);
                ws_sword_active = 0;
            } else {
                set_active(rightSword,  1);
                set_active(timerCanvas, 1);
                set_active(flareGun,    0);
                ws_sword_active = 1;
            }
        }
    } else {
        if (ws_pressing) {
            ws_pressing = 0;
            LOGI("Flipping right handle");
            flip_right();
        }
    }
}

/* ── NetworkWeaponSwap.Update (full replacement) ────────────────────── */
/*   Fields: rightSword 0x10 | flareGun 0x14 | networkRightSword 0x18  */
static float nws_press_start = 0;
static int   nws_pressing    = 0;

MAKE_HOOK(NetworkWeaponSwap_Update, 0x63584C, void, void *self)
{
    /* Original never called — full replacement */
    float t = get_time();

    if (fn_OVRInput_GetDown(OVR_BTN_PRIMARY_TS, OVR_CTRL_RTOUCH, NULL)) {
        nws_press_start = t;
        nws_pressing    = 1;
    }
    if (fn_OVRInput_Get(OVR_BTN_PRIMARY_TS, OVR_CTRL_RTOUCH, NULL)) {
        if (nws_pressing && (t - nws_press_start) > cfg_hold_duration) {
            nws_pressing = 0;
            LOGI("Swapping weapon (network)");
            void *rightSword        = *(void **)((char *)self + 0x10);
            void *flareGun          = *(void **)((char *)self + 0x14);
            void *networkRightSword = *(void **)((char *)self + 0x18);
            if (nws_sword_active) {
                if (fn_SwordDisabled && networkRightSword)
                    fn_SwordDisabled(networkRightSword);
                set_active(rightSword, 0);
                set_active(flareGun,   1);
                nws_sword_active = 0;
            } else {
                set_active(rightSword, 1);
                set_active(flareGun,   0);
                nws_sword_active = 1;
            }
        }
    } else {
        if (nws_pressing) {
            nws_pressing = 0;
            LOGI("Flipping right handle (network)");
            flip_right();
        }
    }
}

/* ── OVRCameraRig.UpdateAnchors (postfix) ───────────────────────────── */
/*   Fields: leftHandAnchor 0x1C | rightHandAnchor 0x20                 */

MAKE_HOOK(OVRCameraRig_UpdateAnchors, 0x5AB168, void, void *self, int updateEye, int updateHand)
{
    OVRCameraRig_UpdateAnchors(self, updateEye, updateHand);   /* run original first */

    if (!fn_Transform_Rotate) return;

    void  *leftAnchor  = *(void **)((char *)self + 0x1C);
    void  *rightAnchor = *(void **)((char *)self + 0x20);
    float  t           = get_time();
    Vector3 zero       = { 0.0f, 0.0f, 0.0f };
    Vector3 offset     = { cfg_grip_offset_x, 0.0f, cfg_grip_offset_z };

    /* Left A button (Button.One = 1, LTouch = 1) toggles left grip */
    if (fn_OVRInput_GetDown(OVR_BTN_ONE, OVR_CTRL_LTOUCH, NULL) && !left_flip_anim) {
        reload_config();
        offset = (Vector3){ cfg_grip_offset_x, 0.0f, cfg_grip_offset_z };
        left_flip_anim  = 1;
        left_levi_grip  = !left_levi_grip;
        left_flip_start = t;
    }

    /* ── Left hand ───────────────────────────────────────────────────── */
    if (left_levi_grip && !left_flip_anim) {
        fn_Transform_Rotate(leftAnchor, offset, NULL);
    } else if (left_flip_anim) {
        Vector3 start = left_levi_grip ? zero   : offset;
        Vector3 end   = left_levi_grip ? offset : zero;
        float frac = (t - left_flip_start) / cfg_flip_duration;
        fn_Transform_Rotate(leftAnchor, lerp3(start, end, frac), NULL);
        if (t - left_flip_start > cfg_flip_duration) left_flip_anim = 0;
    }

    /* ── Right hand ──────────────────────────────────────────────────── */
    /* Animation path uses offset.z - 360 so the flip goes through -180  */
    Vector3 right_offset = { cfg_grip_offset_x, 0.0f, cfg_grip_offset_z - 360.0f };
    if (right_levi_grip && !right_flip_anim) {
        fn_Transform_Rotate(rightAnchor, offset, NULL);
    } else if (right_flip_anim) {
        Vector3 start = right_levi_grip ? zero         : right_offset;
        Vector3 end   = right_levi_grip ? right_offset : zero;
        float frac = (t - right_flip_start) / cfg_flip_duration;
        fn_Transform_Rotate(rightAnchor, lerp3(start, end, frac), NULL);
        if (t - right_flip_start > cfg_flip_duration) right_flip_anim = 0;
    }
}

/* ── Entry point ─────────────────────────────────────────────────────── */
__attribute__((constructor)) void lib_main(void)
{
    LOGI("loading...");

    aoqmm_register("liblevigrip.so", "Treys Levi Grip", "1.0.0", "Treyo1928",
                   "Rotates hand anchors for a levi-style grip. "
                   "Left A = toggle left grip, right thumbstick tap = flip right, hold = swap weapon.");

    aoqmm_ensure_config("liblevigrip.so",
        "{\n"
        "  \"entries\": [\n"
        "    {\"key\":\"GripOffsetX\",\"type\":\"float\",\"value\":82.0,"
            "\"description\":\"X rotation of the grip offset in degrees.\"},\n"
        "    {\"key\":\"GripOffsetZ\",\"type\":\"float\",\"value\":180.0,"
            "\"description\":\"Z rotation of the grip offset in degrees.\"},\n"
        "    {\"key\":\"FlipDuration\",\"type\":\"float\",\"value\":0.25,"
            "\"description\":\"Duration of the flip animation in seconds.\"},\n"
        "    {\"key\":\"HoldDuration\",\"type\":\"float\",\"value\":0.2,"
            "\"description\":\"Seconds to hold right thumbstick before swapping weapon.\"}\n"
        "  ]\n"
        "}\n"
    );

    reload_config();

    fn_OVRInput_GetDown = (OVRInput_GetDown_t) getRealOffset(0xAF7CAC);
    fn_OVRInput_Get     = (OVRInput_Get_t)     getRealOffset(0xAF7A08);
    fn_Transform_Rotate = (Transform_Rotate_t) getRealOffset(0xFD6DFC);
    fn_Time_get_time    = (Time_get_time_t)    getRealOffset(0xFD3EE0);
    fn_SetActive        = (SetActive_t)        getRealOffset(0xC745F0);
    fn_SwordDisabled    = (SwordDisabled_t)    getRealOffset(0x6331BC);

    INSTALL_HOOK(WeaponSwap_Update);
    INSTALL_HOOK(NetworkWeaponSwap_Update);
    INSTALL_HOOK(OVRCameraRig_UpdateAnchors);

    LOGI("hooks installed!");
}
