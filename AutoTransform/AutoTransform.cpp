/*******************************************************************/
/*                                                                 */
/*  AutoTransform.cpp                                              */
/*                                                                 */
/*  Automated position and scale transform effect for              */
/*  Adobe Premiere Pro. No keyframes needed.                       */
/*                                                                 */
/*  Based on Adobe After Effects SDK Skeleton sample.              */
/*                                                                 */
/*******************************************************************/

#include "AutoTransform.h"
#include "AutoTransform_Update.h"
#include "AutoTransform_CPU.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <mutex>
#include <unordered_set>
/* ================================================================
 *  About
 * ================================================================ */

static PF_Err About(
    PF_InData*   in_data,
    PF_OutData*  out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output)
{
    AEGP_SuiteHandler suites(in_data->pica_basicP);

    suites.ANSICallbacksSuite1()->sprintf(
        out_data->return_msg,
        "%s v%d.%d.%d\r%s",
        STR(StrID_Name),
        MAJOR_VERSION,
        MINOR_VERSION,
        BUG_VERSION,
        STR(StrID_Description));

    return PF_Err_NONE;
}

/* ================================================================
 *  GlobalSetup
 * ================================================================ */

static PF_Err GlobalSetup(
    PF_InData*   in_data,
    PF_OutData*  out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output)
{
    out_data->my_version = PF_VERSION(
        MAJOR_VERSION, MINOR_VERSION, BUG_VERSION,
        STAGE_VERSION, BUILD_VERSION);

    if (in_data->appl_id == 'PrMr' && in_data->pica_basicP)
    {
        PF_PixelFormatSuite1* pixelFormatSuite = NULL;
        if (in_data->pica_basicP->AcquireSuite(
                kPFPixelFormatSuite,
                kPFPixelFormatSuiteVersion1,
                (const void**)&pixelFormatSuite) == PF_Err_NONE && pixelFormatSuite)
        {
            pixelFormatSuite->ClearSupportedPixelFormats(in_data->effect_ref);
            pixelFormatSuite->AddSupportedPixelFormat(in_data->effect_ref, PrPixelFormat_VUYA_4444_32f);
            pixelFormatSuite->AddSupportedPixelFormat(in_data->effect_ref, PrPixelFormat_BGRA_4444_32f);
            in_data->pica_basicP->ReleaseSuite(kPFPixelFormatSuite, kPFPixelFormatSuiteVersion1);
        }
    }

    out_data->out_flags =
        PF_OutFlag_DEEP_COLOR_AWARE |
        PF_OutFlag_PIX_INDEPENDENT  |
        PF_OutFlag_NON_PARAM_VARY   |
        PF_OutFlag_SEND_UPDATE_PARAMS_UI |
        PF_OutFlag_USE_OUTPUT_EXTENT |
        PF_OutFlag_CUSTOM_UI;

    out_data->out_flags2 =
        PF_OutFlag2_SUPPORTS_THREADED_RENDERING |
        PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG;

    return PF_Err_NONE;
}

/* ================================================================
 *  GetDefaultPresets — 35개 프리셋 기본값 초기화 (4K 기준 좌표)
 * ================================================================ */

void GetDefaultPresets(PresetDataBlock* data)
{
    if (!data) return;

    data->version = 1;

    /* 4K(3840×2160) 기준 절대 픽셀 좌표와 Scale(%) */
    static const struct { double x; double y; double scale; }
        kDefaults[AT_NUM_PRESETS] =
    {
        { 1920,  2700, 250.0 },  /* 01 */
        { 6120,  4320, 400.0 },  /* 02 */
        { 3980,  3200, 300.0 },  /* 03 */
        { 2960,  3200, 300.0 },  /* 04 */
        { 1920,  3200, 300.0 },  /* 05 */
        {  980,  3200, 300.0 },  /* 06 */
        { -140,  3200, 300.0 },  /* 07 */
        {-2280,  4320, 400.0 },  /* 08 */
        { 4620,  2700, 250.0 },  /* 09 */
        { 3680,  2060, 250.0 },  /* 10 */
        { 2820,  2060, 250.0 },  /* 11 */
        { 1920,  2060, 250.0 },  /* 12 */
        { 1020,  2060, 250.0 },  /* 13 */
        {  160,  2060, 250.0 },  /* 14 */
        { -660,  2160, 250.0 },  /* 15 */
        { 4720,  2000, 250.0 },  /* 16 */
        { 2840,  1200, 250.0 },  /* 17 */
        { 1920,  1080, 100.0 },  /* 18 */
        { 1000,  1200, 250.0 },  /* 19 */
        { -960,   560, 250.0 },  /* 20 */
        { 4720,   440, 250.0 },  /* 21 */
        { 3820,   420, 250.0 },  /* 22 */
        { 2860,   420, 250.0 },  /* 23 */
        { 1920,   420, 250.0 },  /* 24 */
        {  980,   420, 250.0 },  /* 25 */
        {   20,   420, 250.0 },  /* 26 */
        { -960,    20, 250.0 },  /* 27 */
        { 4800,  -540, 250.0 },  /* 28 */
        { 3880,  -460, 250.0 },  /* 29 */
        { 2900,  -460, 250.0 },  /* 30 */
        { 1920,  -460, 250.0 },  /* 31 */
        {  940,  -460, 250.0 },  /* 32 */
        {  -40,  -460, 250.0 },  /* 33 */
        { -960,  -540, 250.0 },  /* 34 */
        { 1920,  -540, 250.0 },  /* 35 */
    };

    for (int i = 0; i < AT_NUM_PRESETS; i++)
    {
        data->presets[i].x     = kDefaults[i].x;
        data->presets[i].y     = kDefaults[i].y;
        data->presets[i].scale = kDefaults[i].scale;
    }
}

static PF_Handle CreateDefaultGridPresetHandle(PF_InData* in_data)
{
    PF_Handle h = PF_NEW_HANDLE(sizeof(GridUIPresetData));
    if (!h) return NULL;

    GridUIPresetData* p = reinterpret_cast<GridUIPresetData*>(PF_LOCK_HANDLE(h));
    if (!p)
    {
        PF_DISPOSE_HANDLE(h);
        return NULL;
    }

    p->magic = GRID_ARB_MAGIC;
    p->version = GRID_ARB_VERSION;
    GetDefaultPresets(&p->presetData);
    PF_UNLOCK_HANDLE(h);
    return h;
}

/* ================================================================
 *  ParamsSetup
 * ================================================================ */

