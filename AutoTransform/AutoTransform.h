/*******************************************************************/
/*                                                                 */
/*  AutoTransform.h                                                */
/*                                                                 */
/*  Auto Transform effect plugin for Premiere Pro                  */
/*  Based on Adobe After Effects SDK Skeleton sample               */
/*                                                                 */
/*******************************************************************/

#pragma once

#ifndef AUTOTRANSFORM_H
    #define AUTOTRANSFORM_H

typedef unsigned char u_char;
typedef unsigned short u_short;
typedef unsigned short u_int16;
typedef unsigned long u_long;
typedef short int int16;
typedef int int32;
    #define PF_TABLE_BITS 12
    #define PF_TABLE_SZ_16 4096

    #define PF_DEEP_COLOR_AWARE 1

    #include "AEConfig.h"

    #ifdef AE_OS_WIN
typedef unsigned short PixelType;
        #include <Windows.h>
    #endif

    #include "entry.h"
    #include "AE_Effect.h"
    #include "AE_EffectCB.h"
    #include "AE_Macros.h"
    #include "Param_Utils.h"
    #include "AE_EffectCBSuites.h"
    #include "String_Utils.h"
    #include "AE_GeneralPlug.h"
    #include "AEGP_SuiteHandler.h"
    #include "AE_EffectUI.h"
    #include "adobesdk/DrawbotSuite.h"
    #include "AEFX_SuiteHelper.h"

    #include "PrSDKAESupport.h"

    #include "AutoTransform_Strings.h"

/* Versioning information */

    #define MAJOR_VERSION   0
    #define MINOR_VERSION   1
    #define BUG_VERSION     3
    #define STAGE_VERSION   PF_Stage_DEVELOP
    #define BUILD_VERSION   1

/* ================================================================
 *  Parameter array indices (registration order = UI display order)
 * ================================================================ */

enum
{
    AT_INPUT = 0,           // Input layer (always index 0)

    AT_UPDATE_BANNER,        // 1  새 릴리스가 있을 때만 표시

    // --- 1. Transform Group (최상단, 기본 접힘) ---
    AT_GROUP_TRANSFORM_START, // "Transform" 그룹 시작
    AT_START_POSITION,        // Start Position
    AT_END_POSITION,          // End Position
    AT_START_SCALE,           // Start Scale
    AT_END_SCALE,             // End Scale
    AT_GROUP_TRANSFORM_END,   // Transform 그룹 끝

    // --- 2. Timing & Easing Group ---
    AT_GROUP_TIMING_START,  // 7
    AT_DURATION,            // 8
    AT_EASING_PRESET,       // 9
    AT_EASE_IN,             // 10
    AT_EASE_OUT,            // 11
    AT_GROUP_TIMING_END,    // 12

    // --- 3. Motion Blur Group ---
    AT_GROUP_BLUR_START,    // 13
    AT_SHUTTER_ANGLE,       // 14
    AT_SAMPLES,             // 15
    AT_GROUP_BLUR_END,      // 16

    // --- 4. Preset Settings Group (Buttons 바로 위, 기본 접힘) ---
    AT_GROUP_PRESET_SETTINGS_START, // 17 "Preset Settings" 그룹 시작
    AT_EDITOR_PRESET,         // 18 편집할 프리셋 번호 선택
    AT_EDITOR_POS,            // 19 편집 Position
    AT_EDITOR_SCALE,          // 20 편집 Scale
    AT_MODIFIER_OFFSET_MULT,  // 21 <> 오프셋 배율 (기본값 3.0)
    AT_GROUP_PRESET_SETTINGS_END,   // 22 Preset Settings 그룹 끝

    // --- 5. Bottom Controls ---
    AT_GRID_UI,             // 23 Custom Buttons UI
    AT_PRESET_CODE,         // 24 Preset Code (0 < 100 > 200)
    AT_MOVE_MODE,           // 25 Move Mode 체크박스

    AT_START_MASK_MODE,     // 26 hidden GPU mask state
    AT_END_MASK_MODE,       // 27 hidden GPU mask state

    AT_NUM_PARAMS           // 28
};

/* ================================================================
 *  Parameter Disk IDs (프로젝트/프리셋 호환성 — 절대 변경 금지)
 * ================================================================ */

