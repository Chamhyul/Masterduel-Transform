/*******************************************************************/
/*                                                                 */
/*  AutoTransform_GPU.mm                                           */
/*                                                                 */
/*  Metal GPU accelerated rendering implementation for             */
/*  Premiere Pro Mercury Playback Engine (MPE).                    */
/*  Includes Shutter Angle Motion Blur with Subframe Integration.  */
/*                                                                 */
/*******************************************************************/

#include "AutoTransform.h"
#include "PrGPUFilterModule.h"
#include "PrSDKSequenceInfoSuite.h"
#import <Metal/Metal.h>
#include <dlfcn.h>
#include <math.h>
#include <vector>

// 서브샘플 구조체 (AutoTransform.metal과 100% 일치)
struct TransformSample
{
    float posX;
    float posY;
    float scale;
};

// Metal 커널 파라미터 구조체 (AutoTransform.metal과 100% 일치)
struct AutoTransformParams
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
};

static inline prSuiteError CheckMetalError(NSError* inError)
{
    if (inError)
    {
        return suiteError_Fail;
    }
    return suiteError_NoError;
}

static inline size_t DivideRoundUp(size_t inValue, size_t inMultiple)
{
    return inValue ? (inValue + inMultiple - 1) / inMultiple : 0;
}

// 런타임 소스 컴파일용 임베디드 Metal 셰이더 (JIT 폴백용)
static const char* kEmbeddedMetalSource = R"(
#include <metal_stdlib>
using namespace metal;

struct TransformSample
{
    float posX;
    float posY;
    float scale;
};

struct AutoTransformParams
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
};

static inline float4 ReadPixel(
    device const void* src,
    int                pitch,
    int                x,
    int                y,
    int                width,
    int                height,
    int                is16f)
{
    if (x < 0 || x >= width || y < 0 || y >= height)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    int index = y * pitch + x;
    if (is16f)
    {
        device const half4* ptr = (device const half4*)src;
        return float4(ptr[index]);
    }
    else
    {
        device const float4* ptr = (device const float4*)src;
        return ptr[index];
    }
}

static inline void WritePixel(
    device void* dest,
    int          pitch,
    int          x,
    int          y,
    float4       value,
    int          is16f)
{
    int index = y * pitch + x;
    if (is16f)
    {
        device half4* ptr = (device half4*)dest;
        ptr[index] = half4(value);
    }
    else
    {
        device float4* ptr = (device float4*)dest;
        ptr[index] = value;
    }
}

static inline float4 SampleBilinear(
    device const void* src,
    int                pitch,
    float              srcX,
    float              srcY,
    int                width,
    int                height,
    int                is16f)
{
    if (srcX < -1.0f || srcX >= (float)width ||
        srcY < -1.0f || srcY >= (float)height)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    int x0 = (int)floor(srcX);
    int y0 = (int)floor(srcY);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    float fx  = srcX - (float)x0;
    float fy  = srcY - (float)y0;
    float fx1 = 1.0f - fx;
    float fy1 = 1.0f - fy;

    float4 p00 = ReadPixel(src, pitch, x0, y0, width, height, is16f);
    float4 p10 = ReadPixel(src, pitch, x1, y0, width, height, is16f);
    float4 p01 = ReadPixel(src, pitch, x0, y1, width, height, is16f);
    float4 p11 = ReadPixel(src, pitch, x1, y1, width, height, is16f);

    return p00 * (fx1 * fy1) +
           p10 * (fx  * fy1) +
           p01 * (fx1 * fy ) +
           p11 * (fx  * fy );
}

kernel void kAutoTransformKernel(
    device const void*              src        [[buffer(0)]],
    device void*                    dest       [[buffer(1)]],
    constant AutoTransformParams&   params     [[buffer(2)]],
    constant TransformSample*       samples    [[buffer(3)]],
    uint2                           inXY       [[thread_position_in_grid]])
{
    if (inXY.x >= (uint)params.width || inXY.y >= (uint)params.height)
    {
        return;
    }

    // 가시 영역([cropLeft, cropRight)) 밖인 경우 완전 투명 처리
    if ((float)inXY.x < params.cropLeft || (float)inXY.x >= params.cropRight)
    {
        WritePixel(dest, params.destPitch, inXY.x, inXY.y, float4(0.0f, 0.0f, 0.0f, 0.0f), params.is16f);
        return;
    }

    if (params.numSamples <= 1)
    {
        float safeScale = max(samples[0].scale, 0.0001f);
        float srcX = params.cx + ((float)inXY.x - samples[0].posX) / safeScale;
        float srcY = params.cy + ((float)inXY.y - samples[0].posY) / safeScale;

        float4 result = SampleBilinear(src, params.srcPitch, srcX, srcY, params.width, params.height, params.is16f);
        WritePixel(dest, params.destPitch, inXY.x, inXY.y, result, params.is16f);
    }
    else
    {
        float4 accum = float4(0.0f, 0.0f, 0.0f, 0.0f);

        for (int i = 0; i < params.numSamples; ++i)
        {
            float safeScale = max(samples[i].scale, 0.0001f);
            float srcX = params.cx + ((float)inXY.x - samples[i].posX) / safeScale;
            float srcY = params.cy + ((float)inXY.y - samples[i].posY) / safeScale;

            accum += SampleBilinear(src, params.srcPitch, srcX, srcY, params.width, params.height, params.is16f);
        }

        float4 result = accum / (float)params.numSamples;
        WritePixel(dest, params.destPitch, inXY.x, inXY.y, result, params.is16f);
    }
}
)";