static PF_Err ParamsSetup(
    PF_InData*   in_data,
    PF_OutData*  out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output)
{
    PF_Err      err = PF_Err_NONE;
    PF_ParamDef def;

    /* 다운로드 안내는 렌더링 값이 아닌 UI 전용이며, 기본적으로 행 전체를 숨긴다. */
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY | PF_ParamFlag_CANNOT_INTERP;
    def.ui_flags = PF_PUI_CONTROL | PF_PUI_INVISIBLE | PF_PUI_DONT_ERASE_CONTROL;
    def.ui_width = UI_GRID_WIDTH;
    def.ui_height = 26;
    PF_ADD_CHECKBOX(STR(StrID_UpdateBanner_Name), "", FALSE, 0, UPDATE_BANNER_DISK_ID);

    /* ============================================================
     *  1. Transform Group (최상단, 기본 접힘)
     * ============================================================ */

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_START_COLLAPSED;
    PF_ADD_TOPIC(STR(StrID_Topic_Transform), GROUP_TRANSFORM_START_DISK_ID);

    /* Start Position */
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY | PF_ParamFlag_SUPERVISE;
    PF_ADD_POINT(
        STR(StrID_StartPos_Name),
        DEFAULT_POS_PERCENT,
        DEFAULT_POS_PERCENT,
        false,
        START_POSITION_DISK_ID);

    /* End Position */
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY | PF_ParamFlag_SUPERVISE;
    PF_ADD_POINT(
        STR(StrID_EndPos_Name),
        DEFAULT_POS_PERCENT,
        DEFAULT_POS_PERCENT,
        false,
        END_POSITION_DISK_ID);

    /* Start Scale */
    PF_ADD_FLOAT_SLIDERX(
        STR(StrID_StartScale_Name),
        SCALE_MIN,
        SCALE_MAX,
        SCALE_SLIDER_MIN,
        SCALE_SLIDER_MAX,
        DEFAULT_START_SCALE,
        PF_Precision_TENTHS,
        PF_ValueDisplayFlag_PERCENT,
        PF_ParamFlag_CANNOT_TIME_VARY | PF_ParamFlag_SUPERVISE,
        START_SCALE_DISK_ID);

    /* End Scale */
    PF_ADD_FLOAT_SLIDERX(
        STR(StrID_EndScale_Name),
        SCALE_MIN,
        SCALE_MAX,
        SCALE_SLIDER_MIN,
        SCALE_SLIDER_MAX,
        DEFAULT_END_SCALE,
        PF_Precision_TENTHS,
        PF_ValueDisplayFlag_PERCENT,
        PF_ParamFlag_CANNOT_TIME_VARY | PF_ParamFlag_SUPERVISE,
        END_SCALE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(GROUP_TRANSFORM_END_DISK_ID);

    /* ============================================================
     *  2. Timing & Easing Group (기존 접힘)
     * ============================================================ */

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_START_COLLAPSED;
    PF_ADD_TOPIC(STR(StrID_Topic_Timing), GROUP_TIMING_START_DISK_ID);

    /* Duration (frames) */
    PF_ADD_FLOAT_SLIDERX(
        STR(StrID_Duration_Name),
        DURATION_MIN,
        DURATION_MAX,
        DURATION_SLIDER_MIN,
        DURATION_SLIDER_MAX,
        DEFAULT_DURATION,
        PF_Precision_INTEGER,
        0,
        PF_ParamFlag_CANNOT_TIME_VARY,
        DURATION_DISK_ID);

    /* Easing Preset 드롭다운 */
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY | PF_ParamFlag_CANNOT_INTERP;
    PF_ADD_POPUP(
        STR(StrID_Easing_Preset_Name),
        EASE_PRESET_COUNT,
        EASE_PRESET_LINEAR,
        STR(StrID_Easing_Choices),
        EASING_PRESET_DISK_ID);

    /* Ease In (%) */
    PF_ADD_FLOAT_SLIDERX(
        STR(StrID_EaseIn_Name),
        0.0,
        100.0,
        0.0,
        100.0,
        DEFAULT_EASE_IN,
        PF_Precision_INTEGER,
        PF_ValueDisplayFlag_PERCENT,
        PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY,
        EASE_IN_DISK_ID);

    /* Ease Out (%) */
    PF_ADD_FLOAT_SLIDERX(
        STR(StrID_EaseOut_Name),
        0.0,
        100.0,
        0.0,
        100.0,
        DEFAULT_EASE_OUT,
        PF_Precision_INTEGER,
        PF_ValueDisplayFlag_PERCENT,
        PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY,
        EASE_OUT_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(GROUP_TIMING_END_DISK_ID);

    /* ============================================================
     *  3. Motion Blur Group (기존 접힘)
     * ============================================================ */

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_START_COLLAPSED;
    PF_ADD_TOPIC(STR(StrID_Topic_MotionBlur), GROUP_BLUR_START_DISK_ID);

    /* Shutter Angle */
    PF_ADD_FLOAT_SLIDERX(
        STR(StrID_ShutterAngle_Name),
        SHUTTER_ANGLE_MIN,
        SHUTTER_ANGLE_MAX,
        SHUTTER_ANGLE_SLIDER_MIN,
        SHUTTER_ANGLE_SLIDER_MAX,
        DEFAULT_SHUTTER_ANGLE,
        PF_Precision_TENTHS,
        0,
        PF_ParamFlag_CANNOT_TIME_VARY,
        SHUTTER_ANGLE_DISK_ID);

    /* Samples */
    PF_ADD_SLIDER(
        STR(StrID_Samples_Name),
        SAMPLES_MIN,
        SAMPLES_MAX,
        SAMPLES_MIN,
        SAMPLES_MAX,
        DEFAULT_SAMPLES,
        SAMPLES_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(GROUP_BLUR_END_DISK_ID);

    /* ============================================================
     *  4. Preset Settings Group (Buttons 바로 위, 기본 접힘)
     * ============================================================ */

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_START_COLLAPSED;
    PF_ADD_TOPIC(STR(StrID_Topic_PresetSettings), GROUP_PRESET_SETTINGS_START_DISK_ID);

    /* Editor: Preset 선택 드롭다운 (01~35, 35개) */
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE |
                PF_ParamFlag_CANNOT_TIME_VARY |
                PF_ParamFlag_CANNOT_INTERP;
    PF_ADD_POPUP(
        STR(StrID_EditorPreset_Name),
        EDITOR_PRESET_NUM_CHOICES,
        1,
        STR(StrID_EditorPreset_Choices),
        EDITOR_PRESET_DISK_ID);

    /* Editor: Position */
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY;
    PF_ADD_POINT(
        STR(StrID_EditorPos_Name),
        EDITOR_POS_DEFAULT_X,
        EDITOR_POS_DEFAULT_Y,
        false,
        EDITOR_POS_DISK_ID);

    /* Editor: Scale */
    PF_ADD_FLOAT_SLIDERX(
        STR(StrID_EditorScale_Name),
        EDITOR_SCALE_MIN,
        EDITOR_SCALE_MAX,
        EDITOR_SCALE_SLIDER_MIN,
        EDITOR_SCALE_SLIDER_MAX,
        EDITOR_SCALE_DEFAULT,
        PF_Precision_TENTHS,
        PF_ValueDisplayFlag_PERCENT,
        PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY,
        EDITOR_SCALE_DISK_ID);

    /* Modifier Offset Multiplier (배율 슬라이더: 기본값 3.0) */
    PF_ADD_FLOAT_SLIDERX(
        STR(StrID_OffsetMult_Name),
        0.0,
        10.0,
        0.0,
        10.0,
        3.0,
        PF_Precision_TENTHS,
        0,
        PF_ParamFlag_CANNOT_TIME_VARY,
        MODIFIER_OFFSET_MULT_DISK_ID);

    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(GROUP_PRESET_SETTINGS_END_DISK_ID);

    /* ============================================================
     *  4. Bottom Controls: Grid UI, Preset Code, Move Mode
     *     (CANNOT_TIME_VARY 적용으로 시계 모양 아이콘 제거)
     * ============================================================ */

    /* Grid & Custom UI */
    AEFX_CLR_STRUCT(def);
    def.flags    = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY;
    def.ui_flags = PF_PUI_CONTROL | PF_PUI_DONT_ERASE_CONTROL;
    def.ui_width = UI_GRID_WIDTH;
    def.ui_height = UI_GRID_HEIGHT;

    PF_Handle defaultGridPresets = CreateDefaultGridPresetHandle(in_data);
    if (!defaultGridPresets) return PF_Err_OUT_OF_MEMORY;

    PF_ADD_ARBITRARY2(
        STR(StrID_GridUI_Name),
        UI_GRID_WIDTH,
        UI_GRID_HEIGHT,
        PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY,
        PF_PUI_CONTROL | PF_PUI_DONT_ERASE_CONTROL,
        defaultGridPresets,
        GRID_UI_DISK_ID,
        GRID_ARB_REFCON);

    if (!err)
    {
        PF_CustomUIInfo ci;
        AEFX_CLR_STRUCT(ci);
        ci.events = PF_CustomEFlag_EFFECT;
        ci.comp_ui_width = ci.comp_ui_height = 0;
        ci.comp_ui_alignment = PF_UIAlignment_NONE;
        ci.layer_ui_width = ci.layer_ui_height = 0;
        ci.layer_ui_alignment = PF_UIAlignment_NONE;
        ci.preview_ui_width = ci.preview_ui_height = 0;
        err = (*(in_data->inter.register_ui))(in_data->effect_ref, &ci);
    }

    /* Preset Code (키보드 매크로용 숫자 입력란) */
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY;
    PF_ADD_SLIDER(
        STR(StrID_PresetCode_Name),
        0,
        300,
        0,
        300,
        0,
        PRESET_CODE_DISK_ID);

    /* Move Mode 체크박스 */
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY;
    PF_ADD_CHECKBOX(
        STR(StrID_MoveMode_Name),
        "",
        FALSE,
        0,
        MOVE_MODE_DISK_ID);

    // Persist crop modes in scalar parameters the GPU filter can read.
    // 3 means a project saved before these parameters were introduced.
    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY | PF_ParamFlag_CANNOT_INTERP;
    def.ui_flags = PF_PUI_INVISIBLE;
    PF_ADD_SLIDER("Start Mask Mode", 0, 3, 0, 3, 3, START_MASK_MODE_DISK_ID);

    AEFX_CLR_STRUCT(def);
    def.flags = PF_ParamFlag_CANNOT_TIME_VARY | PF_ParamFlag_CANNOT_INTERP;
    def.ui_flags = PF_PUI_INVISIBLE;
    PF_ADD_SLIDER("End Mask Mode", 0, 3, 0, 3, 3, END_MASK_MODE_DISK_ID);

    out_data->num_params = AT_NUM_PARAMS;

    return err;
}

/* ================================================================
 *  프리셋 데이터 테이블 (기본값 및 인스턴스 데이터 접근 헬퍼)
 * ================================================================ */

static PresetDataBlock s_defaultPresets;
static bool            s_defaultPresetsReady = false;

static PresetDataBlock* GetDefaultPresetDataBlock()
{
    if (!s_defaultPresetsReady)
    {
        GetDefaultPresets(&s_defaultPresets);
        s_defaultPresetsReady = true;
    }
    return &s_defaultPresets;
}

static bool GetEffectPresetData(PF_InData* in_data, PF_ParamDef* params[], PresetDataBlock* outData)
{
    if (in_data && params && params[AT_GRID_UI] && params[AT_GRID_UI]->u.arb_d.value)
    {
        PF_Handle h = params[AT_GRID_UI]->u.arb_d.value;
        GridUIPresetData* p = reinterpret_cast<GridUIPresetData*>(PF_LOCK_HANDLE(h));
        if (p)
        {
            if (p->magic == GRID_ARB_MAGIC)
            {
                if (outData) *outData = p->presetData;
                PF_UNLOCK_HANDLE(h);
                return true;
            }
            PF_UNLOCK_HANDLE(h);
        }
    }
    if (outData) *outData = *GetDefaultPresetDataBlock();
    return false;
}

/* ================================================================
 *  Preset 헬퍼 — 현재 Position/Scale이 어느 프리셋 및 모디파이어와 일치하는지 검색
 *  반환: MatchResult (presetId 1~35 또는 0, modifier None/X1/X2)
 * ================================================================ */

MatchResult FindMatchingPresetExtPixel(
    const PresetDataBlock* data,
    double                 curX,
    double                 curY,
    double                 scaleVal,
    A_long                 clipWidth,
    A_long                 clipHeight,
    double                 offsetMult)
{
    MatchResult res = { 0, PresetModifier::None };
    if (!data || clipWidth <= 0 || clipHeight <= 0) return res;

    for (int i = 0; i < AT_NUM_PRESETS; i++)
    {
        double baseY = (data->presets[i].y / PRESET_BASE_H) * (double)clipHeight;
        if (fabs(curY - baseY) >= 2.0) continue;
        if (fabs(scaleVal - data->presets[i].scale) >= 0.1) continue;

        double baseScale = data->presets[i].scale;

        // 1) None (오프셋 없음)
        double expectedX_None = (data->presets[i].x / PRESET_BASE_W) * (double)clipWidth;
        if (fabs(curX - expectedX_None) < 2.0)
        {
            res.presetId = i + 1;
            res.modifier = PresetModifier::None;
            return res;
        }

        // 2) X1 (<): base_x - scale * offsetMult
        double expectedX_X1 = ((data->presets[i].x - (baseScale * offsetMult)) / PRESET_BASE_W) * (double)clipWidth;
        if (fabs(curX - expectedX_X1) < 2.0)
        {
            res.presetId = i + 1;
            res.modifier = PresetModifier::X1;
            return res;
        }

        // 3) X2 (>): base_x + scale * offsetMult
        double expectedX_X2 = ((data->presets[i].x + (baseScale * offsetMult)) / PRESET_BASE_W) * (double)clipWidth;
        if (fabs(curX - expectedX_X2) < 2.0)
        {
            res.presetId = i + 1;
            res.modifier = PresetModifier::X2;
            return res;
        }
    }
    return res;
}

static MatchResult FindMatchingPresetExt(
    const PresetDataBlock* data,
    PF_Fixed               xFixed,
    PF_Fixed               yFixed,
    PF_FpShort             scaleVal,
    A_long                 clipWidth,
    A_long                 clipHeight,
    double                 offsetMult = 3.0)
{
    return FindMatchingPresetExtPixel(
        data,
        FIX_2_FLOAT(xFixed),
        FIX_2_FLOAT(yFixed),
        (double)scaleVal,
        clipWidth,
        clipHeight,
        offsetMult);
}

static int FindMatchingPreset(
    const PresetDataBlock* data,
    PF_Fixed               xFixed,
    PF_Fixed               yFixed,
    PF_FpShort             scaleVal,
    A_long                 clipWidth,
    A_long                 clipHeight)
{
    MatchResult m = FindMatchingPresetExt(data, xFixed, yFixed, scaleVal, clipWidth, clipHeight);
    return (m.presetId > 0 && m.modifier == PresetModifier::None) ? (m.presetId - 1) : -1;
}

static void SyncMaskModes(PF_InData* in_data, PF_ParamDef* params[])
{
    if (!in_data || !params || !params[AT_START_MASK_MODE] || !params[AT_END_MASK_MODE]) return;

    PresetDataBlock presets;
    GetEffectPresetData(in_data, params, &presets);
    A_long width = params[AT_INPUT]->u.ld.width;
    A_long height = params[AT_INPUT]->u.ld.height;
    if (width <= 0) width = in_data->width > 0 ? in_data->width : (A_long)PRESET_BASE_W;
    if (height <= 0) height = in_data->height > 0 ? in_data->height : (A_long)PRESET_BASE_H;
    double offset = params[AT_MODIFIER_OFFSET_MULT]->u.fs_d.value;
    if (offset < 0.0) offset = 0.0;

    const int positions[] = { AT_START_POSITION, AT_END_POSITION };
    const int scales[] = { AT_START_SCALE, AT_END_SCALE };
    const int modes[] = { AT_START_MASK_MODE, AT_END_MASK_MODE };
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    for (int i = 0; i < 2; ++i) {
        const MatchResult match = FindMatchingPresetExt(&presets,
            params[positions[i]]->u.td.x_value,
            params[positions[i]]->u.td.y_value,
            params[scales[i]]->u.fs_d.value, width, height, offset);
        const A_long mode = static_cast<A_long>(match.modifier);
        if (params[modes[i]]->u.sd.value != mode) {
            params[modes[i]]->u.sd.value = mode;
            params[modes[i]]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
            suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref, modes[i], params[modes[i]]);
        }
    }
}

/* ================================================================
 *  내부 UI 업데이트 재진입 방지 가드
 * ================================================================ */

static bool s_isInternalUpdating = false;
static A_long s_lastProgrammedPresetCode = -1;

struct AutoInternalUpdateGuard
{
    AutoInternalUpdateGuard()  { s_isInternalUpdating = true; }
    ~AutoInternalUpdateGuard() { s_isInternalUpdating = false; }
};

/* ================================================================
 *  UserChangedParam — 파라미터 간 상호 동기화
 * ================================================================ */

static PF_Err UserChangedParam(
    PF_InData*                      in_data,
    PF_OutData*                     out_data,
    PF_ParamDef*                    params[],
    PF_LayerDef*                    output,
    const PF_UserChangedParamExtra* extra)
{
    PF_Err err = PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);

    if (!extra) return err;

    const A_long paramIdx = extra->param_index;

    if (s_isInternalUpdating)
    {
        return PF_Err_NONE;
    }

    /* 클립 실제 해상도 취득 */
    A_long clipW = params[AT_INPUT]->u.ld.width;
    A_long clipH = params[AT_INPUT]->u.ld.height;
    if (clipW <= 0) clipW = (in_data->width  > 0) ? in_data->width  : (A_long)PRESET_BASE_W;
    if (clipH <= 0) clipH = (in_data->height > 0) ? in_data->height : (A_long)PRESET_BASE_H;

    double offsetMult = (double)params[AT_MODIFIER_OFFSET_MULT]->u.fs_d.value;
    if (offsetMult < 0.0) offsetMult = 0.0;

    /* ----------------------------------------------------------
     *  Case 1: Start / End Position / Scale 직접 수정 또는 오프셋 배율 수정
     *  → 그리드 UI 하이라이트 실시간 갱신 및 Preset Code 역동기화
     * ---------------------------------------------------------- */
    if (paramIdx == AT_START_POSITION || paramIdx == AT_START_SCALE ||
        paramIdx == AT_END_POSITION   || paramIdx == AT_END_SCALE   ||
        paramIdx == AT_MODIFIER_OFFSET_MULT)
    {
        PresetDataBlock effectPresets;
        GetEffectPresetData(in_data, params, &effectPresets);
        MatchResult endMatch = FindMatchingPresetExt(
            &effectPresets,
            params[AT_END_POSITION]->u.td.x_value,
            params[AT_END_POSITION]->u.td.y_value,
            params[AT_END_SCALE]->u.fs_d.value,
            clipW, clipH, offsetMult);

        A_long expectedCode = 0;
        if (endMatch.presetId >= 1 && endMatch.presetId <= AT_NUM_PRESETS)
        {
            if (endMatch.modifier == PresetModifier::X1)      expectedCode = endMatch.presetId;       // 0단위 (<)
            else if (endMatch.modifier == PresetModifier::None) expectedCode = 100 + endMatch.presetId; // 100단위 (기본)
            else if (endMatch.modifier == PresetModifier::X2) expectedCode = 200 + endMatch.presetId; // 200단위 (>)
        }

        if (params[AT_PRESET_CODE]->u.sd.value != expectedCode)
        {
            AutoInternalUpdateGuard guard;
            s_lastProgrammedPresetCode = expectedCode;
            params[AT_PRESET_CODE]->u.sd.value = expectedCode;
            params[AT_PRESET_CODE]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
            suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref, AT_PRESET_CODE, params[AT_PRESET_CODE]);
        }

        out_data->out_flags |= PF_OutFlag_REFRESH_UI;
        if (paramIdx == AT_MODIFIER_OFFSET_MULT)
        {
            out_data->out_flags |= PF_OutFlag_FORCE_RERENDER;
        }
    }

    /* ----------------------------------------------------------
     *  Case: Preset Code 입력 (0 < 100 > 200 체계)
     *  → 해당 도착점 프리셋 버튼을 누른 것과 완전히 동일하게 처리
     * ---------------------------------------------------------- */
    else if (paramIdx == AT_PRESET_CODE)
    {
        A_long code = params[AT_PRESET_CODE]->u.sd.value;
        int targetPresetId = -1;
        int targetModifier = 0;

        if (code == s_lastProgrammedPresetCode)
        {
            SyncMaskModes(in_data, params);
            return PF_Err_NONE;
        }
        s_lastProgrammedPresetCode = code;

        if (code >= 1 && code <= AT_NUM_PRESETS)
        {
            targetPresetId = (int)code;
            targetModifier = 1; // 0단위: < (좌측 절반)
        }
        else if (code >= 101 && code <= 100 + AT_NUM_PRESETS)
        {
            targetPresetId = (int)(code - 100);
            targetModifier = 0; // 100단위: 기본 전체 화면
        }
        else if (code >= 201 && code <= 200 + AT_NUM_PRESETS)
        {
            targetPresetId = (int)(code - 200);
            targetModifier = 2; // 200단위: > (우측 절반)
        }

        if (targetPresetId >= 1 && targetPresetId <= AT_NUM_PRESETS)
        {
            AutoInternalUpdateGuard guard;

            PresetDataBlock effectPresets;
            GetEffectPresetData(in_data, params, &effectPresets);
            int newIdx = targetPresetId - 1;

            bool moveMode = (params[AT_MOVE_MODE]->u.bd.value != 0);

            if (moveMode)
            {
                params[AT_START_POSITION]->u.td.x_value = params[AT_END_POSITION]->u.td.x_value;
                params[AT_START_POSITION]->u.td.y_value = params[AT_END_POSITION]->u.td.y_value;
                params[AT_START_POSITION]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;

                params[AT_START_SCALE]->u.fs_d.value = params[AT_END_SCALE]->u.fs_d.value;
                params[AT_START_SCALE]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;

                suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                    in_data->effect_ref, AT_START_POSITION, params[AT_START_POSITION]);
                suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                    in_data->effect_ref, AT_START_SCALE, params[AT_START_SCALE]);
            }

            double basePresetX = effectPresets.presets[newIdx].x;
            double basePresetY = effectPresets.presets[newIdx].y;
            double baseScale   = effectPresets.presets[newIdx].scale;

            if (targetModifier == 1) // X1 (<) 좌측 이동: -(scale * offsetMult)
            {
                basePresetX -= (baseScale * offsetMult);
            }
            else if (targetModifier == 2) // X2 (>) 우측 이동: +(scale * offsetMult)
            {
                basePresetX += (baseScale * offsetMult);
            }

            double xSet = (basePresetX / PRESET_BASE_W) * (double)clipW;
            double ySet = (basePresetY / PRESET_BASE_H) * (double)clipH;

            params[AT_END_POSITION]->u.td.x_value = FLOAT2FIX(xSet);
            params[AT_END_POSITION]->u.td.y_value = FLOAT2FIX(ySet);
            params[AT_END_POSITION]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;

            params[AT_END_SCALE]->u.fs_d.value = (PF_FpShort)baseScale;
            params[AT_END_SCALE]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;

            suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref, AT_END_POSITION, params[AT_END_POSITION]);
            suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref, AT_END_SCALE, params[AT_END_SCALE]);

            out_data->out_flags |= PF_OutFlag_REFRESH_UI | PF_OutFlag_FORCE_RERENDER;
        }
        else
        {
            // 유효하지 않은 코드이거나 0 입력 시 실제 매칭 상태로 복구
            PresetDataBlock effectPresets;
            GetEffectPresetData(in_data, params, &effectPresets);
            MatchResult endMatch = FindMatchingPresetExt(
                &effectPresets,
                params[AT_END_POSITION]->u.td.x_value,
                params[AT_END_POSITION]->u.td.y_value,
                params[AT_END_SCALE]->u.fs_d.value,
                clipW, clipH, offsetMult);

            A_long expectedCode = 0;
            if (endMatch.presetId >= 1 && endMatch.presetId <= AT_NUM_PRESETS)
            {
                if (endMatch.modifier == PresetModifier::X1)      expectedCode = endMatch.presetId;
                else if (endMatch.modifier == PresetModifier::None) expectedCode = 100 + endMatch.presetId;
                else if (endMatch.modifier == PresetModifier::X2) expectedCode = 200 + endMatch.presetId;
            }

            if (params[AT_PRESET_CODE]->u.sd.value != expectedCode)
            {
                AutoInternalUpdateGuard guard;
                s_lastProgrammedPresetCode = expectedCode;
                params[AT_PRESET_CODE]->u.sd.value = expectedCode;
                params[AT_PRESET_CODE]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
                suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                    in_data->effect_ref, AT_PRESET_CODE, params[AT_PRESET_CODE]);
            }
        }
    }

    /* ----------------------------------------------------------
     *  Case 2: Preset Editor — 프리셋 번호 선택
     *  → 해당 프리셋 데이터를 Editor Position/Scale에 로드
     * ---------------------------------------------------------- */
    else if (paramIdx == AT_EDITOR_PRESET)
    {
        A_long choice = params[AT_EDITOR_PRESET]->u.pd.value;
        int    idx    = choice - 1;  /* 1-based → 0-based */

        if (idx >= 0 && idx < AT_NUM_PRESETS)
        {
            PresetDataBlock effectPresets;
            GetEffectPresetData(in_data, params, &effectPresets);

            double xSet = (effectPresets.presets[idx].x / PRESET_BASE_W) * (double)clipW;
            double ySet = (effectPresets.presets[idx].y / PRESET_BASE_H) * (double)clipH;

            params[AT_EDITOR_POS]->u.td.x_value = FLOAT2FIX(xSet);
            params[AT_EDITOR_POS]->u.td.y_value = FLOAT2FIX(ySet);
            params[AT_EDITOR_POS]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;

            params[AT_EDITOR_SCALE]->u.fs_d.value = (PF_FpShort)effectPresets.presets[idx].scale;
            params[AT_EDITOR_SCALE]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;

            suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref, AT_EDITOR_POS, params[AT_EDITOR_POS]);
            suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref, AT_EDITOR_SCALE, params[AT_EDITOR_SCALE]);

            out_data->out_flags |= PF_OutFlag_REFRESH_UI;
        }
    }

    /* ----------------------------------------------------------
     *  Case 3: Preset Editor — Position / Scale 값 수정
     *  → 현재 클립 인스턴스의 Grid UI Arbitrary Data 내 해당 슬롯 갱신
     * ---------------------------------------------------------- */
    else if (paramIdx == AT_EDITOR_POS || paramIdx == AT_EDITOR_SCALE)
    {
        A_long choice = params[AT_EDITOR_PRESET]->u.pd.value;
        int    idx    = choice - 1;  /* 1-based → 0-based */

        if (idx >= 0 && idx < AT_NUM_PRESETS)
        {
            double rawX = FIX_2_FLOAT(params[AT_EDITOR_POS]->u.td.x_value);
            double rawY = FIX_2_FLOAT(params[AT_EDITOR_POS]->u.td.y_value);

            if (params[AT_GRID_UI])
            {
                if (!params[AT_GRID_UI]->u.arb_d.value)
                {
                    params[AT_GRID_UI]->u.arb_d.value = CreateDefaultGridPresetHandle(in_data);
                    if (!params[AT_GRID_UI]->u.arb_d.value) return PF_Err_OUT_OF_MEMORY;
                }
                PF_Handle h = params[AT_GRID_UI]->u.arb_d.value;
                GridUIPresetData* p = reinterpret_cast<GridUIPresetData*>(PF_LOCK_HANDLE(h));
                if (p)
                {
                    if (p->magic == GRID_ARB_MAGIC)
                    {
                        p->presetData.presets[idx].x     = rawX * (PRESET_BASE_W / (double)clipW);
                        p->presetData.presets[idx].y     = rawY * (PRESET_BASE_H / (double)clipH);
                        p->presetData.presets[idx].scale = (double)params[AT_EDITOR_SCALE]->u.fs_d.value;
                    }
                    PF_UNLOCK_HANDLE(h);

                    params[AT_GRID_UI]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
                    suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                        in_data->effect_ref, AT_GRID_UI, params[AT_GRID_UI]);
                }
            }

            out_data->out_flags |= PF_OutFlag_REFRESH_UI | PF_OutFlag_FORCE_RERENDER;
        }
    }

    /* ----------------------------------------------------------
     *  기존 Case: Easing Preset ↔ Ease In / Ease Out 슬라이더 동기화
     * ---------------------------------------------------------- */
    else if (paramIdx == AT_EASING_PRESET)
    {
        A_long    preset      = params[AT_EASING_PRESET]->u.pd.value;
        PF_FpShort targetIn  = 70.0f;
        PF_FpShort targetOut = 70.0f;
        bool       update    = true;

        switch (preset)
        {
        case EASE_PRESET_LINEAR:
            targetIn  = 0.0f;  targetOut = 0.0f;  break;
        case EASE_PRESET_EASE_IN:
            targetIn  = 70.0f; targetOut = 0.0f;  break;
        case EASE_PRESET_EASE_OUT:
            targetIn  = 0.0f;  targetOut = 70.0f; break;
        case EASE_PRESET_EASE_IN_OUT:
            targetIn  = 70.0f; targetOut = 70.0f; break;
        default:
            update = false;
            break;
        }

        if (update)
        {
            params[AT_EASE_IN]->u.fs_d.value  = targetIn;
            params[AT_EASE_IN]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;

            params[AT_EASE_OUT]->u.fs_d.value  = targetOut;
            params[AT_EASE_OUT]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;

            suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref, AT_EASE_IN, params[AT_EASE_IN]);
            suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref, AT_EASE_OUT, params[AT_EASE_OUT]);

            out_data->out_flags |= PF_OutFlag_REFRESH_UI | PF_OutFlag_FORCE_RERENDER;
        }
    }
    else if (paramIdx == AT_EASE_IN || paramIdx == AT_EASE_OUT)
    {
        if (params[AT_EASING_PRESET]->u.pd.value != EASE_PRESET_CUSTOM)
        {
            params[AT_EASING_PRESET]->u.pd.value = EASE_PRESET_CUSTOM;
            params[AT_EASING_PRESET]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;

            suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref, AT_EASING_PRESET, params[AT_EASING_PRESET]);

            out_data->out_flags |= PF_OutFlag_REFRESH_UI | PF_OutFlag_FORCE_RERENDER;
        }
    }

    SyncMaskModes(in_data, params);
    return err;
}

