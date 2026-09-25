/*******************************************************************/
/*                                                                 */
/*  AutoTransform_GPU_Win.cpp                                      */
/*                                                                 */
/*  Windows GPU accelerated rendering implementation for           */
/*  Premiere Pro Mercury Playback Engine (MPE).                    */
/*  Supports NVIDIA CUDA and AMD/Intel OpenCL.                     */
/*  Includes Shutter Angle Motion Blur with Subframe Integration.  */
/*                                                                 */
/*******************************************************************/

#if _WIN32

#include <windows.h>
#include <math.h>
#include <sstream>
#include <vector>
#include <string>

// CUDA SDK headers (must be included before AutoTransform.h to avoid MAJOR_VERSION macro conflict)
#include <cuda_runtime.h>

// OpenCL SDK headers
#include <CL/cl.h>

#include "AutoTransform.h"
#include "PrGPUFilterModule.h"
#include "PrSDKSequenceInfoSuite.h"
#include "DirectXUtils.h"
#include "AutoTransform_CSO.h"

#define HAS_CUDA    1
#define HAS_OPENCL  1
#define HAS_DIRECTX 1

// 서브샘플 구조체 (AutoTransform.cu 및 AutoTransform.metal과 100% 일치)
struct TransformSample
{
    float posX;
    float posY;
    float scale;
};

// 커널 파라미터 구조체 (AutoTransform.cu 및 AutoTransform.metal과 100% 일치)
struct AutoTransformParams
{
    int   srcPitch;   // 소스 버퍼 행 피치 (픽셀 단위)
    int   destPitch;  // 대상 버퍼 행 피치 (픽셀 단위)
    int   is16f;      // 16비트 부동소수점 여부 (0: 32비트, 1: 16비트)
    int   width;      // 프레임 가로 크기
    int   height;     // 프레임 세로 크기
    int   numSamples; // 서브샘플 수 (1 ~ 32)
    float cx;         // 레이어 중심점 X
    float cy;         // 레이어 중심점 Y
    float cropLeft;   // 가시 영역 왼쪽 경계 (픽셀 단위)
    float cropRight;  // 가시 영역 오른쪽 경계 (픽셀 단위)
};

// DirectX 12 전용 구조체 (16바이트 정렬 및 32개 서브샘플 내장)
struct TransformSampleDX
{
    float posX;
    float posY;
    float scale;
    float _pad;
};

struct AutoTransformParamsDX
{
    int   srcPitch;
    int   destPitch;
    int   is16f;
    int   width;

    int   height;
    int   numSamples;
    float cx;
    float cy;

    float cropLeft;
    float cropRight;
    float pad0;
    float pad1;

    TransformSampleDX samples[32];
};

// CUDA 커널 호스트 래퍼 선언 (AutoTransform.cu에 정의)
extern "C" void AutoTransform_CUDA(
    const void*                 srcBuf,
    void*                       destBuf,
    const AutoTransformParams*  params,
    const TransformSample*      d_samples,
    cudaStream_t                stream);

// =========================================================================
// OpenCL 임베디드 커널 소스 (외부 파일 의존성 없는 JIT 런타임 컴파일)
// =========================================================================
static const char* kEmbeddedOpenCLSource = R"CLC(
#ifdef cl_khr_fp16
#pragma OPENCL EXTENSION cl_khr_fp16 : enable
#endif

typedef struct
{
    float posX;
    float posY;
    float scale;
} TransformSample;

typedef struct
{
    int   srcPitch;
    int   destPitch;
    int   is16f;
    int   width;
    int   height;
    int   numSamples;
    float cx;
    float cy;
    float cropLeft;
    float cropRight;
} AutoTransformParams;

static inline float4 ReadPixelCL(
    __global const void* src,
    int                  pitch,
    int                  x,
    int                  y,
    int                  width,
    int                  height,
    int                  is16f)
{
    if (x < 0 || x >= width || y < 0 || y >= height)
    {
        return (float4)(0.0f, 0.0f, 0.0f, 0.0f);
    }
    int index = y * pitch + x;
    if (is16f)
    {
        __global const half* ptr = (__global const half*)src;
        return vload_half4(index, ptr);
    }
    else
    {
        __global const float4* ptr = (__global const float4*)src;
        return ptr[index];
    }
}

static inline void WritePixelCL(
    __global void* dest,
    int            pitch,
    int            x,
    int            y,
    float4         value,
    int            is16f)
{
    int index = y * pitch + x;
    if (is16f)
    {
        __global half* ptr = (__global half*)dest;
        vstore_half4_rtz(value, index, ptr);
    }
    else
    {
        __global float4* ptr = (__global float4*)dest;
        ptr[index] = value;
    }
}

static inline float4 SampleBilinearCL(
    __global const void* src,
    int                  pitch,
    float                srcX,
    float                srcY,
    int                  width,
    int                  height,
    int                  is16f)
{
    if (srcX < -1.0f || srcX >= (float)width ||
        srcY < -1.0f || srcY >= (float)height)
    {
        return (float4)(0.0f, 0.0f, 0.0f, 0.0f);
    }

    int x0 = (int)floor(srcX);
    int y0 = (int)floor(srcY);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float fx  = srcX - (float)x0;
    float fy  = srcY - (float)y0;
    float fx1 = 1.0f - fx;
    float fy1 = 1.0f - fy;

    float4 p00 = ReadPixelCL(src, pitch, x0, y0, width, height, is16f);
    float4 p10 = ReadPixelCL(src, pitch, x1, y0, width, height, is16f);
    float4 p01 = ReadPixelCL(src, pitch, x0, y1, width, height, is16f);
    float4 p11 = ReadPixelCL(src, pitch, x1, y1, width, height, is16f);

    return p00 * (fx1 * fy1) +
           p10 * (fx  * fy1) +
           p01 * (fx1 * fy ) +
           p11 * (fx  * fy );
}

__kernel void kAutoTransformKernelCL(
    __global const void*               src,
    __global void*                     dest,
    __constant AutoTransformParams*    params,
    __global const TransformSample*    samples)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= params->width || y >= params->height)
    {
        return;
    }

    if ((float)x < params->cropLeft || (float)x >= params->cropRight)
    {
        WritePixelCL(dest, params->destPitch, x, y, (float4)(0.0f, 0.0f, 0.0f, 0.0f), params->is16f);
        return;
    }

    if (params->numSamples <= 1)
    {
        float safeScale = (samples[0].scale > 0.0001f) ? samples[0].scale : 0.0001f;
        float srcX = params->cx + ((float)x - samples[0].posX) / safeScale;
        float srcY = params->cy + ((float)y - samples[0].posY) / safeScale;

        float4 result = SampleBilinearCL(src, params->srcPitch, srcX, srcY, params->width, params->height, params->is16f);
        WritePixelCL(dest, params->destPitch, x, y, result, params->is16f);
    }
    else
    {
        float4 accum = (float4)(0.0f, 0.0f, 0.0f, 0.0f);

        for (int i = 0; i < params->numSamples; ++i)
        {
            float safeScale = (samples[i].scale > 0.0001f) ? samples[i].scale : 0.0001f;
            float srcX = params->cx + ((float)x - samples[i].posX) / safeScale;
            float srcY = params->cy + ((float)y - samples[i].posY) / safeScale;

            accum += SampleBilinearCL(src, params->srcPitch, srcX, srcY, params->width, params->height, params->is16f);
        }

        float4 result = accum / (float)params->numSamples;
        WritePixelCL(dest, params->destPitch, x, y, result, params->is16f);
    }
}
)CLC";