enum { kMaxDevices = 16 };
static id<MTLComputePipelineState> sMetalPipelineCache[kMaxDevices] = {};

// 노드 하위 트리에 대상 노드(우리 이펙트 또는 소유자 노드)가 포함되어 있는지 재귀 탐색
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

// 컴포지터 노드의 각 입력 레이어를 검사하여 우리 이펙트가 소속된 레이어의 오프셋 추출
static bool FindLayerOffsetForEffect(
    PrSDKVideoSegmentSuite* segmentSuite,
    csSDK_int32 rootNodeID,
    csSDK_int32 effectNodeID,
    csSDK_int32 ownerNodeID,
    PrTime* outLayerOffset)
{
    if (!segmentSuite || rootNodeID == 0 || !outLayerOffset)
    {
        return false;
    }

    // 루트 노드 자체가 대상 노드인 경우
    if (rootNodeID == effectNodeID || (ownerNodeID != 0 && rootNodeID == ownerNodeID))
    {
        *outLayerOffset = 0;
        return true;
    }

    csSDK_int32 inputCount = 0;
    if (segmentSuite->GetNodeInputCount(rootNodeID, &inputCount) != suiteError_NoError || inputCount <= 0)
    {
        // 입력이 없더라도 하위 오퍼레이터에 직접 연결되어 있을 수 있음
        if (DoesNodeContainTarget(segmentSuite, rootNodeID, effectNodeID, ownerNodeID, 0))
        {
            *outLayerOffset = 0;
            return true;
        }
        return false;
    }

    for (int i = 0; i < inputCount; ++i)
    {
        PrTime layerOffset = 0;
        csSDK_int32 layerNodeID = 0;
        if (segmentSuite->AcquireInputNodeID(rootNodeID, i, &layerOffset, &layerNodeID) == suiteError_NoError && layerNodeID != 0)
        {
            bool match = false;
            if (layerNodeID == ownerNodeID || layerNodeID == effectNodeID)
            {
                match = true;
            }
            else if (DoesNodeContainTarget(segmentSuite, layerNodeID, effectNodeID, ownerNodeID, 0))
            {
                match = true;
            }

            segmentSuite->ReleaseVideoNodeID(layerNodeID);

            if (match)
            {
                *outLayerOffset = layerOffset;
                return true;
            }
        }
    }

    return false;
}

// 노드 계층 트리(일반 비디오 클립)에서 직속 MediaNode를 탐색하여 InPointMediaTimeAsTicks를 추출
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

// 클립 시작부(Head)에 트랜지션(크로스페이드 등)이 걸려있는지 검사하고 트랜지션 길이(틱) 반환
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

