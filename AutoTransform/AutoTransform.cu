/*******************************************************************/
/*                                                                 */
/*  AutoTransform.cu                                               */
/*                                                                 */
/*  NVIDIA CUDA accelerated rendering kernel for                   */
/*  AutoTransform plugin in Premiere Pro Mercury Playback Engine   */
/*                                                                 */
/*******************************************************************/

#ifndef AUTOTRANSFORM_CU
#define AUTOTRANSFORM_CU

#include <cuda_runtime.h>
#include <cuda_fp16.h>
#include <math.h>

#ifndef MAX_SUB_SAMPLES
    #define MAX_SUB_SAMPLES 32
#endif

// Subsample structure (matches AutoTransform.metal and C++ host)
struct TransformSample
{
    float posX;
    float posY;
    float scale;
};

// Kernel parameter structure (matches AutoTransform.metal and C++ host)
struct AutoTransformParams
{
    int   srcPitch;   // Source row pitch in pixels
    int   destPitch;  // Destination row pitch in pixels
    int   is16f;      // 16-bit float flag (0: 32-bit, 1: 16-bit)
    int   width;      // Frame width
    int   height;     // Frame height
    int   numSamples; // Subsample count (1 ~ 32)
    float cx;         // Center X
    float cy;         // Center Y
    float cropLeft;   // Visible left boundary in pixels
    float cropRight;  // Visible right boundary in pixels
};

// 16-bit float4 structure
struct half4
{
    half x, y, z, w;
};

static inline __device__ float4 Half4ToFloat4(half4 inV)
{
    float4 out;
    out.x = __half2float(inV.x);
    out.y = __half2float(inV.y);
    out.z = __half2float(inV.z);
    out.w = __half2float(inV.w);
    return out;
}

static inline __device__ half4 Float4ToHalf4(float4 inV)
{
    half4 out;
    out.x = __float2half_rn(inV.x);
    out.y = __float2half_rn(inV.y);
    out.z = __float2half_rn(inV.z);
    out.w = __float2half_rn(inV.w);
    return out;
}

static inline __device__ float4 make_float4_zero()
{
    float4 v;
    v.x = 0.0f; v.y = 0.0f; v.z = 0.0f; v.w = 0.0f;
    return v;
}

static inline __device__ float4 add_float4(float4 a, float4 b)
{
    float4 r;
    r.x = a.x + b.x;
    r.y = a.y + b.y;
    r.z = a.z + b.z;
    r.w = a.w + b.w;
    return r;
}

static inline __device__ float4 scale_float4(float4 a, float s)
{
    float4 r;
    r.x = a.x * s;
    r.y = a.y * s;
    r.z = a.z * s;
    r.w = a.w * s;
    return r;
}

// Pixel read helper
static inline __device__ float4 ReadPixel(
    const void* src,
    int         pitch,
    int         x,
    int         y,
    int         width,
    int         height,
    int         is16f)
{
    if (x < 0 || x >= width || y < 0 || y >= height)
    {
        return make_float4_zero();
    }

    int index = y * pitch + x;
    if (is16f)
    {
        const half4* ptr = (const half4*)src;
        return Half4ToFloat4(ptr[index]);
    }
    else
    {
        const float4* ptr = (const float4*)src;
        return ptr[index];
    }
}

// Pixel write helper
static inline __device__ void WritePixel(
    void*  dest,
    int    pitch,
    int    x,
    int    y,
    float4 value,
    int    is16f)
{
    int index = y * pitch + x;
    if (is16f)
    {
        half4* ptr = (half4*)dest;
        ptr[index] = Float4ToHalf4(value);
    }
    else
    {
        float4* ptr = (float4*)dest;
        ptr[index] = value;
    }
}

// Bilinear sampling helper
static inline __device__ float4 SampleBilinear(
    const void* src,
    int         pitch,
    float       srcX,
    float       srcY,
    int         width,
    int         height,
    int         is16f)
{
    if (srcX < -1.0f || srcX >= (float)width ||
        srcY < -1.0f || srcY >= (float)height)
    {
        return make_float4_zero();
    }

    int x0 = (int)floorf(srcX);
    int y0 = (int)floorf(srcY);
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

    float4 r = make_float4_zero();
    r = add_float4(r, scale_float4(p00, fx1 * fy1));
    r = add_float4(r, scale_float4(p10, fx  * fy1));
    r = add_float4(r, scale_float4(p01, fx1 * fy ));
    r = add_float4(r, scale_float4(p11, fx  * fy ));
    return r;
}

// CUDA transform and motion blur kernel
__global__ void kAutoTransformKernelCUDA(
    const void*                 src,
    void*                       dest,
    const AutoTransformParams   params,
    const TransformSample*      samples)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;

    if (x >= params.width || y >= params.height)
    {
        return;
    }

    // Transparent outside visible crop boundary
    if ((float)x < params.cropLeft || (float)x >= params.cropRight)
    {
        WritePixel(dest, params.destPitch, x, y, make_float4_zero(), params.is16f);
        return;
    }

    // Single sample (no motion blur or stationary)
    if (params.numSamples <= 1)
    {
        float safeScale = (samples[0].scale > 0.0001f) ? samples[0].scale : 0.0001f;
        float srcX = params.cx + ((float)x - samples[0].posX) / safeScale;
        float srcY = params.cy + ((float)y - samples[0].posY) / safeScale;

        float4 result = SampleBilinear(src, params.srcPitch, srcX, srcY, params.width, params.height, params.is16f);
        WritePixel(dest, params.destPitch, x, y, result, params.is16f);
    }
    else
    {
        // Multi-subsample accumulation (shutter angle motion blur)
        float4 accum = make_float4_zero();

        for (int i = 0; i < params.numSamples; ++i)
        {
            float safeScale = (samples[i].scale > 0.0001f) ? samples[i].scale : 0.0001f;
            float srcX = params.cx + ((float)x - samples[i].posX) / safeScale;
            float srcY = params.cy + ((float)y - samples[i].posY) / safeScale;

            float4 s = SampleBilinear(src, params.srcPitch, srcX, srcY, params.width, params.height, params.is16f);
            accum = add_float4(accum, s);
        }

        float invSamples = 1.0f / (float)params.numSamples;
        float4 result = scale_float4(accum, invSamples);
        WritePixel(dest, params.destPitch, x, y, result, params.is16f);
    }
}

// C++ host interface
extern "C" void AutoTransform_CUDA(
    const void*                 srcBuf,
    void*                       destBuf,
    const AutoTransformParams*  params,
    const TransformSample*      d_samples,
    cudaStream_t                stream)
{
    dim3 blockDim(16, 16, 1);
    dim3 gridDim(
        (params->width + blockDim.x - 1) / blockDim.x,
        (params->height + blockDim.y - 1) / blockDim.y,
        1);

    kAutoTransformKernelCUDA<<<gridDim, blockDim, 0, stream>>>(
        srcBuf,
        destBuf,
        *params,
        d_samples);
}

#endif // AUTOTRANSFORM_CU