/* ================================================================
 *  Custom ECW Grid UI — 슬롯 매핑 테이블 (7행 × 7열)
 * ================================================================ */

static const GridSlot kGridSlots[7][7] = {
    // Row 0: Preset 1 (전체 7열 가로폭)
    { { GridSlotType::Preset, 1 }, { GridSlotType::Preset, 1 }, { GridSlotType::Preset, 1 }, { GridSlotType::Preset, 1 }, { GridSlotType::Preset, 1 }, { GridSlotType::Preset, 1 }, { GridSlotType::Preset, 1 } },
    // Row 1: Preset 2~8
    { { GridSlotType::Preset, 2 }, { GridSlotType::Preset, 3 }, { GridSlotType::Preset, 4 }, { GridSlotType::Preset, 5 }, { GridSlotType::Preset, 6 }, { GridSlotType::Preset, 7 }, { GridSlotType::Preset, 8 } },
    // Row 2: Preset 9~15
    { { GridSlotType::Preset, 9 }, { GridSlotType::Preset, 10 }, { GridSlotType::Preset, 11 }, { GridSlotType::Preset, 12 }, { GridSlotType::Preset, 13 }, { GridSlotType::Preset, 14 }, { GridSlotType::Preset, 15 } },
    // Row 3: 16, Special X1, 17, 18, 19, Special X2, 20
    { { GridSlotType::Preset, 16 }, { GridSlotType::Special, 1 }, { GridSlotType::Preset, 17 }, { GridSlotType::Preset, 18 }, { GridSlotType::Preset, 19 }, { GridSlotType::Special, 2 }, { GridSlotType::Preset, 20 } },
    // Row 4: Preset 21~27
    { { GridSlotType::Preset, 21 }, { GridSlotType::Preset, 22 }, { GridSlotType::Preset, 23 }, { GridSlotType::Preset, 24 }, { GridSlotType::Preset, 25 }, { GridSlotType::Preset, 26 }, { GridSlotType::Preset, 27 } },
    // Row 5: Preset 28~34
    { { GridSlotType::Preset, 28 }, { GridSlotType::Preset, 29 }, { GridSlotType::Preset, 30 }, { GridSlotType::Preset, 31 }, { GridSlotType::Preset, 32 }, { GridSlotType::Preset, 33 }, { GridSlotType::Preset, 34 } },
    // Row 6: Preset 35 (전체 7열 가로폭)
    { { GridSlotType::Preset, 35 }, { GridSlotType::Preset, 35 }, { GridSlotType::Preset, 35 }, { GridSlotType::Preset, 35 }, { GridSlotType::Preset, 35 }, { GridSlotType::Preset, 35 }, { GridSlotType::Preset, 35 } }
};

