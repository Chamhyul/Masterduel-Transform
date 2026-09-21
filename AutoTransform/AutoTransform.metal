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

// 픽셀 읽기 도우미 함수 (32f 및 16f 지원)
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

// 픽셀 쓰기 도우미 함수 (32f 및 16f 지원)
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

// 양선형 보간 샘플링 도우미 함수
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

// 역방향 매핑, 양선형 보간 및 서브샘플링 모션 블러 렌더링 커널
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

    // 단일 샘플 (모션 블러 미사용 또는 정지 상태 최적화)
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
        // 다중 서브샘플링 누적 합성 (셔터각 모션 블러)
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
