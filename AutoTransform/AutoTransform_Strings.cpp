/*******************************************************************/
/*                                                                 */
/*  AutoTransform_Strings.cpp                                      */
/*                                                                 */
/*******************************************************************/

#include "AutoTransform.h"

typedef struct
{
    A_u_long index;
    A_char str[512];
} TableString;

TableString g_strs[StrID_NUMTYPES] = {
    { StrID_NONE,
      "" },
    { StrID_Name,
      "MasterDuel Transform" },
    { StrID_Description,
#if defined(AE_OS_WIN) || defined(_WIN32)
      "MasterDuel Transform v0.1.3 (Windows)\r개발자: 참혈\r유튜브: https://www.youtube.com/@참혈\rGitHub: https://github.com/Chamhyul/Masterduel-Transform"
#else
      "MasterDuel Transform v0.1.3 (macOS)\r개발자: 참혈\r유튜브: https://www.youtube.com/@참혈\rGitHub: https://github.com/Chamhyul/Masterduel-Transform"
#endif
    },
    { StrID_UpdateBanner_Name,
      "Update Available" },

    /* ---- 신규: Preset UI & Transform ---- */
    { StrID_Topic_Transform,
      "Transform" },
    { StrID_Topic_PresetSettings,
      "Preset Settings" },
    { StrID_OffsetMult_Name,
      "Modifier Offset Multiplier" },
    { StrID_GridUI_Name,
      "Buttons" },
    { StrID_PresetCode_Name,
      "Preset Code" },
    { StrID_Topic_Preset,
      "Preset Settings" },
    { StrID_StartPreset_Name,
      "Start Preset" },
    { StrID_EndPreset_Name,
      "End Preset" },
    { StrID_MoveMode_Name,
      "Move Mode" },
    { StrID_Topic_Editor,
      "Preset Editor" },
    { StrID_EditorPreset_Name,
      "Preset" },
    { StrID_EditorPos_Name,
      "Position" },
    { StrID_EditorScale_Name,
      "Scale" },
    /* Start/End Preset 드롭다운: Custom(1) + Preset 01~35(2~36) = 36개 */
    { StrID_Preset_Choices,
      "Custom|01|02|03|04|05|06|07|08|09|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|32|33|34|35" },
    /* Editor Preset 드롭다운: Preset 01~35(1~35) = 35개 */
    { StrID_EditorPreset_Choices,
      "01|02|03|04|05|06|07|08|09|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|32|33|34|35" },

    /* ---- 기존: Timing & Easing ---- */
    { StrID_Duration_Name,
      "Duration (frames)" },
    { StrID_Topic_Timing,
      "Timing & Easing" },
    { StrID_Easing_Preset_Name,
      "Easing Preset" },
    { StrID_Easing_Choices,
      "Linear|Ease In|Ease Out|Ease In & Out|Custom" },
    { StrID_EaseIn_Name,
      "Ease In (%)" },
    { StrID_EaseOut_Name,
      "Ease Out (%)" },

    /* ---- 기존: Motion Blur ---- */
    { StrID_Topic_MotionBlur,
      "Motion Blur" },
    { StrID_ShutterAngle_Name,
      "Shutter Angle" },
    { StrID_Samples_Name,
      "Samples" },

    /* ---- 기존: 하단 4개 ---- */
    { StrID_StartPos_Name,
      "Start Position" },
    { StrID_EndPos_Name,
      "End Position" },
    { StrID_StartScale_Name,
      "Start Scale" },
    { StrID_EndScale_Name,
      "End Scale" },
};

char* GetStringPtr(int strNum)
{
    return g_strs[strNum].str;
}