enum
{
    /* 기존 Disk ID (1~14) — 변경 불가 */
    DURATION_DISK_ID           = 1,
    START_POSITION_DISK_ID     = 2,
    END_POSITION_DISK_ID       = 3,
    START_SCALE_DISK_ID        = 4,
    END_SCALE_DISK_ID          = 5,
    EASING_PRESET_DISK_ID      = 6,
    EASE_IN_DISK_ID            = 7,
    GROUP_TIMING_START_DISK_ID = 8,
    EASE_OUT_DISK_ID           = 9,
    GROUP_TIMING_END_DISK_ID   = 10,
    GROUP_BLUR_START_DISK_ID   = 11,
    SHUTTER_ANGLE_DISK_ID      = 12,
    SAMPLES_DISK_ID            = 13,
    GROUP_BLUR_END_DISK_ID     = 14,

    /* 신규 Disk ID (15~24) */
    GROUP_PRESET_START_DISK_ID  = 15,
    START_PRESET_DISK_ID        = 16,
    END_PRESET_DISK_ID          = 17,
    MOVE_MODE_DISK_ID           = 18,
    GROUP_EDITOR_START_DISK_ID  = 19,
    EDITOR_PRESET_DISK_ID       = 20,
    EDITOR_POS_DISK_ID          = 21,
    EDITOR_SCALE_DISK_ID        = 22,
    GROUP_EDITOR_END_DISK_ID    = 23,
    GROUP_PRESET_END_DISK_ID    = 24,

    /* Grid UI Disk ID (25) */
    GRID_UI_DISK_ID             = 25,

    /* 신규 Disk ID (26~31) */
    GROUP_TRANSFORM_START_DISK_ID       = 26,
    GROUP_TRANSFORM_END_DISK_ID         = 27,
    PRESET_CODE_DISK_ID                 = 28,
    GROUP_PRESET_SETTINGS_START_DISK_ID = 29,
    GROUP_PRESET_SETTINGS_END_DISK_ID   = 30,
    MODIFIER_OFFSET_MULT_DISK_ID        = 31,
    UPDATE_BANNER_DISK_ID              = 32,
    START_MASK_MODE_DISK_ID            = 33,
    END_MASK_MODE_DISK_ID              = 34
};

/* ================================================================
 *  Easing Presets
 * ================================================================ */

enum
{
    EASE_PRESET_LINEAR = 1,
    EASE_PRESET_EASE_IN,
    EASE_PRESET_EASE_OUT,
    EASE_PRESET_EASE_IN_OUT,
    EASE_PRESET_CUSTOM,
    EASE_PRESET_COUNT = 5
};

/* ================================================================
 *  Duration defaults
 * ================================================================ */

    #define DEFAULT_DURATION        10.0
    #define DURATION_MIN            1.0
    #define DURATION_MAX            9999.0
    #define DURATION_SLIDER_MIN     1.0
    #define DURATION_SLIDER_MAX     120.0

/* ================================================================
 *  Easing defaults (Linear by default)
 * ================================================================ */

    #define DEFAULT_EASE_IN         0.0
    #define DEFAULT_EASE_OUT        0.0

/* ================================================================
 *  Motion Blur defaults
 * ================================================================ */

    #define DEFAULT_SHUTTER_ANGLE        0.0
    #define SHUTTER_ANGLE_MIN            0.0
    #define SHUTTER_ANGLE_MAX            720.0
    #define SHUTTER_ANGLE_SLIDER_MIN     0.0
    #define SHUTTER_ANGLE_SLIDER_MAX     360.0

    #define DEFAULT_SAMPLES              8
    #define SAMPLES_MIN                  2
    #define SAMPLES_MAX                  32

/* ================================================================
 *  Position/Scale defaults
 * ================================================================ */

    /* Position default percentage (50 = layer center, host converts to pixels) */
    #define DEFAULT_POS_PERCENT     50

    #define DEFAULT_START_SCALE     100.0
    #define DEFAULT_END_SCALE       100.0
    #define SCALE_MIN               1.0
    #define SCALE_MAX               1000.0
    #define SCALE_SLIDER_MIN        10.0
    #define SCALE_SLIDER_MAX        400.0