/* ================================================================
 *  Custom ECW Grid UI — 상태 판정 헬퍼
 * ================================================================ */

static int s_activeModifier = 0; // 0: 꺼짐, 1: X1 (<) 켜짐, 2: X2 (>) 켜짐

static UIStateInfo GetUIStateInfo(PF_ParamDef* params[], PF_InData* in_data)
{
    UIStateInfo state;
    PresetDataBlock effectPresets;
    GetEffectPresetData(in_data, params, &effectPresets);

    A_long clipW = params[AT_INPUT]->u.ld.width;
    A_long clipH = params[AT_INPUT]->u.ld.height;
    if (clipW <= 0) clipW = (in_data && in_data->width > 0) ? in_data->width : (A_long)PRESET_BASE_W;
    if (clipH <= 0) clipH = (in_data && in_data->height > 0) ? in_data->height : (A_long)PRESET_BASE_H;

    double offsetMult = (double)params[AT_MODIFIER_OFFSET_MULT]->u.fs_d.value;
    if (offsetMult < 0.0) offsetMult = 0.0;

    MatchResult startMatch = FindMatchingPresetExt(
        &effectPresets,
        params[AT_START_POSITION]->u.td.x_value,
        params[AT_START_POSITION]->u.td.y_value,
        params[AT_START_SCALE]->u.fs_d.value,
        clipW, clipH, offsetMult);

    MatchResult endMatch = FindMatchingPresetExt(
        &effectPresets,
        params[AT_END_POSITION]->u.td.x_value,
        params[AT_END_POSITION]->u.td.y_value,
        params[AT_END_SCALE]->u.fs_d.value,
        clipW, clipH, offsetMult);

    state.startIsCustom = (startMatch.presetId == 0);
    state.startPresetId = startMatch.presetId; // 1~35 (0: Custom)
    state.startMod      = startMatch.modifier;

    state.endIsCustom   = (endMatch.presetId == 0);
    state.endPresetId   = endMatch.presetId;     // 1~35 (0: Custom)
    state.endMod        = endMatch.modifier;

    double startPosX  = FIX_2_FLOAT(params[AT_START_POSITION]->u.td.x_value);
    double startPosY  = FIX_2_FLOAT(params[AT_START_POSITION]->u.td.y_value);
    double startScale = params[AT_START_SCALE]->u.fs_d.value;

    double endPosX    = FIX_2_FLOAT(params[AT_END_POSITION]->u.td.x_value);
    double endPosY    = FIX_2_FLOAT(params[AT_END_POSITION]->u.td.y_value);
    double endScale   = params[AT_END_SCALE]->u.fs_d.value;

    state.isTransformEqual = (fabs(startPosX - endPosX) < 0.5) &&
                             (fabs(startPosY - endPosY) < 0.5) &&
                             (fabs(startScale - endScale) < 0.05);

    return state;
}

static ButtonColorState GetPresetButtonColorState(int presetId, const UIStateInfo& uiState)
{
    bool isStart = (!uiState.startIsCustom && uiState.startPresetId == presetId);
    bool isEnd   = (!uiState.endIsCustom   && uiState.endPresetId   == presetId);

    if (isStart && isEnd)
    {
        // 모디파이어까지 완전히 같으면 완전 겹침(빨강), 모디파이어가 다르면 일부 겹침(파랑)
        return (uiState.startMod == uiState.endMod) ? ButtonColorState::ConflictRed
                                                    : ButtonColorState::CustomBlue;
    }
    if (isStart)
    {
        return ButtonColorState::StartYellow;
    }
    if (isEnd)
    {
        return ButtonColorState::EndGreen;
    }
    return ButtonColorState::Default;
}

static ButtonColorState GetModifierButtonColorState(int modId, const UIStateInfo& uiState)
{
    PresetModifier targetMod = (modId == 1) ? PresetModifier::X1 : PresetModifier::X2;
    bool isStart = (!uiState.startIsCustom && uiState.startMod == targetMod);
    bool isEnd   = (!uiState.endIsCustom   && uiState.endMod   == targetMod);

    if (isStart && isEnd)
    {
        // 프리셋 번호까지 동일하면 완전 겹침(빨강), 번호가 다르면 일부 겹침(파랑)
        return (uiState.startPresetId == uiState.endPresetId) ? ButtonColorState::ConflictRed
                                                              : ButtonColorState::CustomBlue;
    }
    if (isStart)
    {
        return ButtonColorState::StartYellow;
    }
    if (isEnd)
    {
        return ButtonColorState::EndGreen;
    }
    return ButtonColorState::Default;
}

static ButtonColorState GetCustomButtonColorState(const UIStateInfo& uiState)
{
    if (!uiState.startIsCustom && !uiState.endIsCustom)
    {
        // Start, End 둘 다 Preset
        return ButtonColorState::Default;
    }
    if (uiState.startIsCustom && !uiState.endIsCustom)
    {
        // Start만 Custom
        return ButtonColorState::StartYellow;
    }
    if (!uiState.startIsCustom && uiState.endIsCustom)
    {
        // End만 Custom
        return ButtonColorState::EndGreen;
    }
    // 둘 다 Custom인 경우
    if (uiState.isTransformEqual)
    {
        // 실제 좌표/스케일 동일
        return ButtonColorState::ConflictRed;
    }
    else
    {
        // 실제 좌표/스케일 다름
        return ButtonColorState::CustomBlue;
    }
}

static void ConvertAsciiToUTF16(const char* src, DRAWBOT_UTF16Char* dst, int maxLen)
{
    int i = 0;
    while (src[i] != '\0' && i < maxLen - 1)
    {
        dst[i] = (DRAWBOT_UTF16Char)(unsigned char)src[i];
        i++;
    }
    dst[i] = 0;
}

/* ================================================================
 *  Custom ECW Grid UI — 개별 버튼 드로잉 헬퍼
 * ================================================================ */

static void DrawSingleButton(
    DRAWBOT_Suites*     drawbotSuites,
    DRAWBOT_SurfaceRef  surface,
    DRAWBOT_SupplierRef supplier,
    DRAWBOT_FontRef     font,
    const DRAWBOT_RectF32& rect,
    ButtonColorState    state,
    const char*         label,
    bool                isToggled = false)
{
    DRAWBOT_ColorRGBA bgCol, borderCol, textCol;
    switch (state)
    {
    case ButtonColorState::StartYellow:
        bgCol     = { 0.92f, 0.74f, 0.12f, 1.0f };
        borderCol = { 1.00f, 0.84f, 0.25f, 1.0f };
        textCol   = { 0.10f, 0.10f, 0.10f, 1.0f };
        break;
    case ButtonColorState::EndGreen:
        bgCol     = { 0.18f, 0.72f, 0.34f, 1.0f };
        borderCol = { 0.28f, 0.85f, 0.44f, 1.0f };
        textCol   = { 1.00f, 1.00f, 1.00f, 1.0f };
        break;
    case ButtonColorState::ConflictRed:
        bgCol     = { 0.86f, 0.20f, 0.20f, 1.0f };
        borderCol = { 1.00f, 0.35f, 0.35f, 1.0f };
        textCol   = { 1.00f, 1.00f, 1.00f, 1.0f };
        break;
    case ButtonColorState::CustomBlue:
        bgCol     = { 0.20f, 0.50f, 0.90f, 1.0f };
        borderCol = { 0.35f, 0.65f, 1.00f, 1.0f };
        textCol   = { 1.00f, 1.00f, 1.00f, 1.0f };
        break;
    case ButtonColorState::DisabledSpecial:
        bgCol     = { 0.16f, 0.16f, 0.18f, 1.0f };
        borderCol = { 0.25f, 0.25f, 0.28f, 1.0f };
        textCol   = { 0.45f, 0.45f, 0.48f, 1.0f };
        break;
    case ButtonColorState::Default:
    default:
        bgCol     = { 0.22f, 0.22f, 0.24f, 1.0f };
        borderCol = { 0.34f, 0.34f, 0.38f, 1.0f };
        textCol   = { 0.85f, 0.85f, 0.85f, 1.0f };
        break;
    }

    DRAWBOT_PathRef  path = NULL;
    DRAWBOT_BrushRef bgBrush = NULL;
    DRAWBOT_PenRef   pen = NULL;
    DRAWBOT_BrushRef textBrush = NULL;

    if (drawbotSuites->supplier_suiteP->NewPath(supplier, &path) == PF_Err_NONE && path)
    {
        drawbotSuites->path_suiteP->AddRect(path, const_cast<DRAWBOT_RectF32*>(&rect));

        // 배경 채우기
        if (drawbotSuites->supplier_suiteP->NewBrush(supplier, &bgCol, &bgBrush) == PF_Err_NONE && bgBrush)
        {
            drawbotSuites->surface_suiteP->FillPath(surface, bgBrush, path, kDRAWBOT_FillType_Default);
            drawbotSuites->supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(bgBrush));
        }

        // 기본 테두리 그리기
        if (drawbotSuites->supplier_suiteP->NewPen(supplier, &borderCol, 1.0f, &pen) == PF_Err_NONE && pen)
        {
            drawbotSuites->surface_suiteP->StrokePath(surface, pen, path);
            drawbotSuites->supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(pen));
        }

        // 토글 활성화 상태 강조 (굵은 흰색 테두리)
        if (isToggled)
        {
            DRAWBOT_PenRef togglePen = NULL;
            DRAWBOT_ColorRGBA whiteCol = { 1.0f, 1.0f, 1.0f, 1.0f };
            if (drawbotSuites->supplier_suiteP->NewPen(supplier, &whiteCol, 2.0f, &togglePen) == PF_Err_NONE && togglePen)
            {
                drawbotSuites->surface_suiteP->StrokePath(surface, togglePen, path);
                drawbotSuites->supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(togglePen));
            }
        }

        drawbotSuites->supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(path));
    }

    // 텍스트 그리기
    if (label && label[0] != '\0')
    {
        DRAWBOT_UTF16Char uStr[32];
        ConvertAsciiToUTF16(label, uStr, 32);

        if (drawbotSuites->supplier_suiteP->NewBrush(supplier, &textCol, &textBrush) == PF_Err_NONE && textBrush)
        {
            DRAWBOT_PointF32 origin;
            origin.x = rect.left + rect.width * 0.5f;
            origin.y = rect.top + (rect.height * 0.5f) + 3.5f;

            drawbotSuites->surface_suiteP->DrawString(
                surface,
                textBrush,
                font,
                uStr,
                &origin,
                kDRAWBOT_TextAlignment_Center,
                kDRAWBOT_TextTruncation_None,
                0.0f);

            drawbotSuites->supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(textBrush));
        }
    }
}