enum { kMaxDevices = 16 };
static cl_kernel sOpenCLKernelCache[kMaxDevices] = {};
static std::vector<DXContextPtr> sDXContextCache;
static std::vector<ShaderObjectPtr> sDXShaderObjectCache;

static inline size_t RoundUpMultiple(size_t inVal, size_t inMult)
{
    return inMult ? ((inVal + inMult - 1) / inMult) * inMult : inVal;
}

// =========================================================================
// [통합 InPoint 탐색 아키텍처 - AGENT.md 5장 검증 완료 로직 100% 보존]
// =========================================================================

static bool DoesNodeContainTarget(
    PrSDKVideoSegmentSuite* segmentSuite,
    csSDK_int32 currentNodeID,
    csSDK_int32 targetEffectNodeID,
    csSDK_int32 targetOwnerNodeID,
    int depth = 0)
{
    if (!segmentSuite || currentNodeID == 0 || depth > 8)
    {
        return false;
    }

    if (currentNodeID == targetEffectNodeID || (targetOwnerNodeID != 0 && currentNodeID == targetOwnerNodeID))
    {
        return true;
    }

    // 1. 오퍼레이터 노드 순회 (이펙트 등)
    csSDK_int32 opCount = 0;
    if (segmentSuite->GetNodeOperatorCount(currentNodeID, &opCount) == suiteError_NoError && opCount > 0)
    {
        for (int i = 0; i < opCount; ++i)
        {
            csSDK_int32 opNodeID = 0;
            if (segmentSuite->AcquireOperatorNodeID(currentNodeID, i, &opNodeID) == suiteError_NoError && opNodeID != 0)
            {
                bool found = (opNodeID == targetEffectNodeID || (targetOwnerNodeID != 0 && opNodeID == targetOwnerNodeID));
                if (!found)
                {
                    found = DoesNodeContainTarget(segmentSuite, opNodeID, targetEffectNodeID, targetOwnerNodeID, depth + 1);
                }
                segmentSuite->ReleaseVideoNodeID(opNodeID);
                if (found)
                {
                    return true;
                }
            }
        }
    }

    // 2. 입력 노드 순회 (레이어, 소스 클립 등)
    csSDK_int32 inputCount = 0;
    if (segmentSuite->GetNodeInputCount(currentNodeID, &inputCount) == suiteError_NoError && inputCount > 0)
    {
        for (int i = 0; i < inputCount; ++i)
        {
            PrTime inOffset = 0;
            csSDK_int32 inputNodeID = 0;
            if (segmentSuite->AcquireInputNodeID(currentNodeID, i, &inOffset, &inputNodeID) == suiteError_NoError && inputNodeID != 0)
            {
                bool found = (inputNodeID == targetEffectNodeID || (targetOwnerNodeID != 0 && inputNodeID == targetOwnerNodeID));
                if (!found)
                {
                    found = DoesNodeContainTarget(segmentSuite, inputNodeID, targetEffectNodeID, targetOwnerNodeID, depth + 1);
                }
                segmentSuite->ReleaseVideoNodeID(inputNodeID);
                if (found)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

static bool FindInPointTicksInHierarchy(
    PrSDKVideoSegmentSuite* segmentSuite,
    PrSDKMemoryManagerSuite* memSuite,
    csSDK_int32 currentNodeID,
    PrTime* outInPointTicks,
    int depth = 0)
{
    if (!segmentSuite || !memSuite || currentNodeID == 0 || depth > 8 || !outInPointTicks)
    {
        return false;
    }

    char nodeType[256] = {0};
    prPluginID hash = {};
    csSDK_int32 flags = 0;
    segmentSuite->GetNodeInfo(currentNodeID, nodeType, &hash, &flags);

    // RenderableNodeMediaImpl 노드 발견 시 InPointMediaTimeAsTicks 조회
    if (strstr(nodeType, "Media") != nullptr)
    {
        PrMemoryPtr inPointBuf = nullptr;
        if (segmentSuite->GetNodeProperty(currentNodeID, "MediaNode::InPointMediaTimeAsTicks", &inPointBuf) == suiteError_NoError && inPointBuf)
        {
            std::istringstream stream((const char*)inPointBuf);
            stream >> *outInPointTicks;
            memSuite->PrDisposePtr(inPointBuf);
            return true;
        }
    }

    // 하위 입력 노드(Input Node) 순회
    csSDK_int32 inCount = 0;
    if (segmentSuite->GetNodeInputCount(currentNodeID, &inCount) == suiteError_NoError && inCount > 0)
    {
        for (int i = 0; i < inCount; ++i)
        {
            PrTime offset = 0;
            csSDK_int32 inID = 0;
            if (segmentSuite->AcquireInputNodeID(currentNodeID, i, &offset, &inID) == suiteError_NoError && inID != 0)
            {
                bool found = FindInPointTicksInHierarchy(segmentSuite, memSuite, inID, outInPointTicks, depth + 1);
                segmentSuite->ReleaseVideoNodeID(inID);
                if (found) return true;
            }
        }
    }

    return false;
}

static PrTime GetHeadTransitionDurationTicks(
    PrSDKVideoSegmentSuite* segmentSuite,
    PrSDKMemoryManagerSuite* memSuite,
    csSDK_int32 clipNodeID)
{
    if (!segmentSuite || !memSuite || clipNodeID == 0)
    {
        return 0;
    }

    PrTime trackStart = -1;
    PrTime effectiveStart = -1;

    PrMemoryPtr propBuffer = nullptr;
    if (segmentSuite->GetNodeProperty(clipNodeID, "ClipNode::TrackItemStartAsTicks", &propBuffer) == suiteError_NoError && propBuffer)
    {
        std::istringstream stream((const char*)propBuffer);
        stream >> trackStart;
        memSuite->PrDisposePtr(propBuffer);
    }

    propBuffer = nullptr;
    if (segmentSuite->GetNodeProperty(clipNodeID, "ClipNode::EffectiveTrackItemStartAsTicks", &propBuffer) == suiteError_NoError && propBuffer)
    {
        std::istringstream stream((const char*)propBuffer);
        stream >> effectiveStart;
        memSuite->PrDisposePtr(propBuffer);
    }

    if (trackStart > 0 && effectiveStart > 0 && effectiveStart < trackStart)
    {
        return (trackStart - effectiveStart);
    }

    return 0;
}

static bool ExtractFromAdjustmentNode(
    PrSDKVideoSegmentSuite* segmentSuite,
    PrSDKMemoryManagerSuite* memSuite,
    csSDK_int32 adjNodeID,
    PrTime* outInPointTicks,
    PrTime* outHeadTransitionTicks = nullptr,
    bool isInsideTransition = false)
{
    if (!segmentSuite || !memSuite || adjNodeID == 0 || !outInPointTicks) return false;

    PrTime transTicks = GetHeadTransitionDurationTicks(segmentSuite, memSuite, adjNodeID);

    csSDK_int32 subCount = 0;
    segmentSuite->GetNodeInputCount(adjNodeID, &subCount);

    prPluginID hash = {};
    csSDK_int32 flags = 0;
    PrTime clipNodeOffset = 0;

    // 하위 입력을 순회하여 ClipNode(트랜지션 길이)와 MediaNode(InPoint)를 모두 탐색
    for (int si = 0; si < subCount; ++si)
    {
        PrTime siOff = 0;
        csSDK_int32 siID = 0;
        if (segmentSuite->AcquireInputNodeID(adjNodeID, si, &siOff, &siID) == suiteError_NoError && siID != 0)
        {
            char siType[256] = {0};
            segmentSuite->GetNodeInfo(siID, siType, &hash, &flags);

            if (strstr(siType, "Clip") != nullptr)
            {
                PrTime ct = GetHeadTransitionDurationTicks(segmentSuite, memSuite, siID);
                if (ct > 0 && transTicks == 0) transTicks = ct;
                clipNodeOffset = siOff;
            }

            if (strstr(siType, "Media") != nullptr && *outInPointTicks == 0)
            {
                PrMemoryPtr inPointBuf = nullptr;
                if (segmentSuite->GetNodeProperty(siID, "MediaNode::InPointMediaTimeAsTicks", &inPointBuf) == suiteError_NoError && inPointBuf)
                {
                    std::istringstream stream((const char*)inPointBuf);
                    stream >> *outInPointTicks;
                    memSuite->PrDisposePtr(inPointBuf);
                }
            }

            segmentSuite->ReleaseVideoNodeID(siID);
        }
    }

    if (isInsideTransition && transTicks == 0 && *outInPointTicks > 0 && clipNodeOffset < 0)
    {
        PrTime absClipOff = -clipNodeOffset;
        if (*outInPointTicks > absClipOff)
        {
            transTicks = *outInPointTicks - absClipOff;
        }
    }

    if (outHeadTransitionTicks) *outHeadTransitionTicks = transTicks;

    return (*outInPointTicks > 0);
}

// Match the live effect to an operator on the adjustment layer's own Clip.
// If the runtime property is unavailable, do not select another layer.
static bool AdjustmentOwnsEffect(
    PrSDKVideoSegmentSuite* suite, PrSDKMemoryManagerSuite* memory,
    csSDK_int32 adjustment, PrTime targetInstance)
{
    if (!suite || !memory || targetInstance <= 0) return false;
    csSDK_int32 count = 0;
    if (suite->GetNodeInputCount(adjustment, &count) != suiteError_NoError) return false;
    for (int i = count - 1; i >= 0; --i) {
        PrTime offset = 0;
        csSDK_int32 child = 0;
        if (suite->AcquireInputNodeID(adjustment, i, &offset, &child) != suiteError_NoError || !child) continue;
        char type[256] = {};
        prPluginID hash = {};
        csSDK_int32 flags = 0;
        suite->GetNodeInfo(child, type, &hash, &flags);
        bool matches = false;
        // Only the adjustment's own Clip operators; never descend into background.
        if (strstr(type, "Clip") != nullptr) {
            csSDK_int32 operators = 0;
            suite->GetNodeOperatorCount(child, &operators);
            for (int j = 0; j < operators; ++j) {
                csSDK_int32 op = 0;
                if (suite->AcquireOperatorNodeID(child, j, &op) != suiteError_NoError || !op) continue;
                PrMemoryPtr value = nullptr;
                if (suite->GetNodeProperty(op, "EffectNode::RuntimeInstanceID", &value) == suiteError_NoError && value) {
                    PrTime instance = 0;
                    std::istringstream stream((const char*)value);
                    if ((stream >> instance) && instance == targetInstance) matches = true;
                    memory->PrDisposePtr(value);
                }
                suite->ReleaseVideoNodeID(op);
                if (matches) break;
            }
        }
        suite->ReleaseVideoNodeID(child);
        if (matches) return true;
    }
    return false;
}

static PrTime GetEffectRuntimeInstance(
    PrSDKVideoSegmentSuite* suite, PrSDKMemoryManagerSuite* memory, csSDK_int32 effect)
{
    if (!suite || !memory) return 0;
    PrMemoryPtr value = nullptr;
    PrTime instance = 0;
    if (suite->GetNodeProperty(effect, "EffectNode::RuntimeInstanceID", &value) == suiteError_NoError && value) {
        std::istringstream stream((const char*)value);
        if (!(stream >> instance)) instance = 0;
        memory->PrDisposePtr(value);
    }
    return instance;
}

static bool FindAdjustmentLayerInPoint(
    PrSDKVideoSegmentSuite* segmentSuite,
    PrSDKMemoryManagerSuite* memSuite,
    csSDK_int32 segNodeID,
    PrTime targetInstance,
    PrTime* outInPointTicks,
    PrTime* outHeadTransitionTicks = nullptr,
    PrTime* outLayerOffsetTicks = nullptr)
{
    if (!segmentSuite || !memSuite || segNodeID == 0 || !outInPointTicks || targetInstance <= 0)
    {
        return false;
    }

    *outInPointTicks = 0;
    if (outHeadTransitionTicks) *outHeadTransitionTicks = 0;
    if (outLayerOffsetTicks) *outLayerOffsetTicks = 0;

    csSDK_int32 inCount = 0;
    if (segmentSuite->GetNodeInputCount(segNodeID, &inCount) == suiteError_NoError && inCount > 0)
    {
        for (int i = 0; i < inCount; ++i)
        {
            PrTime layerOff = 0;
            csSDK_int32 layerID = 0;
            if (segmentSuite->AcquireInputNodeID(segNodeID, i, &layerOff, &layerID) == suiteError_NoError && layerID != 0)
            {
                char lType[256] = {0};
                prPluginID hash = {};
                csSDK_int32 flags = 0;
                segmentSuite->GetNodeInfo(layerID, lType, &hash, &flags);


                // 1) 직접 조정 레이어(RenderableNode_AdjustmentImpl) 노드 발견 시
                if (strstr(lType, "Adjustment") != nullptr)
                {
                    if (AdjustmentOwnsEffect(segmentSuite, memSuite, layerID, targetInstance) &&
                        ExtractFromAdjustmentNode(segmentSuite, memSuite, layerID, outInPointTicks, outHeadTransitionTicks))
                    {
                        if (outLayerOffsetTicks) *outLayerOffsetTicks = 0;
                        segmentSuite->ReleaseVideoNodeID(layerID);
                        return true;
                    }
                }
                // 2) 전환 노드(RenderableNodeTransitionImpl) 발견 시 -> 내부 입력에서 후속 조정 레이어 탐색
                else if (strstr(lType, "Transition") != nullptr)
                {
                    // Search both sides, accepting only the current effect owner.
                    csSDK_int32 transInCount = 0;
                    if (segmentSuite->GetNodeInputCount(layerID, &transInCount) == suiteError_NoError && transInCount > 0)
                    {
                        for (int ti = transInCount - 1; ti >= 0; --ti)
                        {
                            PrTime tiOff = 0;
                            csSDK_int32 tiID = 0;
                            if (segmentSuite->AcquireInputNodeID(layerID, ti, &tiOff, &tiID) == suiteError_NoError && tiID != 0)
                            {
                                char tiType[256] = {0};
                                segmentSuite->GetNodeInfo(tiID, tiType, &hash, &flags);

                                csSDK_int32 targetAdjID = 0;
                                bool needReleaseTarget = false;

                                if (strstr(tiType, "Adjustment") != nullptr)
                                {
                                    targetAdjID = tiID;
                                }
                                else
                                {
                                    // Clip 노드 하위에 Adjustment 노드가 있는지 확인
                                    csSDK_int32 clipSubCount = 0;
                                    segmentSuite->GetNodeInputCount(tiID, &clipSubCount);
                                    for (int ci = 0; ci < clipSubCount; ++ci)
                                    {
                                        PrTime cOff = 0;
                                        csSDK_int32 cID = 0;
                                        if (segmentSuite->AcquireInputNodeID(tiID, ci, &cOff, &cID) == suiteError_NoError && cID != 0)
                                        {
                                            char cType[256] = {0};
                                            segmentSuite->GetNodeInfo(cID, cType, &hash, &flags);
                                            if (strstr(cType, "Adjustment") != nullptr)
                                            {
                                                targetAdjID = cID;
                                                needReleaseTarget = true;
                                                break;
                                            }
                                            segmentSuite->ReleaseVideoNodeID(cID);
                                        }
                                    }
                                }

                                if (targetAdjID != 0)
                                {
                                    PrTime subInPoint = 0;
                                    PrTime subTrans = 0;
                                    if (AdjustmentOwnsEffect(segmentSuite, memSuite, targetAdjID, targetInstance) &&
                                        ExtractFromAdjustmentNode(segmentSuite, memSuite, targetAdjID, &subInPoint, &subTrans, ti == 1))
                                    {
                                        *outInPointTicks = subInPoint;
                                        if (outLayerOffsetTicks) *outLayerOffsetTicks = tiOff;

                                        // Only the incoming side may derive head timing from this transition.
                                        // Do not assume half of a transition duration (alignment can vary).
                                        PrTime resolvedHeadTrans = ti == 1 ? subTrans : 0;
                                        if (outHeadTransitionTicks) *outHeadTransitionTicks = resolvedHeadTrans;

                                        if (needReleaseTarget) segmentSuite->ReleaseVideoNodeID(targetAdjID);
                                        segmentSuite->ReleaseVideoNodeID(tiID);
                                        segmentSuite->ReleaseVideoNodeID(layerID);
                                        return true;
                                    }
                                    if (needReleaseTarget) segmentSuite->ReleaseVideoNodeID(targetAdjID);
                                }

                                segmentSuite->ReleaseVideoNodeID(tiID);
                            }
                        }
                    }
                }
                segmentSuite->ReleaseVideoNodeID(layerID);
            }
        }
    }

    return false;
}

class AutoTransformGPUFilter : public PrGPUFilterBase
{
public:
    AutoTransformGPUFilter()
        : mCUDATempBuffer(nullptr)
        , mCUDATempBufferSize(0)
        , mCUDASampleBuffer(nullptr)
        , mCUDASampleBufferSize(0)
        , mCLTempBuffer(nullptr)
        , mCLTempBufferSize(0)
        , mCLSampleBuffer(nullptr)
        , mCLSampleBufferSize(0)
        , mCLParamBuffer(nullptr)
        , mCLParamBufferSize(0)
        , mCLCommandQueue(nullptr)
        , mCLKernel(nullptr)
    {
    }

    virtual ~AutoTransformGPUFilter()
    {
        FreeBuffers();
    }

    void FreeBuffers()
    {
#if HAS_CUDA
        if (mCUDATempBuffer)
        {
            cudaFree(mCUDATempBuffer);
            mCUDATempBuffer = nullptr;
            mCUDATempBufferSize = 0;
        }
        if (mCUDASampleBuffer)
        {
            cudaFree(mCUDASampleBuffer);
            mCUDASampleBuffer = nullptr;
            mCUDASampleBufferSize = 0;
        }
#endif

#if HAS_OPENCL
        if (mCLTempBuffer)
        {
            clReleaseMemObject(mCLTempBuffer);
            mCLTempBuffer = nullptr;
            mCLTempBufferSize = 0;
        }
        if (mCLSampleBuffer)
        {
            clReleaseMemObject(mCLSampleBuffer);
            mCLSampleBuffer = nullptr;
            mCLSampleBufferSize = 0;
        }
        if (mCLParamBuffer)
        {
            clReleaseMemObject(mCLParamBuffer);
            mCLParamBuffer = nullptr;
            mCLParamBufferSize = 0;
        }
#endif

#if HAS_DIRECTX
        if (mDXTempBuffer)
        {
            mDXTempBuffer.Reset();
            mDXTempBufferSize = 0;
        }
#endif
    }

    virtual prSuiteError Initialize(PrGPUFilterInstance* ioInstanceData) override
    {
        PrGPUFilterBase::Initialize(ioInstanceData);

        if (mSuites && mSuites->utilFuncs && mSuites->utilFuncs->getSPBasicSuite())
        {
            mSuites->utilFuncs->getSPBasicSuite()->AcquireSuite(
                kPrSDKSequenceInfoSuite,
                kPrSDKSequenceInfoSuiteVersion,
                (const void**)&mSequenceInfoSuite);
        }

        if (mDeviceIndex >= kMaxDevices)
        {
            return suiteError_Fail;
        }

        if (mDeviceInfo.outDeviceFramework == PrGPUDeviceFramework_CUDA)
        {
#if HAS_CUDA
            // CUDA 커널은 정적 링크되므로 특별한 런타임 초기화 불필요
            return suiteError_NoError;
#else
            return suiteError_Fail;
#endif
        }
        else if (mDeviceInfo.outDeviceFramework == PrGPUDeviceFramework_OpenCL)
        {
#if HAS_OPENCL
            mCLCommandQueue = (cl_command_queue)mDeviceInfo.outCommandQueueHandle;
            mCLKernel = sOpenCLKernelCache[mDeviceIndex];

            if (!mCLKernel)
            {
                cl_int result = CL_SUCCESS;
                size_t srcLen = strlen(kEmbeddedOpenCLSource);
                cl_context ctx = (cl_context)mDeviceInfo.outContextHandle;
                cl_device_id dev = (cl_device_id)mDeviceInfo.outDeviceHandle;

                cl_program program = clCreateProgramWithSource(ctx, 1, &kEmbeddedOpenCLSource, &srcLen, &result);
                if (result != CL_SUCCESS || !program)
                {
                    return suiteError_Fail;
                }

                result = clBuildProgram(program, 1, &dev, "-cl-single-precision-constant -cl-fast-relaxed-math", nullptr, nullptr);
                if (result != CL_SUCCESS)
                {
                    clReleaseProgram(program);
                    return suiteError_Fail;
                }

                mCLKernel = clCreateKernel(program, "kAutoTransformKernelCL", &result);
                clReleaseProgram(program);

                if (result != CL_SUCCESS || !mCLKernel)
                {
                    return suiteError_Fail;
                }

                sOpenCLKernelCache[mDeviceIndex] = mCLKernel;
            }
            return suiteError_NoError;
#else
            return suiteError_Fail;
#endif
        }
        else if (mDeviceInfo.outDeviceFramework == PrGPUDeviceFramework_DirectX)
        {
#if HAS_DIRECTX
            if (mDeviceIndex >= (csSDK_int32)sDXContextCache.size())
            {
                sDXContextCache.resize(mDeviceIndex + 1);
                sDXShaderObjectCache.resize(mDeviceIndex + 1);
            }
            if (!sDXContextCache[mDeviceIndex])
            {
                DXContextPtr dxContext = std::make_shared<DXContext>();
                if (!dxContext->Initialize(
                    (ID3D12Device*)mDeviceInfo.outDeviceHandle,
                    (ID3D12CommandQueue*)mDeviceInfo.outCommandQueueHandle))
                {
                    return suiteError_Fail;
                }

                ShaderObjectPtr shaderObj = std::make_shared<ShaderObject>();

                // 1) 외부 폴더에 컴파일된 셰이더 파일이 있는지 먼저 확인
                std::wstring csoPath, sigPath;
                bool loaded = false;
                if (GetShaderPath(L"AutoTransform", csoPath, sigPath))
                {
                    loaded = dxContext->LoadShader(csoPath.c_str(), sigPath.c_str(), shaderObj);
                }

                // 2) 외부 파일이 없으면 임베디드 바이트코드(AutoTransform_CSO.h)로부터 직접 생성 (무결점 단일 바이너리)
                if (!loaded)
                {
                    loaded = dxContext->LoadShaderFromMemory(
                        kAutoTransformCSO,
                        kAutoTransformCSOSize,
                        kAutoTransformRS,
                        kAutoTransformRSSize,
                        shaderObj);
                }

                if (!loaded)
                {
                    return suiteError_Fail;
                }

                sDXShaderObjectCache[mDeviceIndex] = shaderObj;
                sDXContextCache[mDeviceIndex] = dxContext;
            }
            return suiteError_NoError;
#else
            return suiteError_Fail;
#endif
        }

        return suiteError_NotImplemented;
    }

    virtual prSuiteError Render(
        const PrGPUFilterRenderParams* inRenderParams,
        const PPixHand*                inFrames,
        csSDK_size_t                   inFrameCount,
        PPixHand*                      outFrame) override
    {
        if (!outFrame || !*outFrame)
        {
            return suiteError_InvalidParms;
        }

        if (mDeviceInfo.outDeviceFramework != PrGPUDeviceFramework_CUDA &&
            mDeviceInfo.outDeviceFramework != PrGPUDeviceFramework_OpenCL &&
            mDeviceInfo.outDeviceFramework != PrGPUDeviceFramework_DirectX)
        {
            return suiteError_NotImplemented;
        }

        void* destFrameData = nullptr;
        mGPUDeviceSuite->GetGPUPPixData(*outFrame, &destFrameData);
        if (!destFrameData)
        {
            return suiteError_Fail;
        }

        PrPixelFormat pixelFormat = PrPixelFormat_Invalid;
        mPPixSuite->GetPixelFormat(*outFrame, &pixelFormat);

        prRect bounds = {};
        mPPixSuite->GetBounds(*outFrame, &bounds);
        int width  = bounds.right - bounds.left;
        int height = bounds.bottom - bounds.top;

        csSDK_int32 destRowBytes = 0;
        mPPixSuite->GetRowBytes(*outFrame, &destRowBytes);

        int bytesPerPixel = GetGPUBytesPerPixel(pixelFormat);
        int destPitch = destRowBytes / bytesPerPixel;
        int is16f = (pixelFormat != PrPixelFormat_GPU_BGRA_4444_32f) ? 1 : 0;
        size_t totalBytes = (size_t)destRowBytes * height;

        // --- 1. 시간 및 프레임 번호 계산 ---
        PrTime ticksPerFrame = 0;
        if (mSequenceInfoSuite &&
            mSequenceInfoSuite->GetFrameRate(mTimelineID, &ticksPerFrame) == suiteError_NoError &&
            ticksPerFrame > 0)
        {
        }
        else
        {
            ticksPerFrame = 254016000000LL / 30;
        }

        A_long currentFrame = 0;
        bool frameCalculated = false;

        csSDK_int32 ownerNodeID = 0;
        if (mVideoSegmentSuite)
        {
            mVideoSegmentSuite->AcquireOperatorOwnerNodeID(mNodeID, &ownerNodeID);
        }

        // [경로 1: 일반 비디오 클립]
        if (!frameCalculated && ownerNodeID != 0 && mVideoSegmentSuite && mMemoryManagerSuite)
        {
            PrTime inPointTicks = 0;
            if (FindInPointTicksInHierarchy(mVideoSegmentSuite, mMemoryManagerSuite, ownerNodeID, &inPointTicks))
            {
                PrTime headTransitionTicks = GetHeadTransitionDurationTicks(mVideoSegmentSuite, mMemoryManagerSuite, ownerNodeID);
                PrTime adjustedInPointTicks = inPointTicks - headTransitionTicks;
                PrTime elapsedTicks = inRenderParams->inClipTime - adjustedInPointTicks;

                if (elapsedTicks >= 0)
                {
                    currentFrame = (A_long)(elapsedTicks / ticksPerFrame);
                    frameCalculated = true;
                }
                else if (headTransitionTicks > 0)
                {
                    currentFrame = 0;
                    frameCalculated = true;
                }
            }
        }

        const PrTime targetInstance = GetEffectRuntimeInstance(mVideoSegmentSuite, mMemoryManagerSuite, mNodeID);

        // [경로 2: 조정 레이어] ownerNodeID가 0이거나 경로 1에서 계산되지 않은 경우
        if (!frameCalculated && mVideoSegmentSuite && mMemoryManagerSuite && mTimelineID != 0)
        {
            csSDK_int32 videoSegmentsID = 0;
            if (mVideoSegmentSuite->AcquireVideoSegmentsID(mTimelineID, &videoSegmentsID) == suiteError_NoError && videoSegmentsID != 0)
            {
                PrTime segOffset = 0;
                csSDK_int32 segNodeID = 0;
                if (mVideoSegmentSuite->AcquireNodeForTime(videoSegmentsID, inRenderParams->inSequenceTime, &segNodeID, &segOffset) == suiteError_NoError && segNodeID != 0)
                {
                    PrTime inPointTicks = 0;
                    PrTime headTransitionTicks = 0;
                    PrTime layerOffsetTicks = 0;
                    if (FindAdjustmentLayerInPoint(mVideoSegmentSuite, mMemoryManagerSuite, segNodeID, targetInstance, &inPointTicks, &headTransitionTicks, &layerOffsetTicks))
                    {
                        PrTime effectiveClipTime = inRenderParams->inClipTime + layerOffsetTicks;

                        // Re-probe this effect's start transition when outside it.
                        // This keeps the result independent of render order and old edits.
                        if (headTransitionTicks == 0)
                        {
                            // 플레이헤드가 전환 구간을 거치지 않고 직접 배치되었을 때,
                            // 현재 클립의 컷 시작 시점 직전(testSeqTime)을 역탐색하여 트랜지션 유무 확인
                            PrTime cutSeqTime = inRenderParams->inSequenceTime - (effectiveClipTime - inPointTicks);
                            PrTime testSeqTime = cutSeqTime - (ticksPerFrame / 2);
                            if (testSeqTime >= 0)
                            {
                                csSDK_int32 testSegNodeID = 0;
                                PrTime testSegOffset = 0;
                                if (mVideoSegmentSuite->AcquireNodeForTime(videoSegmentsID, testSeqTime, &testSegNodeID, &testSegOffset) == suiteError_NoError && testSegNodeID != 0)
                                {
                                    PrTime testInPoint = 0;
                                    PrTime testHeadTrans = 0;
                                    PrTime testLayerOff = 0;
                                    if (FindAdjustmentLayerInPoint(mVideoSegmentSuite, mMemoryManagerSuite, testSegNodeID, targetInstance, &testInPoint, &testHeadTrans, &testLayerOff))
                                    {
                                        if (testInPoint == inPointTicks && testHeadTrans > 0)
                                        {
                                            headTransitionTicks = testHeadTrans;
                                        }
                                    }
                                    mVideoSegmentSuite->ReleaseVideoNodeID(testSegNodeID);
                                }
                            }
                        }
                        PrTime adjustedInPointTicks = inPointTicks - headTransitionTicks;
                        PrTime elapsedTicks = effectiveClipTime - adjustedInPointTicks;

                        if (elapsedTicks >= 0)
                        {
                            currentFrame = (A_long)(elapsedTicks / ticksPerFrame);
                            frameCalculated = true;
                        }
                        else
                        {
                            // 트랜지션 구간: 0프레임으로 안전하게 보호하여 최후 Fallback(도착점 고정) 방지
                            currentFrame = 0;
                            frameCalculated = true;
                        }
                    }
                    mVideoSegmentSuite->ReleaseVideoNodeID(segNodeID);
                }
                mVideoSegmentSuite->ReleaseVideoSegmentsID(videoSegmentsID);
            }
        }

        // [경로 3: Fallback - ownerNodeID의 TrackItemStartAsTicks]
        if (!frameCalculated && ownerNodeID != 0 && mVideoSegmentSuite && mMemoryManagerSuite)
        {
            PrTime trackItemStart = -1;
            PrMemoryPtr propBuffer = nullptr;
            if (mVideoSegmentSuite->GetNodeProperty(ownerNodeID, "ClipNode::TrackItemStartAsTicks", &propBuffer) == suiteError_NoError && propBuffer)
            {
                std::istringstream stream((const char*)propBuffer);
                stream >> trackItemStart;
                mMemoryManagerSuite->PrDisposePtr(propBuffer);
            }

            if (trackItemStart >= 0)
            {
                PrTime seqElapsedTicks = inRenderParams->inSequenceTime - trackItemStart;
                if (seqElapsedTicks >= 0)
                {
                    currentFrame = (A_long)(seqElapsedTicks / ticksPerFrame);
                    frameCalculated = true;
                }
            }
        }

        if (ownerNodeID != 0 && mVideoSegmentSuite)
        {
            mVideoSegmentSuite->ReleaseVideoNodeID(ownerNodeID);
            ownerNodeID = 0;
        }

        // [경로 4: 최후 Fallback]
        if (!frameCalculated)
        {
            currentFrame = (A_long)(inRenderParams->inClipTime / ticksPerFrame);
        }

        if (currentFrame < 0)
        {
            currentFrame = 0;
        }

        // --- 2. 파라미터 값 읽기 ---
        PrTime tTime = inRenderParams->inClipTime;

        double duration = GetParam(AT_DURATION, tTime).mFloat64;
        if (duration <= 0.0)
        {
            duration = DEFAULT_DURATION;
        }

        int preset = GetParam(AT_EASING_PRESET, tTime).mInt32;
        double easeInVal  = 0.0;
        double easeOutVal = 0.0;

        switch (preset)
        {
        case EASE_PRESET_LINEAR:
            easeInVal  = 0.0;
            easeOutVal = 0.0;
            break;
        case EASE_PRESET_EASE_IN:
            easeInVal  = 0.70;
            easeOutVal = 0.0;
            break;
        case EASE_PRESET_EASE_OUT:
            easeInVal  = 0.0;
            easeOutVal = 0.70;
            break;
        case EASE_PRESET_EASE_IN_OUT:
            easeInVal  = 0.70;
            easeOutVal = 0.70;
            break;
        case EASE_PRESET_CUSTOM:
        default:
            easeInVal  = GetParam(AT_EASE_IN, tTime).mFloat64 / 100.0;
            easeOutVal = GetParam(AT_EASE_OUT, tTime).mFloat64 / 100.0;
            break;
        }

        if (easeInVal < 0.0) easeInVal = 0.0;
        if (easeInVal > 1.0) easeInVal = 1.0;
        if (easeOutVal < 0.0) easeOutVal = 0.0;
        if (easeOutVal > 1.0) easeOutVal = 1.0;

        double p1 = (1.0 / 3.0) * (1.0 - easeInVal);
        double p2 = (2.0 / 3.0) + (1.0 / 3.0) * easeOutVal;

        PrParam paramStartPos = GetParam(AT_START_POSITION, tTime);
        PrParam paramEndPos   = GetParam(AT_END_POSITION, tTime);

        double startPosX = paramStartPos.mPoint.x * width;
        double startPosY = paramStartPos.mPoint.y * height;
        double endPosX   = paramEndPos.mPoint.x   * width;
        double endPosY   = paramEndPos.mPoint.y   * height;

        double startScale = GetParam(AT_START_SCALE, tTime).mFloat64 / 100.0;
        double endScale   = GetParam(AT_END_SCALE, tTime).mFloat64   / 100.0;

        double shutterAngle = GetParam(AT_SHUTTER_ANGLE, tTime).mFloat64;
        int samplesCount = GetParam(AT_SAMPLES, tTime).mInt32;
        if (samplesCount < SAMPLES_MIN) samplesCount = SAMPLES_MIN;
        if (samplesCount > SAMPLES_MAX) samplesCount = SAMPLES_MAX;
        if (shutterAngle < SHUTTER_ANGLE_MIN) shutterAngle = SHUTTER_ANGLE_MIN;
        if (shutterAngle > SHUTTER_ANGLE_MAX) shutterAngle = SHUTTER_ANGLE_MAX;

        std::vector<TransformSample> subSamples;

        double t = (double)currentFrame / duration;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;

        double oneMinusT = 1.0 - t;
        double mainEasedT = 3.0 * oneMinusT * oneMinusT * t * p1 +
                            3.0 * oneMinusT * t * t * p2 +
                            t * t * t;
        if (mainEasedT < 0.0) mainEasedT = 0.0;
        if (mainEasedT > 1.0) mainEasedT = 1.0;

        if (shutterAngle <= 0.0 || duration <= 0.0)
        {
            TransformSample s;
            s.posX  = (float)(startPosX + (endPosX - startPosX) * mainEasedT);
            s.posY  = (float)(startPosY + (endPosY - startPosY) * mainEasedT);
            s.scale = (float)(startScale + (endScale - startScale) * mainEasedT);
            if (s.scale < 0.0001f) s.scale = 0.0001f;
            subSamples.push_back(s);
        }
        else
        {
            int N = samplesCount;
            double deltaF = shutterAngle / 360.0;
            double fStart = (double)currentFrame - deltaF * 0.5;

            for (int i = 0; i < N; ++i)
            {
                double subFrame = fStart + ((double)i + 0.5) / (double)N * deltaF;
                double subT = subFrame / duration;
                if (subT < 0.0) subT = 0.0;
                if (subT > 1.0) subT = 1.0;

                double subOneMinusT = 1.0 - subT;
                double subEasedT = 3.0 * subOneMinusT * subOneMinusT * subT * p1 +
                                   3.0 * subOneMinusT * subT * subT * p2 +
                                   subT * subT * subT;
                if (subEasedT < 0.0) subEasedT = 0.0;
                if (subEasedT > 1.0) subEasedT = 1.0;

                TransformSample s;
                s.posX  = (float)(startPosX + (endPosX - startPosX) * subEasedT);
                s.posY  = (float)(startPosY + (endPosY - startPosY) * subEasedT);
                s.scale = (float)(startScale + (endScale - startScale) * subEasedT);
                if (s.scale < 0.0001f) s.scale = 0.0001f;
                subSamples.push_back(s);
            }
        }

        // --- 3. 렌더링 파라미터 구성 ---
        AutoTransformParams params;
        params.srcPitch   = destPitch;
        params.destPitch  = destPitch;
        params.is16f      = is16f;
        params.width      = width;
        params.height     = height;
        params.numSamples = (int)subSamples.size();
        params.cx         = (float)width  / 2.0f;
        params.cy         = (float)height / 2.0f;

        double offsetMult = GetParam(AT_MODIFIER_OFFSET_MULT, tTime).mFloat64;
        if (offsetMult < 0.0) offsetMult = 0.0;

        PrParam startMaskParam = {};
        PrParam endMaskParam = {};
        int startMode = 3, endMode = 3;
        if (mVideoSegmentSuite->GetParam(mNodeID, AT_START_MASK_MODE - 1,
                tTime, &startMaskParam) == suiteError_NoError &&
            startMaskParam.mType == kPrParamType_Int32)
            startMode = startMaskParam.mInt32;
        if (mVideoSegmentSuite->GetParam(mNodeID, AT_END_MASK_MODE - 1,
                tTime, &endMaskParam) == suiteError_NoError &&
            endMaskParam.mType == kPrParamType_Int32)
            endMode = endMaskParam.mInt32;
        // Older saved effects have no synchronized mask metadata yet.
        if (startMode < 0 || startMode > 2 || endMode < 0 || endMode > 2)
        {
            PresetDataBlock defaultPresets;
            GetDefaultPresets(&defaultPresets);
            // Legacy Preset Code records the End mode even for edited presets.
            if (endMode < 0 || endMode > 2)
            {
                const int code = GetParam(AT_PRESET_CODE, tTime).mInt32;
                if (code >= 1 && code <= AT_NUM_PRESETS) endMode = (int)PresetModifier::X1;
                else if (code >= 101 && code <= 100 + AT_NUM_PRESETS) endMode = (int)PresetModifier::None;
                else if (code >= 201 && code <= 200 + AT_NUM_PRESETS) endMode = (int)PresetModifier::X2;
            }
            if (startMode < 0 || startMode > 2)
                startMode = (int)FindMatchingPresetExtPixel(&defaultPresets,
                    startPosX, startPosY, startScale * 100.0,
                    width, height, offsetMult).modifier;
            if (endMode < 0 || endMode > 2)
                endMode = (int)FindMatchingPresetExtPixel(&defaultPresets,
                    endPosX, endPosY, endScale * 100.0,
                    width, height, offsetMult).modifier;
        }

        float halfW = (float)width * 0.5f;

        float startL = (startMode == (int)PresetModifier::X2) ? halfW : 0.0f;
        float startR = (startMode == (int)PresetModifier::X1) ? halfW : (float)width;

        float endL   = (endMode == (int)PresetModifier::X2)   ? halfW : 0.0f;
        float endR   = (endMode == (int)PresetModifier::X1)   ? halfW : (float)width;

        params.cropLeft  = startL + (endL - startL) * (float)mainEasedT;
        params.cropRight = startR + (endR - startR) * (float)mainEasedT;

        if (startMode == (int)PresetModifier::None && endMode != (int)PresetModifier::None)
        {
            float bleed = 4.0f * (1.0f - (float)mainEasedT);
            if (endMode == (int)PresetModifier::X1)
            {
                params.cropRight += bleed;
            }
            else if (endMode == (int)PresetModifier::X2)
            {
                params.cropLeft -= bleed;
            }
        }
        else if (startMode != (int)PresetModifier::None && endMode == (int)PresetModifier::None)
        {
            float bleed = 4.0f * (float)mainEasedT;
            if (startMode == (int)PresetModifier::X1)
            {
                params.cropRight += bleed;
            }
            else if (startMode == (int)PresetModifier::X2)
            {
                params.cropLeft -= bleed;
            }
        }

        if (params.cropLeft < 0.0f) params.cropLeft = 0.0f;
        if (params.cropRight > (float)width) params.cropRight = (float)width;

        // --- 4. GPU 프레임워크별 렌더링 실행 (In-place 오염 방지) ---

        if (mDeviceInfo.outDeviceFramework == PrGPUDeviceFramework_CUDA)
        {
#if HAS_CUDA
            cudaStream_t stream = (cudaStream_t)mDeviceInfo.outCommandQueueHandle;

            // 임시 버퍼 할당 및 캐싱
            if (!mCUDATempBuffer || mCUDATempBufferSize < totalBytes)
            {
                if (mCUDATempBuffer) cudaFree(mCUDATempBuffer);
                if (cudaMalloc(&mCUDATempBuffer, totalBytes) != cudaSuccess)
                {
                    return suiteError_Fail;
                }
                mCUDATempBufferSize = totalBytes;
            }

            size_t sampleBytes = sizeof(TransformSample) * subSamples.size();
            if (!mCUDASampleBuffer || mCUDASampleBufferSize < sampleBytes)
            {
                if (mCUDASampleBuffer) cudaFree(mCUDASampleBuffer);
                if (cudaMalloc(&mCUDASampleBuffer, sampleBytes) != cudaSuccess)
                {
                    return suiteError_Fail;
                }
                mCUDASampleBufferSize = sampleBytes;
            }

            // 1) 원본 영상을 임시 디바이스 버퍼로 복사 (Device to Device)
            if (cudaMemcpyAsync(mCUDATempBuffer, destFrameData, totalBytes, cudaMemcpyDeviceToDevice, stream) != cudaSuccess)
            {
                return suiteError_Fail;
            }

            // 2) 서브샘플 데이터를 디바이스로 복사
            if (cudaMemcpyAsync(mCUDASampleBuffer, subSamples.data(), sampleBytes, cudaMemcpyHostToDevice, stream) != cudaSuccess)
            {
                return suiteError_Fail;
            }

            // 3) Execute CUDA kernel (mCUDATempBuffer -> destFrameData)
            AutoTransform_CUDA(
                mCUDATempBuffer,
                destFrameData,
                &params,
                (const TransformSample*)mCUDASampleBuffer,
                stream);

            if (cudaPeekAtLastError() != cudaSuccess)
            {
                return suiteError_Fail;
            }

            cudaStreamSynchronize(stream);

            return suiteError_NoError;
#else
            return suiteError_NotImplemented;
#endif
        }
        else if (mDeviceInfo.outDeviceFramework == PrGPUDeviceFramework_OpenCL)
        {
#if HAS_OPENCL
            cl_int clErr = CL_SUCCESS;
            cl_context ctx = (cl_context)mDeviceInfo.outContextHandle;

            // 임시 버퍼 할당 및 캐싱
            if (!mCLTempBuffer || mCLTempBufferSize < totalBytes)
            {
                if (mCLTempBuffer) clReleaseMemObject(mCLTempBuffer);
                mCLTempBuffer = clCreateBuffer(ctx, CL_MEM_READ_WRITE, totalBytes, nullptr, &clErr);
                if (clErr != CL_SUCCESS || !mCLTempBuffer)
                {
                    return suiteError_Fail;
                }
                mCLTempBufferSize = totalBytes;
            }

            size_t sampleBytes = sizeof(TransformSample) * subSamples.size();
            if (!mCLSampleBuffer || mCLSampleBufferSize < sampleBytes)
            {
                if (mCLSampleBuffer) clReleaseMemObject(mCLSampleBuffer);
                mCLSampleBuffer = clCreateBuffer(ctx, CL_MEM_READ_ONLY, sampleBytes, nullptr, &clErr);
                if (clErr != CL_SUCCESS || !mCLSampleBuffer)
                {
                    return suiteError_Fail;
                }
                mCLSampleBufferSize = sampleBytes;
            }

            if (!mCLParamBuffer)
            {
                mCLParamBuffer = clCreateBuffer(ctx, CL_MEM_READ_ONLY, sizeof(AutoTransformParams), nullptr, &clErr);
                if (clErr != CL_SUCCESS || !mCLParamBuffer)
                {
                    return suiteError_Fail;
                }
                mCLParamBufferSize = sizeof(AutoTransformParams);
            }

            cl_mem destCLMem = (cl_mem)destFrameData;

            // 1) 원본 영상 복사
            clErr = clEnqueueCopyBuffer(mCLCommandQueue, destCLMem, mCLTempBuffer, 0, 0, totalBytes, 0, nullptr, nullptr);
            if (clErr != CL_SUCCESS) return suiteError_Fail;

            // 2) 파라미터 및 샘플 데이터 복사
            clErr = clEnqueueWriteBuffer(mCLCommandQueue, mCLParamBuffer, CL_TRUE, 0, sizeof(AutoTransformParams), &params, 0, nullptr, nullptr);
            if (clErr != CL_SUCCESS) return suiteError_Fail;

            clErr = clEnqueueWriteBuffer(mCLCommandQueue, mCLSampleBuffer, CL_TRUE, 0, sampleBytes, subSamples.data(), 0, nullptr, nullptr);
            if (clErr != CL_SUCCESS) return suiteError_Fail;

            // 3) 커널 파라미터 바인딩
            clErr = clSetKernelArg(mCLKernel, 0, sizeof(cl_mem), &mCLTempBuffer);
            if (clErr != CL_SUCCESS) return suiteError_Fail;
            clErr = clSetKernelArg(mCLKernel, 1, sizeof(cl_mem), &destCLMem);
            if (clErr != CL_SUCCESS) return suiteError_Fail;
            clErr = clSetKernelArg(mCLKernel, 2, sizeof(cl_mem), &mCLParamBuffer);
            if (clErr != CL_SUCCESS) return suiteError_Fail;
            clErr = clSetKernelArg(mCLKernel, 3, sizeof(cl_mem), &mCLSampleBuffer);
            if (clErr != CL_SUCCESS) return suiteError_Fail;

            size_t localWorkSize[2] = { 16, 16 };
            size_t globalWorkSize[2] = {
                RoundUpMultiple((size_t)width, localWorkSize[0]),
                RoundUpMultiple((size_t)height, localWorkSize[1])
            };

            clErr = clEnqueueNDRangeKernel(
                mCLCommandQueue,
                mCLKernel,
                2,
                nullptr,
                globalWorkSize,
                localWorkSize,
                0,
                nullptr,
                nullptr);

            if (clErr != CL_SUCCESS) return suiteError_Fail;

            return suiteError_NoError;
#else
            return suiteError_NotImplemented;
#endif
        }
        else if (mDeviceInfo.outDeviceFramework == PrGPUDeviceFramework_DirectX)
        {
#if HAS_DIRECTX
            if (mDeviceIndex >= (csSDK_int32)sDXContextCache.size() || !sDXContextCache[mDeviceIndex])
            {
                return suiteError_Fail;
            }

            DXContextPtr dxContext = sDXContextCache[mDeviceIndex];
            ID3D12Device* device = dxContext->mDevice.Get();
            ID3D12GraphicsCommandList* cmdList = dxContext->mCommandList.Get();
            ID3D12Resource* destResource = (ID3D12Resource*)destFrameData;

            // 1) 임시 디바이스 버퍼 할당 및 캐싱
            if (!mDXTempBuffer || mDXTempBufferSize < totalBytes)
            {
                D3D12_RESOURCE_DESC bufDesc = {};
                bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
                bufDesc.Width = totalBytes;
                bufDesc.Height = 1;
                bufDesc.DepthOrArraySize = 1;
                bufDesc.MipLevels = 1;
                bufDesc.Format = DXGI_FORMAT_UNKNOWN;
                bufDesc.SampleDesc.Count = 1;
                bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
                bufDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

                D3D12_HEAP_PROPERTIES heapProps = {};
                heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

                mDXTempBuffer.Reset();
                HRESULT hr = device->CreateCommittedResource(
                    &heapProps,
                    D3D12_HEAP_FLAG_NONE,
                    &bufDesc,
                    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                    nullptr,
                    IID_PPV_ARGS(mDXTempBuffer.GetAddressOf()));
                if (FAILED(hr))
                {
                    return suiteError_Fail;
                }
                mDXTempBufferSize = totalBytes;
            }

            // 2) 원본 영상을 임시 디바이스 버퍼로 복사 (destResource -> mDXTempBuffer)
            D3D12_RESOURCE_BARRIER preCopyBarriers[2] = {};
            preCopyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            preCopyBarriers[0].Transition.pResource = destResource;
            preCopyBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            preCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            preCopyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;

            preCopyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            preCopyBarriers[1].Transition.pResource = mDXTempBuffer.Get();
            preCopyBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            preCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            preCopyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;

            cmdList->ResourceBarrier(2, preCopyBarriers);

            cmdList->CopyResource(mDXTempBuffer.Get(), destResource);

            // 3) 복사 후 상태 전환:
            D3D12_RESOURCE_BARRIER postCopyBarriers[2] = {};
            postCopyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            postCopyBarriers[0].Transition.pResource = destResource;
            postCopyBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            postCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
            postCopyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

            postCopyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            postCopyBarriers[1].Transition.pResource = mDXTempBuffer.Get();
            postCopyBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            postCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
            postCopyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

            cmdList->ResourceBarrier(2, postCopyBarriers);

            // 4) 파라미터 구조체 설정
            AutoTransformParamsDX paramsDX = {};
            paramsDX.srcPitch   = destPitch;
            paramsDX.destPitch  = destPitch;
            paramsDX.is16f      = is16f;
            paramsDX.width      = width;
            paramsDX.height     = height;
            paramsDX.numSamples = (int)subSamples.size();
            paramsDX.cx         = (float)width  / 2.0f;
            paramsDX.cy         = (float)height / 2.0f;
            paramsDX.cropLeft   = params.cropLeft;
            paramsDX.cropRight  = params.cropRight;

            int count = (int)min(subSamples.size(), (size_t)32);
            for (int i = 0; i < count; ++i)
            {
                paramsDX.samples[i].posX  = subSamples[i].posX;
                paramsDX.samples[i].posY  = subSamples[i].posY;
                paramsDX.samples[i].scale = subSamples[i].scale;
                paramsDX.samples[i]._pad  = 0.0f;
            }

            // 5) DXShaderExecution 설정 및 실행 (내부에서 Dispatch 및 CloseWaitAndReset 수행)
            DXShaderExecution shaderExecution(
                dxContext,
                sDXShaderObjectCache[mDeviceIndex],
                3);

            shaderExecution.SetParamBuffer(&paramsDX, sizeof(paramsDX));
            shaderExecution.SetUnorderedAccessView(destResource, (UINT)totalBytes);
            shaderExecution.SetShaderResourceView(mDXTempBuffer.Get(), (UINT)totalBytes);

            UINT dispatchX = (UINT)((width + 15) / 16);
            UINT dispatchY = (UINT)((height + 15) / 16);

            if (!shaderExecution.Execute(dispatchX, dispatchY))
            {
                return suiteError_Fail;
            }

            return suiteError_NoError;
#else
            return suiteError_NotImplemented;
#endif
        }

        return suiteError_NotImplemented;
    }

    static prSuiteError Shutdown(piSuitesPtr piSuites, csSDK_int32 inIndex)
    {
#if HAS_OPENCL
        if (inIndex < kMaxDevices && sOpenCLKernelCache[inIndex])
        {
            clReleaseKernel(sOpenCLKernelCache[inIndex]);
            sOpenCLKernelCache[inIndex] = nullptr;
        }
#endif
#if HAS_DIRECTX
        if (inIndex < (csSDK_int32)sDXContextCache.size() && sDXContextCache[inIndex])
        {
            sDXShaderObjectCache[inIndex].reset();
            sDXContextCache[inIndex].reset();
        }
#endif
        return suiteError_NoError;
    }

private:
    PrSDKSequenceInfoSuite* mSequenceInfoSuite = nullptr;

    // CUDA 버퍼 캐시
    void*  mCUDATempBuffer;
    size_t mCUDATempBufferSize;
    void*  mCUDASampleBuffer;
    size_t mCUDASampleBufferSize;

    // OpenCL buffer and queue cache
    cl_mem           mCLTempBuffer;
    size_t           mCLTempBufferSize;
    cl_mem           mCLSampleBuffer;
    size_t           mCLSampleBufferSize;
    cl_mem           mCLParamBuffer;
    size_t           mCLParamBufferSize;
    cl_command_queue mCLCommandQueue;
    cl_kernel        mCLKernel;

    // DirectX 12 buffer cache
    Microsoft::WRL::ComPtr<ID3D12Resource> mDXTempBuffer;
    size_t                                 mDXTempBufferSize = 0;
};

// Premiere Pro MPE GPU Filter entry point
DECLARE_GPUFILTER_ENTRY(PrGPUFilterModule<AutoTransformGPUFilter>)

#endif // _WIN32