/* ================================================================
 *  Preset System
 * ================================================================ */

    /* 총 프리셋 수 */
    #define AT_NUM_PRESETS      35

    /* 기준 해상도 (4K) — 좌표 해상도 독립 변환 기준 */
    #define PRESET_BASE_W       3840.0
    #define PRESET_BASE_H       2160.0

    /* Arbitrary Data 식별 refcon */
    #define ARB_REFCON          ((void*)0xABCD1234)

    /* Start/End Preset 드롭다운: Custom = 1번, Preset N = (N+1)번 */
    #define PRESET_POPUP_CUSTOM         1
    #define PRESET_POPUP_NUM_CHOICES    36      // Custom + 35 presets

    /* Editor Preset 드롭다운: Preset N = N번 (Custom 없음) */
    #define EDITOR_PRESET_NUM_CHOICES   35

    /* Editor 기본값: 1번 프리셋 값 (1920, 2700, 250%) 기준 */
    #define EDITOR_POS_DEFAULT_X    50      // 1920 / 3840 = 50%
    #define EDITOR_POS_DEFAULT_Y    125     // 2700 / 2160 = 125%
    #define EDITOR_SCALE_DEFAULT    250.0   // 250%
    #define EDITOR_SCALE_MIN        1.0
    #define EDITOR_SCALE_MAX        1000.0
    #define EDITOR_SCALE_SLIDER_MIN 10.0
    #define EDITOR_SCALE_SLIDER_MAX 400.0

/* 35개 프리셋 데이터 엔트리 */
struct PresetEntry
{
    double x;       // 4K 기준 픽셀 X 좌표
    double y;       // 4K 기준 픽셀 Y 좌표
    double scale;   // % 단위 (100.0 = 100%)
};

/* 프리셋 블록 전체 (ARBITRARY_DATA로 저장) */
struct PresetDataBlock
{
    A_u_long   version;                   // 호환성 버전 (현재 1)
    PresetEntry presets[AT_NUM_PRESETS];  // 35개 프리셋
};

/* ================================================================
 *  Custom ECW Grid UI Types & Constants
 * ================================================================ */

#define GRID_ARB_REFCON             ((void*)0xABCD5678)
#define GRID_ARB_MAGIC              0x41545052  // 'ATPR'
#define GRID_ARB_VERSION            1
#define UI_GRID_WIDTH               240
#define UI_GRID_HEIGHT              175

/* Grid UI Arbitrary 데이터 (35개 프리셋 테이블 직렬화) */
struct GridUIPresetData
{
    A_long          magic;       // GRID_ARB_MAGIC
    A_long          version;     // GRID_ARB_VERSION
    PresetDataBlock presetData;  // 35개 프리셋 (x, y, scale)
};

/* 프리셋 수평 오프셋 모디파이어 (X1: Left <, X2: Right >) */
enum class PresetModifier
{
    None = 0,
    X1   = 1, // Left (<)  : -(scale * 2.5)
    X2   = 2  // Right (>) : +(scale * 2.5)
};

/* 프리셋 역추적 매칭 결과 */
struct MatchResult
{
    int            presetId; // 1~35 (0: Custom / 불일치)
    PresetModifier modifier; // None, X1, X2
};

/* Grid Slot 타입 */
enum class GridSlotType
{
    Preset,
    Special
};

/* Grid Slot 정보 */
struct GridSlot
{
    GridSlotType type;
    int          id; // 1~35 (Preset) 또는 1~2 (Special X1, X2)
};

/* 버튼 색상 상태 */
enum class ButtonColorState
{
    Default,
    StartYellow,
    EndGreen,
    ConflictRed,
    CustomBlue,
    DisabledSpecial
};

/* 전체 UI 상태 정보 */
struct UIStateInfo
{
    bool           startIsCustom;
    int            startPresetId; // 1~35 (0이면 Custom)
    PresetModifier startMod;      // None, X1, X2

    bool           endIsCustom;
    int            endPresetId;   // 1~35 (0이면 Custom)
    PresetModifier endMod;        // None, X1, X2

    bool           isTransformEqual;
};

/* 프리셋 역추적 매칭 헬퍼 (픽셀 좌표 기반, CPU/GPU 공용) */
void GetDefaultPresets(PresetDataBlock* data);

MatchResult FindMatchingPresetExtPixel(
    const PresetDataBlock* data,
    double                 curX,
    double                 curY,
    double                 scaleVal,
    A_long                 clipWidth,
    A_long                 clipHeight,
    double                 offsetMult = 3.0);

extern "C"
{

    DllExport PF_Err EffectMain(
        PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
        PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}

#endif // AUTOTRANSFORM_H