/* ================================================================
 *  Custom ECW Grid UI — Draw Event
 * ================================================================ */

static PF_Err DrawEvent(
    PF_InData*     in_data,
    PF_OutData*    out_data,
    PF_ParamDef*   params[],
    PF_LayerDef*   output,
    PF_EventExtra* event_extra)
{
    PF_Err err = PF_Err_NONE;

    if (event_extra->effect_win.area != PF_EA_CONTROL)
    {
        return err;
    }
    if (event_extra->effect_win.index != AT_GRID_UI &&
        event_extra->effect_win.index != AT_UPDATE_BANNER) return err;

    DRAWBOT_Suites drawbotSuites;
    ERR(AEFX_AcquireDrawbotSuites(in_data, out_data, &drawbotSuites));

    PF_EffectCustomUISuite1* effectCustomUISuiteP = NULL;
    ERR(AEFX_AcquireSuite(
        in_data,
        out_data,
        kPFEffectCustomUISuite,
        kPFEffectCustomUISuiteVersion1,
        NULL,
        (void**)&effectCustomUISuiteP));

    DRAWBOT_DrawRef drawing_ref = NULL;
    if (!err && effectCustomUISuiteP)
    {
        ERR((*effectCustomUISuiteP->PF_GetDrawingReference)(event_extra->contextH, &drawing_ref));
        AEFX_ReleaseSuite(in_data, out_data, kPFEffectCustomUISuite, kPFEffectCustomUISuiteVersion1, NULL);
    }

    if (!err && drawing_ref)
    {
        DRAWBOT_SupplierRef supplier_ref = NULL;
        DRAWBOT_SurfaceRef  surface_ref  = NULL;
        DRAWBOT_FontRef     font_ref     = NULL;

        ERR(drawbotSuites.drawbot_suiteP->GetSupplier(drawing_ref, &supplier_ref));
        ERR(drawbotSuites.drawbot_suiteP->GetSurface(drawing_ref, &surface_ref));

        float fontSize = 10.0f;
        ERR(drawbotSuites.supplier_suiteP->NewDefaultFont(supplier_ref, fontSize, &font_ref));

        if (!err && supplier_ref && surface_ref && font_ref)
        {
            float originX = (float)event_extra->effect_win.current_frame.left;
            float originY = (float)event_extra->effect_win.current_frame.top;
            float totalW  = (float)(event_extra->effect_win.current_frame.right - event_extra->effect_win.current_frame.left);
            float totalH  = (float)(event_extra->effect_win.current_frame.bottom - event_extra->effect_win.current_frame.top);
            if (totalW < 180.0f) totalW = (float)UI_GRID_WIDTH;

            if (event_extra->effect_win.index == AT_UPDATE_BANNER)
            {
                DRAWBOT_RectF32 banner = {originX + 3.0f, originY + 2.0f,
                                         totalW - 6.0f, totalH - 4.0f};
                DrawSingleButton(&drawbotSuites, surface_ref, supplier_ref, font_ref,
                                 banner, ButtonColorState::CustomBlue,
                                 "Update available - GitHub");
            }
            else
            {
            UIStateInfo uiState = GetUIStateInfo(params, in_data);

            // 1. 호스트 패널 테마 배경색 취득 및 배경 채우기 (패널 일체화 투명화)
            DRAWBOT_ColorRGBA hostBgCol = { 0.15f, 0.15f, 0.15f, 1.0f };
            PFAppSuite4* app_suiteP = NULL;
            if (AEFX_AcquireSuite(in_data, out_data, kPFAppSuite, kPFAppSuiteVersion4, NULL, (void**)&app_suiteP) == PF_Err_NONE && app_suiteP)
            {
                PF_App_Color local_color = { 0, 0, 0 };
                if (app_suiteP->PF_AppGetBgColor(&local_color) == PF_Err_NONE)
                {
                    hostBgCol.red   = (float)local_color.red   / 65535.0f;
                    hostBgCol.green = (float)local_color.green / 65535.0f;
                    hostBgCol.blue  = (float)local_color.blue  / 65535.0f;
                    hostBgCol.alpha = 1.0f;
                }
                AEFX_ReleaseSuite(in_data, out_data, kPFAppSuite, kPFAppSuiteVersion4, NULL);
            }

            DRAWBOT_RectF32 bgRect;
            bgRect.left   = originX;
            bgRect.top    = originY;
            bgRect.width  = totalW;
            bgRect.height = totalH;

            DRAWBOT_PathRef bgPath = NULL;
            DRAWBOT_BrushRef bgBrush = NULL;
            if (drawbotSuites.supplier_suiteP->NewPath(supplier_ref, &bgPath) == PF_Err_NONE && bgPath)
            {
                drawbotSuites.path_suiteP->AddRect(bgPath, &bgRect);
                if (drawbotSuites.supplier_suiteP->NewBrush(supplier_ref, &hostBgCol, &bgBrush) == PF_Err_NONE && bgBrush)
                {
                    drawbotSuites.surface_suiteP->FillPath(surface_ref, bgBrush, bgPath, kDRAWBOT_FillType_Default);
                    drawbotSuites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(bgBrush));
                }
                drawbotSuites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(bgPath));
            }

            const float margin = 3.0f;
            const float gap    = 2.0f;
            const float availW = totalW - margin * 2.0f;
            const float colW   = (availW - gap * 6.0f) / 7.0f;
            const float rowH   = 20.0f;

            // Row 0: Preset 1 (전체 7열 가로폭)
            DRAWBOT_RectF32 rectR1;
            rectR1.left   = originX + margin;
            rectR1.top    = originY + margin;
            rectR1.width  = availW;
            rectR1.height = rowH;
            DrawSingleButton(&drawbotSuites, surface_ref, supplier_ref, font_ref, rectR1,
                             GetPresetButtonColorState(1, uiState), "1");

            // Rows 1~5: 일반 7열
            for (int r = 1; r <= 5; r++)
            {
                float y = originY + margin + (float)r * (rowH + gap);
                for (int c = 0; c < 7; c++)
                {
                    float x = originX + margin + (float)c * (colW + gap);
                    DRAWBOT_RectF32 cellRect;
                    cellRect.left   = x;
                    cellRect.top    = y;
                    cellRect.width  = colW;
                    cellRect.height = rowH;

                    GridSlot slot = kGridSlots[r][c];
                    if (slot.type == GridSlotType::Preset)
                    {
                        char buf[8];
                        snprintf(buf, sizeof(buf), "%d", slot.id);
                        DrawSingleButton(&drawbotSuites, surface_ref, supplier_ref, font_ref, cellRect,
                                         GetPresetButtonColorState(slot.id, uiState), buf);
                    }
                    else // Special X1 (<), X2 (>)
                    {
                        const char* label = (slot.id == 1) ? "<" : ">";
                        ButtonColorState modState = GetModifierButtonColorState(slot.id, uiState);
                        bool isToggled = (s_activeModifier == slot.id);
                        DrawSingleButton(&drawbotSuites, surface_ref, supplier_ref, font_ref, cellRect,
                                         modState, label, isToggled);
                    }
                }
            }

            // Row 6: Preset 35 (전체 7열 가로폭)
            float y6 = originY + margin + 6.0f * (rowH + gap);
            DRAWBOT_RectF32 rectR35;
            rectR35.left   = originX + margin;
            rectR35.top    = y6;
            rectR35.width  = availW;
            rectR35.height = rowH;
            DrawSingleButton(&drawbotSuites, surface_ref, supplier_ref, font_ref, rectR35,
                             GetPresetButtonColorState(35, uiState), "35");

            // Custom 상태 인디케이터 (우측 하단 텍스트: "● Custom")
            ButtonColorState customState = GetCustomButtonColorState(uiState);
            DRAWBOT_ColorRGBA customTextCol;
            switch (customState)
            {
            case ButtonColorState::StartYellow:
                customTextCol = { 0.95f, 0.76f, 0.12f, 1.0f };
                break;
            case ButtonColorState::EndGreen:
                customTextCol = { 0.20f, 0.78f, 0.38f, 1.0f };
                break;
            case ButtonColorState::ConflictRed:
                customTextCol = { 0.90f, 0.25f, 0.25f, 1.0f };
                break;
            case ButtonColorState::CustomBlue:
                customTextCol = { 0.25f, 0.55f, 0.95f, 1.0f };
                break;
            case ButtonColorState::Default:
            default:
                customTextCol = { 0.55f, 0.55f, 0.58f, 1.0f };
                break;
            }

            DRAWBOT_BrushRef customBrush = NULL;
            if (drawbotSuites.supplier_suiteP->NewBrush(supplier_ref, &customTextCol, &customBrush) == PF_Err_NONE && customBrush)
            {
                DRAWBOT_UTF16Char uCustom[16];
                uCustom[0] = 0x25CF; // '●'
                uCustom[1] = ' ';
                uCustom[2] = 'C';
                uCustom[3] = 'u';
                uCustom[4] = 's';
                uCustom[5] = 't';
                uCustom[6] = 'o';
                uCustom[7] = 'm';
                uCustom[8] = 0;

                DRAWBOT_PointF32 customOrigin;
                customOrigin.x = originX + margin + availW;
                customOrigin.y = y6 + rowH + 4.0f + (fontSize * 0.75f);

                drawbotSuites.surface_suiteP->DrawString(
                    surface_ref,
                    customBrush,
                    font_ref,
                    uCustom,
                    &customOrigin,
                    kDRAWBOT_TextAlignment_Right,
                    kDRAWBOT_TextTruncation_None,
                    0.0f);

                drawbotSuites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(customBrush));
            }
            }
            drawbotSuites.supplier_suiteP->ReleaseObject(reinterpret_cast<DRAWBOT_ObjectRef>(font_ref));
        }
    }

    AEFX_ReleaseDrawbotSuites(in_data, out_data);

    if (!err)
    {
        event_extra->evt_out_flags |= PF_EO_HANDLED_EVENT;
    }

    return err;
}

/* ================================================================
 *  Custom ECW Grid UI — Click Event
 * ================================================================ */

