/*******************************************************************/
/*                                                                 */
/*  AutoTransform_Strings.h                                        */
/*                                                                 */
/*******************************************************************/

#pragma once

typedef enum
{
    StrID_NONE,
    StrID_Name,
    StrID_Description,

    /* ---- 신규: Preset UI & Transform ---- */
    StrID_Topic_Transform,
    StrID_Topic_PresetSettings,
    StrID_OffsetMult_Name,
    StrID_GridUI_Name,
    StrID_PresetCode_Name,
    StrID_Topic_Preset,
    StrID_StartPreset_Name,
    StrID_EndPreset_Name,
    StrID_MoveMode_Name,
    StrID_Topic_Editor,
    StrID_EditorPreset_Name,
    StrID_EditorPos_Name,
    StrID_EditorScale_Name,
    StrID_Preset_Choices,       // "Custom|01|02|...|35" (36 items)
    StrID_EditorPreset_Choices, // "01|02|...|35"        (35 items)

    /* ---- 기존: Timing & Easing ---- */
    StrID_Duration_Name,
    StrID_Topic_Timing,
    StrID_Easing_Preset_Name,
    StrID_Easing_Choices,
    StrID_EaseIn_Name,
    StrID_EaseOut_Name,

    /* ---- 기존: Motion Blur ---- */
    StrID_Topic_MotionBlur,
    StrID_ShutterAngle_Name,
    StrID_Samples_Name,

    /* ---- 기존: 하단 4개 ---- */
    StrID_StartPos_Name,
    StrID_EndPos_Name,
    StrID_StartScale_Name,
    StrID_EndScale_Name,

    StrID_NUMTYPES
} StrIDType;
