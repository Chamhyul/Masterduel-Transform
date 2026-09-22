/*******************************************************************/
/*                                                                 */
/*  AutoTransform.hlsl                                             */
/*                                                                 */
/*  DirectX 12 Compute Shader implementation for AutoTransform     */
/*  plugin in Adobe Premiere Pro Mercury Playback Engine.          */
/*  Supports 32-bit float and 16-bit half pixel formats with       */
/*  Shutter Angle Motion Blur and Subframe Accumulation.           */
/*                                                                 */
/*******************************************************************/

struct TransformSampleDX
{
    float posX;
    float posY;
    float scale;
    float _pad;
};

cbuffer cb : register(b0)
{
    int   mSrcPitch;
    int   mDestPitch;
    int   mIs16f;
    int   mWidth;

    int   mHeight;
    int   mNumSamples;
    float mCx;
    float mCy;

    float mCropLeft;
    float mCropRight;
    float mPad0;
    float mPad1;

    TransformSampleDX mSamples[32];
};

RWByteAddressBuffer mDest : register(u0);
ByteAddressBuffer   mSrc  : register(t0);

static inline float4 ReadPixelHLSL(int pitch, int x, int y, int width, int height, int is16f)
{
    if (x < 0 || x >= width || y < 0 || y >= height)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    uint dataSize = is16f ? 8 : 16;
    uint byteOffset = (uint)(y * pitch + x) * dataSize;

    if (is16f)
    {
        return float4(mSrc.Load<half4>(byteOffset));
    }
    else
    {
        return mSrc.Load<float4>(byteOffset);
    }
}

static inline void WritePixelHLSL(int pitch, int x, int y, float4 value, int is16f)
{
    uint dataSize = is16f ? 8 : 16;
    uint byteOffset = (uint)(y * pitch + x) * dataSize;

    if (is16f)
    {
        mDest.Store<half4>(byteOffset, (half4)value);
    }
    else
    {
        mDest.Store<float4>(byteOffset, value);
    }
}

static inline float4 SampleBilinearHLSL(int pitch, float srcX, float srcY, int width, int height, int is16f)
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

    float4 p00 = ReadPixelHLSL(pitch, x0, y0, width, height, is16f);
    float4 p10 = ReadPixelHLSL(pitch, x1, y0, width, height, is16f);
    float4 p01 = ReadPixelHLSL(pitch, x0, y1, width, height, is16f);
    float4 p11 = ReadPixelHLSL(pitch, x1, y1, width, height, is16f);

    return p00 * (fx1 * fy1) +
           p10 * (fx  * fy1) +
           p01 * (fx1 * fy ) +
           p11 * (fx  * fy );
}

[numthreads(16, 16, 1)]
[RootSignature("DescriptorTable(CBV(b0)),DescriptorTable(UAV(u0)),DescriptorTable(SRV(t0))")]
void main(uint3 inXY : SV_DispatchThreadID)
{
    int x = (int)inXY.x;
    int y = (int)inXY.y;

    if (x >= mWidth || y >= mHeight)
    {
        return;
    }

    // Visible crop boundary check
    if ((float)x < mCropLeft || (float)x >= mCropRight)
    {
        WritePixelHLSL(mDestPitch, x, y, float4(0.0f, 0.0f, 0.0f, 0.0f), mIs16f);
        return;
    }

    if (mNumSamples <= 1)
    {
        float safeScale = (mSamples[0].scale > 0.0001f) ? mSamples[0].scale : 0.0001f;
        float srcX = mCx + ((float)x - mSamples[0].posX) / safeScale;
        float srcY = mCy + ((float)y - mSamples[0].posY) / safeScale;

        float4 result = SampleBilinearHLSL(mSrcPitch, srcX, srcY, mWidth, mHeight, mIs16f);
        WritePixelHLSL(mDestPitch, x, y, result, mIs16f);
    }
    else
    {
        float4 accum = float4(0.0f, 0.0f, 0.0f, 0.0f);
        int validSamples = min(mNumSamples, 32);

        for (int i = 0; i < validSamples; ++i)
        {
            float safeScale = (mSamples[i].scale > 0.0001f) ? mSamples[i].scale : 0.0001f;
            float srcX = mCx + ((float)x - mSamples[i].posX) / safeScale;
            float srcY = mCy + ((float)y - mSamples[i].posY) / safeScale;

            accum += SampleBilinearHLSL(mSrcPitch, srcX, srcY, mWidth, mHeight, mIs16f);
        }

        float4 result = accum / (float)validSamples;
        WritePixelHLSL(mDestPitch, x, y, result, mIs16f);
    }
}