static PF_Err DoClick(
    PF_InData*     in_data,
    PF_OutData*    out_data,
    PF_ParamDef*   params[],
    PF_LayerDef*   output,
    PF_EventExtra* event_extra)
{
    PF_Err err = PF_Err_NONE;

    if (event_extra->effect_win.area != PF_EA_CONTROL)
    {
        return err;
    }

    if (event_extra->effect_win.index == AT_UPDATE_BANNER)
    {
        if (AT_IsUpdateAvailable()) AT_OpenLatestReleasePage();
        event_extra->evt_out_flags |= PF_EO_HANDLED_EVENT;
        return err;
    }
    if (event_extra->effect_win.index != AT_GRID_UI) return err;

    AEGP_SuiteHandler suites(in_data->pica_basicP);

    float mouseX = (float)event_extra->u.do_click.screen_point.h;
    float mouseY = (float)event_extra->u.do_click.screen_point.v;

    float originX = (float)event_extra->effect_win.current_frame.left;
    float originY = (float)event_extra->effect_win.current_frame.top;
    float totalW  = (float)(event_extra->effect_win.current_frame.right - event_extra->effect_win.current_frame.left);
    if (totalW < 180.0f) totalW = (float)UI_GRID_WIDTH;

    const float margin    = 3.0f;
    const float gap       = 2.0f;
    const float availW    = totalW - margin * 2.0f;
    const float colW      = (availW - gap * 6.0f) / 7.0f;
    const float rowH      = 20.0f;

    // 가용 영역 밖 클릭 무시
    if (mouseX < originX + margin || mouseX > originX + margin + availW)
    {
        return err;
    }

    int clickedPresetId = -1;

    // Row 0: Preset 1
    float y0 = originY + margin;
    if (mouseY >= y0 && mouseY <= y0 + rowH)
    {
        clickedPresetId = 1;
    }

    // Rows 1~5: 일반 슬롯
    for (int r = 1; r <= 5 && clickedPresetId == -1; r++)
    {
        float yr = originY + margin + (float)r * (rowH + gap);
        if (mouseY >= yr && mouseY <= yr + rowH)
        {
            for (int c = 0; c < 7; c++)
            {
                float xc = originX + margin + (float)c * (colW + gap);
                if (mouseX >= xc && mouseX <= xc + colW)
                {
                    GridSlot slot = kGridSlots[r][c];
                    if (slot.type == GridSlotType::Preset)
                    {
                        clickedPresetId = slot.id;
                    }
                    else if (slot.type == GridSlotType::Special)
                    {
                        // 토글 처리: 동일 버튼 클릭 시 해제(0), 다른 버튼 클릭 시 해당 번호(1 or 2)로 단일 배타적 전환
                        if (s_activeModifier == slot.id)
                        {
                            s_activeModifier = 0;
                        }
                        else
                        {
                            s_activeModifier = slot.id;
                        }

                        PF_Rect inval(event_extra->effect_win.current_frame);
                        suites.AppSuite4()->PF_InvalidateRect(event_extra->contextH, &inval);
                        event_extra->evt_out_flags |= PF_EO_HANDLED_EVENT | PF_EO_UPDATE_NOW;
                        out_data->out_flags |= PF_OutFlag_REFRESH_UI;
                        return err;
                    }
                    break;
                }
            }
        }
    }

    // Row 6: Preset 35
    float y6 = originY + margin + 6.0f * (rowH + gap);
    if (clickedPresetId == -1 && mouseY >= y6 && mouseY <= y6 + rowH)
    {
        clickedPresetId = 35;
    }

    // 유효한 프리셋이 클릭된 경우
    if (clickedPresetId >= 1 && clickedPresetId <= AT_NUM_PRESETS)
    {
        AutoInternalUpdateGuard guard;

        PresetDataBlock effectPresets;
        GetEffectPresetData(in_data, params, &effectPresets);
        int newIdx = clickedPresetId - 1; // 0-based

        A_long clipW = params[AT_INPUT]->u.ld.width;
        A_long clipH = params[AT_INPUT]->u.ld.height;
        if (clipW <= 0) clipW = (in_data->width  > 0) ? in_data->width  : (A_long)PRESET_BASE_W;
        if (clipH <= 0) clipH = (in_data->height > 0) ? in_data->height : (A_long)PRESET_BASE_H;

        double offsetMult = (double)params[AT_MODIFIER_OFFSET_MULT]->u.fs_d.value;
        if (offsetMult < 0.0) offsetMult = 0.0;

        bool moveMode = (params[AT_MOVE_MODE]->u.bd.value != 0);

        if (moveMode)
        {
            // End -> Start 복사
            params[AT_START_POSITION]->u.td.x_value = params[AT_END_POSITION]->u.td.x_value;
            params[AT_START_POSITION]->u.td.y_value = params[AT_END_POSITION]->u.td.y_value;
            params[AT_START_POSITION]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;

            params[AT_START_SCALE]->u.fs_d.value = params[AT_END_SCALE]->u.fs_d.value;
            params[AT_START_SCALE]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;

            suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref, AT_START_POSITION, params[AT_START_POSITION]);
            suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref, AT_START_SCALE, params[AT_START_SCALE]);
        }

        // 새 프리셋 -> End 적용 (모디파이어 오프셋 계산 포함)
        double basePresetX = effectPresets.presets[newIdx].x;
        double basePresetY = effectPresets.presets[newIdx].y;
        double baseScale   = effectPresets.presets[newIdx].scale;

        A_long code = 100 + clickedPresetId; // 기본 전체 화면: 100단위
        if (s_activeModifier == 1) // X1 (<) 좌측 이동: -(scale * offsetMult)
        {
            basePresetX -= (baseScale * offsetMult);
            code = clickedPresetId; // 0단위 (<)
        }
        else if (s_activeModifier == 2) // X2 (>) 우측 이동: +(scale * offsetMult)
        {
            basePresetX += (baseScale * offsetMult);
            code = 200 + clickedPresetId; // 200단위 (>)
        }

        // 오프셋 적용 후 모디파이어 토글 자동 해제
        s_activeModifier = 0;

        double xSet = (basePresetX / PRESET_BASE_W) * (double)clipW;
        double ySet = (basePresetY / PRESET_BASE_H) * (double)clipH;

        params[AT_END_POSITION]->u.td.x_value = FLOAT2FIX(xSet);
        params[AT_END_POSITION]->u.td.y_value = FLOAT2FIX(ySet);
        params[AT_END_POSITION]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;

        params[AT_END_SCALE]->u.fs_d.value = (PF_FpShort)baseScale;
        params[AT_END_SCALE]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;

        suites.ParamUtilsSuite3()->PF_UpdateParamUI(
            in_data->effect_ref, AT_END_POSITION, params[AT_END_POSITION]);
        suites.ParamUtilsSuite3()->PF_UpdateParamUI(
            in_data->effect_ref, AT_END_SCALE, params[AT_END_SCALE]);

        s_lastProgrammedPresetCode = code;
        params[AT_PRESET_CODE]->u.sd.value = code;
        params[AT_PRESET_CODE]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        suites.ParamUtilsSuite3()->PF_UpdateParamUI(
            in_data->effect_ref, AT_PRESET_CODE, params[AT_PRESET_CODE]);

        PF_Rect inval(event_extra->effect_win.current_frame);
        suites.AppSuite4()->PF_InvalidateRect(event_extra->contextH, &inval);

        event_extra->evt_out_flags |= PF_EO_HANDLED_EVENT | PF_EO_UPDATE_NOW;
        out_data->out_flags |= PF_OutFlag_REFRESH_UI | PF_OutFlag_FORCE_RERENDER;
    }

    SyncMaskModes(in_data, params);
    return err;
}

/* ================================================================
 *  Custom ECW Grid UI — Cursor Adjustment Event
 * ================================================================ */

static PF_Err ChangeCursor(
    PF_InData*     in_data,
    PF_OutData*    out_data,
    PF_ParamDef*   params[],
    PF_LayerDef*   output,
    PF_EventExtra* event_extra)
{
    if (event_extra->effect_win.area == PF_EA_CONTROL &&
        (event_extra->effect_win.index == AT_GRID_UI ||
         event_extra->effect_win.index == AT_UPDATE_BANNER))
    {
        event_extra->u.adjust_cursor.set_cursor = PF_Cursor_HAND;
    }
    return PF_Err_NONE;
}

static std::mutex s_updateBannerMutex;
static std::unordered_set<PF_ProgPtr> s_updateBannerShown;

static void RefreshUpdateBanner(PF_InData* in_data, PF_ParamDef* params[])
{
    if (!in_data || !in_data->pica_basicP || !params || !params[AT_UPDATE_BANNER]) return;
    if (!AT_IsUpdateAvailable()) return;
    {
        std::lock_guard<std::mutex> lock(s_updateBannerMutex);
        if (!s_updateBannerShown.insert(in_data->effect_ref).second) return;
    }
    PF_ParamDef banner = *params[AT_UPDATE_BANNER];
    if (banner.ui_flags & PF_PUI_INVISIBLE)
    {
        banner.ui_flags &= ~PF_PUI_INVISIBLE;
        AEGP_SuiteHandler suites(in_data->pica_basicP);
        if (suites.ParamUtilsSuite3()->PF_UpdateParamUI(
                in_data->effect_ref, AT_UPDATE_BANNER, &banner) != PF_Err_NONE)
        {
            std::lock_guard<std::mutex> lock(s_updateBannerMutex);
            s_updateBannerShown.erase(in_data->effect_ref);
        }
    }
}

static void ForgetUpdateBanner(PF_InData* in_data)
{
    if (!in_data) return;
    std::lock_guard<std::mutex> lock(s_updateBannerMutex);
    s_updateBannerShown.erase(in_data->effect_ref);
}

/* ================================================================
 *  HandleEvent — 커스텀 ECW UI 이벤트 디스패처
 * ================================================================ */

static PF_Err HandleEvent(
    PF_InData*     in_data,
    PF_OutData*    out_data,
    PF_ParamDef*   params[],
    PF_LayerDef*   output,
    PF_EventExtra* extra)
{
    PF_Err err = PF_Err_NONE;

    if (!extra) return err;

    switch (extra->e_type)
    {
    case PF_Event_DRAW:
        err = DrawEvent(in_data, out_data, params, output, extra);
        break;
    case PF_Event_DO_CLICK:
        err = DoClick(in_data, out_data, params, output, extra);
        break;
    case PF_Event_ADJUST_CURSOR:
        err = ChangeCursor(in_data, out_data, params, output, extra);
        break;
    case PF_Event_IDLE:
        RefreshUpdateBanner(in_data, params);
        break;
    default:
        break;
    }
    return err;
}

/* ================================================================
 *  HandleArbitrary — Grid UI 임의 데이터 생명주기 핸들러
 * ================================================================ */

static PF_Err HandleArbitrary(
    PF_InData*          in_data,
    PF_OutData*         out_data,
    PF_ParamDef*        params[],
    PF_LayerDef*        output,
    PF_ArbParamsExtra*  extra)
{
    PF_Err err = PF_Err_NONE;

    if (!extra) return PF_Err_BAD_CALLBACK_PARAM;

    switch (extra->which_function)
    {
    case PF_Arbitrary_NEW_FUNC:
        if (extra->u.new_func_params.refconPV != GRID_ARB_REFCON)
        {
            err = PF_Err_INTERNAL_STRUCT_DAMAGED;
        }
        else
        {
            PF_Handle h = CreateDefaultGridPresetHandle(in_data);
            if (h) *(extra->u.new_func_params.arbPH) = h;
            else err = PF_Err_OUT_OF_MEMORY;
        }
        break;

    case PF_Arbitrary_DISPOSE_FUNC:
        if (extra->u.dispose_func_params.refconPV != GRID_ARB_REFCON)
        {
            err = PF_Err_INTERNAL_STRUCT_DAMAGED;
        }
        else if (extra->u.dispose_func_params.arbH)
        {
            PF_DISPOSE_HANDLE(extra->u.dispose_func_params.arbH);
        }
        break;

    case PF_Arbitrary_COPY_FUNC:
        if (extra->u.copy_func_params.refconPV == GRID_ARB_REFCON)
        {
            PF_Handle h = PF_NEW_HANDLE(sizeof(GridUIPresetData));
            if (h)
            {
                if (extra->u.copy_func_params.src_arbH)
                {
                    void* src = PF_LOCK_HANDLE(extra->u.copy_func_params.src_arbH);
                    void* dst = PF_LOCK_HANDLE(h);
                    if (src && dst)
                    {
                        memcpy(dst, src, sizeof(GridUIPresetData));
                    }
                    if (src) PF_UNLOCK_HANDLE(extra->u.copy_func_params.src_arbH);
                    if (dst) PF_UNLOCK_HANDLE(h);
                }
                *(extra->u.copy_func_params.dst_arbPH) = h;
            }
            else
            {
                err = PF_Err_OUT_OF_MEMORY;
            }
        }
        break;

    case PF_Arbitrary_FLAT_SIZE_FUNC:
        *(extra->u.flat_size_func_params.flat_data_sizePLu) = sizeof(GridUIPresetData);
        break;

    case PF_Arbitrary_FLATTEN_FUNC:
        if (extra->u.flatten_func_params.buf_sizeLu == sizeof(GridUIPresetData))
        {
            void* src = PF_LOCK_HANDLE(extra->u.flatten_func_params.arbH);
            if (src)
            {
                memcpy(extra->u.flatten_func_params.flat_dataPV, src, sizeof(GridUIPresetData));
                PF_UNLOCK_HANDLE(extra->u.flatten_func_params.arbH);
            }
        }
        break;

    case PF_Arbitrary_UNFLATTEN_FUNC:
        if (extra->u.unflatten_func_params.buf_sizeLu == sizeof(GridUIPresetData))
        {
            PF_Handle h = PF_NEW_HANDLE(sizeof(GridUIPresetData));
            if (h)
            {
                GridUIPresetData* dst = reinterpret_cast<GridUIPresetData*>(PF_LOCK_HANDLE(h));
                if (dst)
                {
                    memcpy(dst, extra->u.unflatten_func_params.flat_dataPV, sizeof(GridUIPresetData));
                    if (dst->magic != GRID_ARB_MAGIC)
                    {
                        dst->magic   = GRID_ARB_MAGIC;
                        dst->version = GRID_ARB_VERSION;
                        GetDefaultPresets(&dst->presetData);
                    }
                    PF_UNLOCK_HANDLE(h);
                }
                *(extra->u.unflatten_func_params.arbPH) = h;
            }
        }
        break;

    case PF_Arbitrary_COMPARE_FUNC:
        if (extra->u.compare_func_params.compareP)
        {
            *(extra->u.compare_func_params.compareP) = PF_ArbCompare_EQUAL;
            if (extra->u.compare_func_params.a_arbH && extra->u.compare_func_params.b_arbH)
            {
                void* p1 = PF_LOCK_HANDLE(extra->u.compare_func_params.a_arbH);
                void* p2 = PF_LOCK_HANDLE(extra->u.compare_func_params.b_arbH);
                if (p1 && p2)
                {
                    if (memcmp(p1, p2, sizeof(GridUIPresetData)) != 0)
                    {
                        *(extra->u.compare_func_params.compareP) = PF_ArbCompare_NOT_EQUAL;
                    }
                }
                if (p1) PF_UNLOCK_HANDLE(extra->u.compare_func_params.a_arbH);
                if (p2) PF_UNLOCK_HANDLE(extra->u.compare_func_params.b_arbH);
            }
        }
        break;

    case PF_Arbitrary_PRINT_SIZE_FUNC:
        if (extra->u.print_size_func_params.print_sizePLu)
        {
            *(extra->u.print_size_func_params.print_sizePLu) = 16;
        }
        break;

    case PF_Arbitrary_PRINT_FUNC:
        if (extra->u.print_func_params.print_bufferPC)
        {
            snprintf(extra->u.print_func_params.print_bufferPC, 16, "GridUI");
        }
        break;

    default:
        break;
    }

    return err;
}