// 조정 레이어(RenderableNode_AdjustmentImpl)로부터 InPoint 및 트랜지션 길이 추출 헬퍼 함수
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

    // 모든 하위 입력을 순회하여 ClipNode(트랜지션 길이)와 MediaNode(InPoint)를 모두 탐색
    for (int si = 0; si < subCount; ++si)
    {
        PrTime siOff = 0;
        csSDK_int32 siID = 0;
        if (segmentSuite->AcquireInputNodeID(adjNodeID, si, &siOff, &siID) == suiteError_NoError && siID != 0)
        {
            char siType[256] = {0};
            segmentSuite->GetNodeInfo(siID, siType, &hash, &flags);


            // ClipNode 발견 시 트랜지션 길이 조회 및 오프셋 보존
            if (strstr(siType, "Clip") != nullptr)
            {
                PrTime ct = GetHeadTransitionDurationTicks(segmentSuite, memSuite, siID);
                if (ct > 0 && transTicks == 0) transTicks = ct;
                clipNodeOffset = siOff;
            }

            // RenderableNodeMediaImpl 발견 시 InPoint 획득
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

    // 트랜지션 길이는 오직 트랜지션 노드 내부(isInsideTransition == true)이고
    // ClipNode 오프셋이 음수(트랜지션으로 인해 앞단으로 당겨진 경우)일 때만 유효!
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

// 조정 레이어(RenderableNode_AdjustmentImpl 또는 전환 노드)에서 InPoint, 트랜지션 길이, 레이어 오프셋 추출
// RuntimeInstanceID was measured on both the live effect and its tree operator
// in timing diagnostic V2. Node handles themselves change on reacquisition.
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
                                        PrTime resolvedHeadTrans = subTrans;
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
    AutoTransformGPUFilter() = default;

    virtual ~AutoTransformGPUFilter()
    {
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

        if (mDeviceInfo.outDeviceFramework == PrGPUDeviceFramework_Metal)
        {
            if (!sMetalPipelineCache[mDeviceIndex])
            {
                @autoreleasepool
                {
                    id<MTLDevice> device = (id<MTLDevice>)mDeviceInfo.outDeviceHandle;
                    if (!device)
                    {
                        return suiteError_Fail;
                    }

                    id<MTLLibrary> library = nil;
                    NSError* error = nil;

                    // 1. 번들 내 사전 컴파일된 MetalLib 로드 시도
                    Dl_info dlinfo;
                    if (dladdr((const void*)&AutoTransformGPUFilter::Startup, &dlinfo) && dlinfo.dli_fname)
                    {
                        NSString* dylibPath = [NSString stringWithUTF8String:dlinfo.dli_fname];
                        NSString* bundlePath = [[[dylibPath stringByDeletingLastPathComponent] stringByDeletingLastPathComponent] stringByDeletingLastPathComponent];
                        NSString* metalLibPath = [bundlePath stringByAppendingPathComponent:@"Contents/Resources/MetalLib/AutoTransform.metallib"];

                        if ([[NSFileManager defaultManager] fileExistsAtPath:metalLibPath])
                        {
                            library = [device newLibraryWithFile:metalLibPath error:&error];
                        }
                    }

                    // 2. 번들 파일이 없거나 로드 실패 시 내장 소스 JIT 컴파일
                    if (!library)
                    {
                        error = nil;
                        NSString* sourceStr = [NSString stringWithUTF8String:kEmbeddedMetalSource];
                        library = [device newLibraryWithSource:sourceStr options:nil error:&error];
                    }

                    if (!library || error)
                    {
                        return suiteError_Fail;
                    }

                    id<MTLFunction> function = [library newFunctionWithName:@"kAutoTransformKernel"];
                    if (!function)
                    {
                        [library release];
                        return suiteError_Fail;
                    }

                    id<MTLComputePipelineState> pipeline = [device newComputePipelineStateWithFunction:function error:&error];
                    [function release];
                    [library release];

                    if (!pipeline || error)
                    {
                        return suiteError_Fail;
                    }

                    sMetalPipelineCache[mDeviceIndex] = pipeline;
                }
            }
            return suiteError_NoError;
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

        if (mDeviceInfo.outDeviceFramework != PrGPUDeviceFramework_Metal)
        {
            return suiteError_NotImplemented;
        }

        @autoreleasepool
        {
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

            // --- 1. 시간 및 프레임 번호 계산 (컷편집 시작점 및 조정 레이어 보정) ---
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


            // =========================================================================
            // [정석 InPoint 시간 계산: 일반 비디오 및 조정 레이어 분할 완벽 대응]
            // =========================================================================

            // [경로 1: 일반 비디오 클립] ownerNodeID가 존재하는 경우
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
                        // 트랜지션 구간 극초반의 미세 오차는 0프레임으로 고정하여 최후 Fallback 방지
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

            // [Fallback 4단계: 최후 Fallback] 클립 로컬 시간 사용
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

            // 이징 프리셋 및 Ease In/Out 값
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

            // 위치 및 크기 파라미터 취득
            PrParam paramStartPos = GetParam(AT_START_POSITION, tTime);
            PrParam paramEndPos   = GetParam(AT_END_POSITION, tTime);

            double startPosX = paramStartPos.mPoint.x * width;
            double startPosY = paramStartPos.mPoint.y * height;
            double endPosX   = paramEndPos.mPoint.x   * width;
            double endPosY   = paramEndPos.mPoint.y   * height;

            double startScale = GetParam(AT_START_SCALE, tTime).mFloat64 / 100.0;
            double endScale   = GetParam(AT_END_SCALE, tTime).mFloat64   / 100.0;

            // --- 3. 셔터각 및 서브샘플링 계산 ---
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
                // 모션 블러 미사용: 단일 샘플링 최적화
                TransformSample s;
                s.posX  = (float)(startPosX + (endPosX - startPosX) * mainEasedT);
                s.posY  = (float)(startPosY + (endPosY - startPosY) * mainEasedT);
                s.scale = (float)(startScale + (endScale - startScale) * mainEasedT);
                if (s.scale < 0.0001f) s.scale = 0.0001f;
                subSamples.push_back(s);
            }
            else
            {
                // 모션 블러 사용: 셔터 구간 동안 N개 서브샘플링 시간 적분
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

            // --- 4. Metal 렌더링 파라미터 구성 ---
            AutoTransformParams params;
            params.srcPitch   = destPitch;
            params.destPitch  = destPitch;
            params.is16f      = is16f;
            params.width      = width;
            params.height     = height;
            params.numSamples = (int)subSamples.size();
            params.cx         = (float)width  / 2.0f;
            params.cy         = (float)height / 2.0f;

            // <, > 모디파이어 화면 분할 투명화 및 오프셋 배율 적용
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

            // 전체에서 좌우 모드로 가려질 때 4px 여유 마진으로 시작해서 0px로 수렴 (최종 0px 오버랩)
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

            id<MTLDevice> device = (id<MTLDevice>)mDeviceInfo.outDeviceHandle;
            id<MTLCommandQueue> queue = (id<MTLCommandQueue>)mDeviceInfo.outCommandQueueHandle;
            id<MTLCommandBuffer> commandBuffer = [queue commandBuffer];

            id<MTLBuffer> ioBuffer = (id<MTLBuffer>)destFrameData;

            // In-place 원본 영상 복사 (Blit)
            NSUInteger bufferSize = (NSUInteger)destRowBytes * height;
            id<MTLBuffer> tempBuffer = [device newBufferWithLength:bufferSize options:MTLResourceStorageModePrivate];

            id<MTLBlitCommandEncoder> blitEncoder = [commandBuffer blitCommandEncoder];
            [blitEncoder copyFromBuffer:ioBuffer
                           sourceOffset:0
                               toBuffer:tempBuffer
                      destinationOffset:0
                                   size:bufferSize];
            [blitEncoder endEncoding];

            // 파라미터 및 서브샘플 버퍼 생성
            id<MTLBuffer> parameterBuffer = [[device newBufferWithBytes:&params
                                                                 length:sizeof(AutoTransformParams)
                                                                options:MTLResourceStorageModeManaged] autorelease];

            id<MTLBuffer> sampleBuffer = [[device newBufferWithBytes:subSamples.data()
                                                              length:sizeof(TransformSample) * subSamples.size()
                                                             options:MTLResourceStorageModeManaged] autorelease];

            id<MTLComputeCommandEncoder> computeEncoder = [commandBuffer computeCommandEncoder];
            id<MTLComputePipelineState> pipeline = sMetalPipelineCache[mDeviceIndex];
            [computeEncoder setComputePipelineState:pipeline];

            // Buffer 0: 복사된 원본 영상
            // Buffer 1: 출력 영상
            // Buffer 2: 유니폼 파라미터
            // Buffer 3: 서브샘플 배열
            [computeEncoder setBuffer:tempBuffer offset:0 atIndex:0];
            [computeEncoder setBuffer:ioBuffer offset:0 atIndex:1];
            [computeEncoder setBuffer:parameterBuffer offset:0 atIndex:2];
            [computeEncoder setBuffer:sampleBuffer offset:0 atIndex:3];

            MTLSize threadsPerGroup = {[pipeline threadExecutionWidth], 16, 1};
            MTLSize numThreadgroups = {
                DivideRoundUp(width, threadsPerGroup.width),
                DivideRoundUp(height, threadsPerGroup.height),
                1
            };

            [computeEncoder dispatchThreadgroups:numThreadgroups threadsPerThreadgroup:threadsPerGroup];
            [computeEncoder endEncoding];
            [commandBuffer commit];

            [tempBuffer release];
            return suiteError_NoError;
        }
    }

    static prSuiteError Shutdown(piSuitesPtr piSuites, csSDK_int32 inIndex)
    {
        @autoreleasepool
        {
            if (inIndex < kMaxDevices && sMetalPipelineCache[inIndex])
            {
                [sMetalPipelineCache[inIndex] release];
                sMetalPipelineCache[inIndex] = nil;
            }
        }
        return suiteError_NoError;
    }

private:
    PrSDKSequenceInfoSuite* mSequenceInfoSuite = nullptr;
};

// Premiere Pro 네이티브 GPU 필터 엔트리포인트 선언
DECLARE_GPUFILTER_ENTRY(PrGPUFilterModule<AutoTransformGPUFilter>)