/* ================================================================
 *  Bilinear sampling helper (ARGB 8-bit)
 * ================================================================ */

static inline PF_Pixel8 SampleBilinear8(
    const PF_EffectWorld* src,
    PF_FpLong              srcX,
    PF_FpLong              srcY)
{
    PF_Pixel8 result = {0, 0, 0, 0};

    A_long x0 = (A_long)floor(srcX);
    A_long y0 = (A_long)floor(srcY);
    A_long x1 = x0 + 1;
    A_long y1 = y0 + 1;

    if (x1 < 0 || x0 >= src->width || y1 < 0 || y0 >= src->height)
    {
        return result;
    }

    A_long cx0 = (x0 < 0) ? 0 : x0;
    A_long cy0 = (y0 < 0) ? 0 : y0;
    A_long cx1 = (x1 >= src->width)  ? (src->width  - 1) : x1;
    A_long cy1 = (y1 >= src->height) ? (src->height - 1) : y1;

    PF_FpLong fx  = srcX - (PF_FpLong)x0;
    PF_FpLong fy  = srcY - (PF_FpLong)y0;
    PF_FpLong fx1 = 1.0 - fx;
    PF_FpLong fy1 = 1.0 - fy;

    PF_Pixel8* row0 = (PF_Pixel8*)((char*)src->data + cy0 * src->rowbytes);
    PF_Pixel8* row1 = (PF_Pixel8*)((char*)src->data + cy1 * src->rowbytes);

    PF_Pixel8 p00 = row0[cx0];
    PF_Pixel8 p10 = row0[cx1];
    PF_Pixel8 p01 = row1[cx0];
    PF_Pixel8 p11 = row1[cx1];

    PF_FpLong w00 = fx1 * fy1;
    PF_FpLong w10 = fx  * fy1;
    PF_FpLong w01 = fx1 * fy;
    PF_FpLong w11 = fx  * fy;

    result.alpha = (A_u_char)(p00.alpha * w00 + p10.alpha * w10 +
                              p01.alpha * w01 + p11.alpha * w11 + 0.5);
    result.red   = (A_u_char)(p00.red   * w00 + p10.red   * w10 +
                              p01.red   * w01 + p11.red   * w11 + 0.5);
    result.green = (A_u_char)(p00.green * w00 + p10.green * w10 +
                              p01.green * w01 + p11.green * w11 + 0.5);
    result.blue  = (A_u_char)(p00.blue  * w00 + p10.blue  * w10 +
                              p01.blue  * w01 + p11.blue  * w11 + 0.5);

    return result;
}

/* ================================================================
 *  Multi-threaded Pixel Processing Function
 * ================================================================ */

using TransformSample = ATCPU::TransformSample;

typedef struct TransformInfo
{
    const PF_EffectWorld* src;
    PF_FpLong             cx;
    PF_FpLong             cy;
    int                   numSamples;
    TransformSample       samples[32];
    PF_FpLong             cropLeft;
    PF_FpLong             cropRight;
} TransformInfo;

static PF_Err AutoTransformPixelFunc8(
    void*      refcon,
    A_long     xL,
    A_long     yL,
    PF_Pixel8* inP,
    PF_Pixel8* outP)
{
    TransformInfo* tiP = reinterpret_cast<TransformInfo*>(refcon);
    if (!tiP) return PF_Err_BAD_CALLBACK_PARAM;

    // 가시 영역([cropLeft, cropRight)) 밖인 경우 완전 투명 처리
    if ((PF_FpLong)xL < tiP->cropLeft || (PF_FpLong)xL >= tiP->cropRight)
    {
        outP->alpha = 0;
        outP->red   = 0;
        outP->green = 0;
        outP->blue  = 0;
        return PF_Err_NONE;
    }

    if (tiP->numSamples <= 1)
    {
        PF_FpLong srcX = tiP->cx + ((PF_FpLong)xL - tiP->samples[0].posX) / tiP->samples[0].scale;
        PF_FpLong srcY = tiP->cy + ((PF_FpLong)yL - tiP->samples[0].posY) / tiP->samples[0].scale;
        *outP = SampleBilinear8(tiP->src, srcX, srcY);
    }
    else
    {
        PF_FpLong accumA = 0.0, accumR = 0.0, accumG = 0.0, accumB = 0.0;
        for (int i = 0; i < tiP->numSamples; ++i)
        {
            PF_FpLong srcX = tiP->cx + ((PF_FpLong)xL - tiP->samples[i].posX) / tiP->samples[i].scale;
            PF_FpLong srcY = tiP->cy + ((PF_FpLong)yL - tiP->samples[i].posY) / tiP->samples[i].scale;
            PF_Pixel8 pix  = SampleBilinear8(tiP->src, srcX, srcY);
            accumA += pix.alpha;
            accumR += pix.red;
            accumG += pix.green;
            accumB += pix.blue;
        }
        PF_FpLong invN = 1.0 / (PF_FpLong)tiP->numSamples;
        outP->alpha = (A_u_char)(accumA * invN + 0.5);
        outP->red   = (A_u_char)(accumR * invN + 0.5);
        outP->green = (A_u_char)(accumG * invN + 0.5);
        outP->blue  = (A_u_char)(accumB * invN + 0.5);
    }

    return PF_Err_NONE;
}

/* ================================================================
 *  Render (시간 계산 유지, Premiere CPU는 BGRA/VUYA 32f 처리)
 * ================================================================ */

struct TransformFloatRowContext
{
    const TransformInfo* transform;
    PF_EffectWorld* output;
    PF_InData* inData;
};

static PF_Err RenderFloatRow(void* refcon, A_long threadIndex, A_long y, A_long)
{
    const auto& context = *static_cast<TransformFloatRowContext*>(refcon);
    if (threadIndex == 0) {
        PF_Err err = PF_ABORT(context.inData);
        if (err) return err;
    }
    const auto& ti = *context.transform;
    auto* row = reinterpret_cast<ATCPU::Pixel32f*>(
        reinterpret_cast<char*>(context.output->data) +
        static_cast<std::ptrdiff_t>(y) * context.output->rowbytes);
    for (A_long x = 0; x < context.output->width; ++x) {
        row[x] = ATCPU::TransformPixel(ti.src->data, ti.src->rowbytes,
            ti.src->width, ti.src->height, ti.cx, ti.cy, ti.samples,
            ti.numSamples, ti.cropLeft, ti.cropRight, x, y);
    }
    return PF_Err_NONE;
}

static PF_Err Render(
    PF_InData*   in_data,
    PF_OutData*  out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output)
{
    PF_Err err = PF_Err_NONE;

    PF_EffectWorld* input = &params[AT_INPUT]->u.ld;

    // AE's PF_PixelFloat is ARGB; Premiere's advertised formats are BGRA/VUYA.
    // Validate both worlds before using the explicit 16-byte float row renderer.
    const bool premiere = in_data->appl_id == 'PrMr';
    if (premiere)
    {
        AEFX_SuiteScoper<PF_PixelFormatSuite1> pixelFormatSuite(
            in_data, kPFPixelFormatSuite, kPFPixelFormatSuiteVersion1, out_data);
        PrPixelFormat sourceFormat = PrPixelFormat_Invalid;
        PrPixelFormat destinationFormat = PrPixelFormat_Invalid;
        ERR(pixelFormatSuite->GetPixelFormat(input, &sourceFormat));
        ERR(pixelFormatSuite->GetPixelFormat(output, &destinationFormat));
        if (err) return err;
        if (sourceFormat != destinationFormat ||
            (sourceFormat != PrPixelFormat_BGRA_4444_32f &&
             sourceFormat != PrPixelFormat_VUYA_4444_32f))
            return PF_Err_BAD_CALLBACK_PARAM;
    }

    A_long width  = input->width;
    A_long height = input->height;

    /* --- 1. Time calculation --- */

    A_long currentFrame = 0;
    if (in_data->time_step > 0)
    {
        currentFrame = in_data->current_time / in_data->time_step;
    }

    if (in_data->pica_basicP)
    {
        PF_UtilitySuite* utilSuite = NULL;
        PF_Err acquireErr = in_data->pica_basicP->AcquireSuite(
            kPFUtilitySuite,
            kPFUtilitySuiteVersion,
            (const void**)&utilSuite);

        if (acquireErr == PF_Err_NONE && utilSuite)
        {
            bool frameCalculated = false;

            if (utilSuite->GetSequenceTime && utilSuite->GetTrackItemStart)
            {
                PrTime seqTime      = 0;
                A_long trackItemStart = 0;
                if (utilSuite->GetSequenceTime(in_data->effect_ref, &seqTime) == PF_Err_NONE &&
                    utilSuite->GetTrackItemStart(in_data->effect_ref, &trackItemStart) == PF_Err_NONE)
                {
                    A_long seqFrame = in_data->time_step > 0
                        ? (A_long)(seqTime / in_data->time_step) : 0;
                    currentFrame    = seqFrame - trackItemStart;
                    frameCalculated = true;
                }
            }

            if (!frameCalculated && utilSuite->GetMediaTimecode2)
            {
                A_long mediaFrame   = 0;
                PF_TimeDisplay timeDisplay = {};
                if (utilSuite->GetMediaTimecode2(
                        in_data->effect_ref, true, &mediaFrame, &timeDisplay) == PF_Err_NONE)
                {
                    currentFrame    = mediaFrame;
                    frameCalculated = true;
                }
            }

            if (!frameCalculated)
            {
                bool isSynthetic = false;
                if (utilSuite->IsTrackItemEffectAppliedToSynthetic)
                {
                    utilSuite->IsTrackItemEffectAppliedToSynthetic(
                        in_data->effect_ref, &isSynthetic);
                }

                if (!isSynthetic && utilSuite->GetClipStart)
                {
                    A_long clipStart = 0;
                    if (utilSuite->GetClipStart(in_data->effect_ref, &clipStart) == PF_Err_NONE)
                    {
                        currentFrame -= clipStart;
                    }
                }
            }

            if (currentFrame < 0) currentFrame = 0;

            in_data->pica_basicP->ReleaseSuite(kPFUtilitySuite, kPFUtilitySuiteVersion);
        }
    }

    PF_FpLong duration = params[AT_DURATION]->u.fs_d.value;

    PF_FpLong t = 1.0;
    if (duration > 0.0)
    {
        t = (PF_FpLong)currentFrame / duration;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
    }

    /* --- 2. Easing Progress (Cubic-Bezier) --- */

    A_long    preset     = params[AT_EASING_PRESET]->u.pd.value;
    PF_FpLong easeInVal  = 0.0;
    PF_FpLong easeOutVal = 0.0;

    switch (preset)
    {
    case EASE_PRESET_LINEAR:
        easeInVal  = 0.0;  easeOutVal = 0.0;  break;
    case EASE_PRESET_EASE_IN:
        easeInVal  = 0.70; easeOutVal = 0.0;  break;
    case EASE_PRESET_EASE_OUT:
        easeInVal  = 0.0;  easeOutVal = 0.70; break;
    case EASE_PRESET_EASE_IN_OUT:
        easeInVal  = 0.70; easeOutVal = 0.70; break;
    case EASE_PRESET_CUSTOM:
    default:
        easeInVal  = params[AT_EASE_IN]->u.fs_d.value  / 100.0;
        easeOutVal = params[AT_EASE_OUT]->u.fs_d.value / 100.0;
        break;
    }

    if (easeInVal  < 0.0) easeInVal  = 0.0;
    if (easeInVal  > 1.0) easeInVal  = 1.0;
    if (easeOutVal < 0.0) easeOutVal = 0.0;
    if (easeOutVal > 1.0) easeOutVal = 1.0;

    PF_FpLong p1 = (1.0 / 3.0) * (1.0 - easeInVal);
    PF_FpLong p2 = (2.0 / 3.0) + (1.0 / 3.0) * easeOutVal;

    PF_FpLong oneMinusT = 1.0 - t;
    PF_FpLong easedT    = 3.0 * oneMinusT * oneMinusT * t * p1 +
                          3.0 * oneMinusT * t * t * p2 +
                          t * t * t;
    if (easedT < 0.0) easedT = 0.0;
    if (easedT > 1.0) easedT = 1.0;

    /* --- 3. Position parameters (Absolute Pixel Coordinates) --- */

    PF_FpLong startPosX = FIX_2_FLOAT(params[AT_START_POSITION]->u.td.x_value);
    PF_FpLong startPosY = FIX_2_FLOAT(params[AT_START_POSITION]->u.td.y_value);
    PF_FpLong endPosX   = FIX_2_FLOAT(params[AT_END_POSITION]->u.td.x_value);
    PF_FpLong endPosY   = FIX_2_FLOAT(params[AT_END_POSITION]->u.td.y_value);

    /* --- 4. Scale parameters --- */

    PF_FpLong startScale = params[AT_START_SCALE]->u.fs_d.value / 100.0;
    PF_FpLong endScale   = params[AT_END_SCALE]->u.fs_d.value   / 100.0;

    /* --- 5. Interpolate with Shutter Angle Motion Blur --- */

    PF_FpLong shutterAngle = params[AT_SHUTTER_ANGLE]->u.fs_d.value;
    A_long    samplesCount = params[AT_SAMPLES]->u.sd.value;
    if (samplesCount < SAMPLES_MIN) samplesCount = SAMPLES_MIN;
    if (samplesCount > SAMPLES_MAX) samplesCount = SAMPLES_MAX;

    TransformInfo ti;
    ti.src = input;
    ti.cx  = (PF_FpLong)width  / 2.0;
    ti.cy  = (PF_FpLong)height / 2.0;

    if (shutterAngle <= 0.0 || duration <= 0.0)
    {
        ti.numSamples = 1;
        PF_FpLong posX  = startPosX + (endPosX - startPosX) * easedT;
        PF_FpLong posY  = startPosY + (endPosY - startPosY) * easedT;
        PF_FpLong scale = startScale + (endScale - startScale) * easedT;
        if (scale < 0.001) scale = 0.001;
        ti.samples[0].posX  = posX;
        ti.samples[0].posY  = posY;
        ti.samples[0].scale = scale;
    }
    else
    {
        ti.numSamples = (int)samplesCount;
        PF_FpLong deltaF = shutterAngle / 360.0;
        PF_FpLong fStart = (PF_FpLong)currentFrame - deltaF * 0.5;

        for (int i = 0; i < ti.numSamples; ++i)
        {
            PF_FpLong subFrame = fStart + ((PF_FpLong)i + 0.5) / (PF_FpLong)ti.numSamples * deltaF;
            PF_FpLong subT     = subFrame / duration;
            if (subT < 0.0) subT = 0.0;
            if (subT > 1.0) subT = 1.0;

            PF_FpLong subOneMinusT = 1.0 - subT;
            PF_FpLong subEasedT    = 3.0 * subOneMinusT * subOneMinusT * subT * p1 +
                                     3.0 * subOneMinusT * subT * subT * p2 +
                                     subT * subT * subT;
            if (subEasedT < 0.0) subEasedT = 0.0;
            if (subEasedT > 1.0) subEasedT = 1.0;

            PF_FpLong sX     = startPosX + (endPosX - startPosX) * subEasedT;
            PF_FpLong sY     = startPosY + (endPosY - startPosY) * subEasedT;
            PF_FpLong sScale = startScale + (endScale - startScale) * subEasedT;
            if (sScale < 0.001) sScale = 0.001;

            ti.samples[i].posX  = sX;
            ti.samples[i].posY  = sY;
            ti.samples[i].scale = sScale;
        }
    }

    /* --- 6. Crop / Mask calculation for < and > modifiers --- */
    PresetDataBlock effectPresets;
    GetEffectPresetData(in_data, params, &effectPresets);

    double offsetMult = (double)params[AT_MODIFIER_OFFSET_MULT]->u.fs_d.value;
    if (offsetMult < 0.0) offsetMult = 0.0;

    MatchResult startMatch = FindMatchingPresetExt(
        &effectPresets,
        params[AT_START_POSITION]->u.td.x_value,
        params[AT_START_POSITION]->u.td.y_value,
        params[AT_START_SCALE]->u.fs_d.value,
        width, height, offsetMult);

    MatchResult endMatch = FindMatchingPresetExt(
        &effectPresets,
        params[AT_END_POSITION]->u.td.x_value,
        params[AT_END_POSITION]->u.td.y_value,
        params[AT_END_SCALE]->u.fs_d.value,
        width, height, offsetMult);

    PF_FpLong halfW = (PF_FpLong)width * 0.5;

    PF_FpLong startL = (startMatch.modifier == PresetModifier::X2) ? halfW : 0.0;
    PF_FpLong startR = (startMatch.modifier == PresetModifier::X1) ? halfW : (PF_FpLong)width;

    PF_FpLong endL   = (endMatch.modifier == PresetModifier::X2)   ? halfW : 0.0;
    PF_FpLong endR   = (endMatch.modifier == PresetModifier::X1)   ? halfW : (PF_FpLong)width;

    ti.cropLeft  = startL + (endL - startL) * easedT;
    ti.cropRight = startR + (endR - startR) * easedT;

    // 전체에서 좌우 모드로 가려질 때 4px 여유 마진으로 시작해서 0px로 수렴 (최종 0px 오버랩)
    if (startMatch.modifier == PresetModifier::None && endMatch.modifier != PresetModifier::None)
    {
        PF_FpLong bleed = 4.0 * (1.0 - easedT);
        if (endMatch.modifier == PresetModifier::X1)
        {
            ti.cropRight += bleed;
        }
        else if (endMatch.modifier == PresetModifier::X2)
        {
            ti.cropLeft -= bleed;
        }
    }
    else if (startMatch.modifier != PresetModifier::None && endMatch.modifier == PresetModifier::None)
    {
        PF_FpLong bleed = 4.0 * easedT;
        if (startMatch.modifier == PresetModifier::X1)
        {
            ti.cropRight += bleed;
        }
        else if (startMatch.modifier == PresetModifier::X2)
        {
            ti.cropLeft -= bleed;
        }
    }

    if (ti.cropLeft  < 0.0) ti.cropLeft = 0.0;
    if (ti.cropRight > (PF_FpLong)width) ti.cropRight = (PF_FpLong)width;

    AEGP_SuiteHandler suites(in_data->pica_basicP);
    if (premiere)
    {
        TransformFloatRowContext context = { &ti, output, in_data };
        return suites.Iterate8Suite2()->iterate_generic(
            output->height, &context, RenderFloatRow);
    }
    A_long linesL = output->extent_hint.bottom - output->extent_hint.top;

    ERR(suites.Iterate8Suite2()->iterate(
        in_data,
        0,
        linesL,
        input,
        NULL,
        (void*)&ti,
        AutoTransformPixelFunc8,
        output));

    return err;
}

/* ================================================================
 *  UpdateParamsUI — 호스트 UI 갱신 시 Preset Code 동기화
 * ================================================================ */

static PF_Err UpdateParamsUI(
    PF_InData*   in_data,
    PF_OutData*  out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output)
{
    PF_Err err = PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);

    AT_StartUpdateCheck(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION);
    RefreshUpdateBanner(in_data, params);

    A_long clipW = params[AT_INPUT]->u.ld.width;
    A_long clipH = params[AT_INPUT]->u.ld.height;
    if (clipW <= 0) clipW = (in_data->width  > 0) ? in_data->width  : (A_long)PRESET_BASE_W;
    if (clipH <= 0) clipH = (in_data->height > 0) ? in_data->height : (A_long)PRESET_BASE_H;

    double offsetMult = (double)params[AT_MODIFIER_OFFSET_MULT]->u.fs_d.value;
    if (offsetMult < 0.0) offsetMult = 0.0;

    PresetDataBlock effectPresets;
    GetEffectPresetData(in_data, params, &effectPresets);
    MatchResult endMatch = FindMatchingPresetExt(
        &effectPresets,
        params[AT_END_POSITION]->u.td.x_value,
        params[AT_END_POSITION]->u.td.y_value,
        params[AT_END_SCALE]->u.fs_d.value,
        clipW, clipH, offsetMult);

    A_long expectedCode = 0;
    if (endMatch.presetId >= 1 && endMatch.presetId <= AT_NUM_PRESETS)
    {
        if (endMatch.modifier == PresetModifier::X1)      expectedCode = endMatch.presetId;       // 0단위 (<)
        else if (endMatch.modifier == PresetModifier::None) expectedCode = 100 + endMatch.presetId; // 100단위 (기본)
        else if (endMatch.modifier == PresetModifier::X2) expectedCode = 200 + endMatch.presetId; // 200단위 (>)
    }

    if (params[AT_PRESET_CODE]->u.sd.value != expectedCode)
    {
        AutoInternalUpdateGuard guard;
        s_lastProgrammedPresetCode = expectedCode;
        params[AT_PRESET_CODE]->u.sd.value = expectedCode;
        params[AT_PRESET_CODE]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
        suites.ParamUtilsSuite3()->PF_UpdateParamUI(
            in_data->effect_ref, AT_PRESET_CODE, params[AT_PRESET_CODE]);
    }

    return err;
}

/* ================================================================
 *  PluginDataEntryFunction2 (AE 16.0+ export)
 * ================================================================ */

extern "C" DllExport PF_Err PluginDataEntryFunction2(
    PF_PluginDataPtr inPtr,
    PF_PluginDataCB2 inPluginDataCallBackPtr,
    SPBasicSuite*    inSPBasicSuitePtr,
    const char*      inHostName,
    const char*      inHostVersion)
{
    PF_Err result = PF_Err_INVALID_CALLBACK;

    result = PF_REGISTER_EFFECT_EXT2(
        inPtr,
        inPluginDataCallBackPtr,
        "MasterDual Transform",     // Name
        "AutoTransform",            // Match Name
        "Transform",                // Category
        AE_RESERVED_INFO,           // Reserved Info
        "EffectMain",               // Entry point
        "https://github.com");      // Support URL

    return result;
}

/* ================================================================
 *  EffectMain (classic entry point)
 * ================================================================ */

PF_Err EffectMain(
    PF_Cmd       cmd,
    PF_InData*   in_data,
    PF_OutData*  out_data,
    PF_ParamDef* params[],
    PF_LayerDef* output,
    void*        extra)
{
    PF_Err err = PF_Err_NONE;

    try
    {
        switch (cmd)
        {
        case PF_Cmd_ABOUT:
            err = About(in_data, out_data, params, output);
            break;

        case PF_Cmd_GLOBAL_SETUP:
            err = GlobalSetup(in_data, out_data, params, output);
            break;

        case PF_Cmd_PARAMS_SETUP:
            err = ParamsSetup(in_data, out_data, params, output);
            break;

        case PF_Cmd_SEQUENCE_SETUP:
        case PF_Cmd_SEQUENCE_SETDOWN:
            ForgetUpdateBanner(in_data);
            break;

        case PF_Cmd_UPDATE_PARAMS_UI:
            err = UpdateParamsUI(in_data, out_data, params, output);
            break;

        case PF_Cmd_USER_CHANGED_PARAM:
        {
            const PF_UserChangedParamExtra* uextra = reinterpret_cast<const PF_UserChangedParamExtra*>(extra);
            err = UserChangedParam(
                in_data, out_data, params, output,
                uextra);
            break;
        }

        case PF_Cmd_RENDER:
            err = Render(in_data, out_data, params, output);
            break;

        case PF_Cmd_EVENT:
        {
            PF_EventExtra* eextra = reinterpret_cast<PF_EventExtra*>(extra);
            err = HandleEvent(
                in_data, out_data, params, output,
                eextra);
            break;
        }

        case PF_Cmd_ARBITRARY_CALLBACK:
            err = HandleArbitrary(
                in_data, out_data, params, output,
                reinterpret_cast<PF_ArbParamsExtra*>(extra));
            break;
        }
    }
    catch (PF_Err& thrown_err)
    {
        err = thrown_err;
    }
    return err;
}
