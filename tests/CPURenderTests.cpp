#include "../AutoTransform/AutoTransform_CPU.h"

#include <cassert>
#include <cmath>

using ATCPU::Pixel32f;

static void Equal(Pixel32f actual, Pixel32f expected)
{
    for (int c = 0; c < 4; ++c)
        assert(std::fabs(actual.channels[c] - expected.channels[c]) < 0.00001f);
}

int main()
{
    // Distinct BGRA / VUYA channels, HDR and negative chroma, non-opaque alpha.
    // Each row includes padding that must never be interpreted as image data.
    Pixel32f image[2][3] = {
        {{{2.0f, -0.25f, 0.5f, 0.2f}}, {{4.0f, 0.25f, 1.5f, 0.4f}}, {{99, 99, 99, 99}}},
        {{{6.0f, -0.75f, 2.5f, 0.6f}}, {{8.0f, 0.75f, 3.5f, 0.8f}}, {{99, 99, 99, 99}}}
    };
    const auto pitch = sizeof(image[0]);
    const ATCPU::TransformSample identity = {1, 1, 1};
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x)
            Equal(ATCPU::TransformPixel(image, pitch, 2, 2, 1, 1,
                &identity, 1, 0, 2, x, y), image[y][x]);

    Equal(ATCPU::SampleBilinear(image, pitch, 2, 2, 0.5, 0.5),
          Pixel32f{{5, 0, 2, 0.5f}});
    Equal(ATCPU::SampleBilinear(image, pitch, 2, 2, 1.5, 1.5),
          Pixel32f{{2, 0.1875f, 0.875f, 0.2f}});
    Equal(ATCPU::SampleBilinear(image, pitch, 2, 2, -0.5, 0),
          Pixel32f{{1, -0.125f, 0.25f, 0.1f}});
    Equal(ATCPU::SampleBilinear(image, pitch, 2, 2, -1.1, 0), Pixel32f{});
    Equal(ATCPU::SampleBilinear(image, pitch, 2, 2, 2, 0), Pixel32f{});
    Equal(ATCPU::TransformPixel(image, pitch, 2, 2, 1, 1,
        &identity, 1, 0, 1, 1, 0), Pixel32f{});

    // Two shutter samples average all four channels without 8-bit rounding.
    const ATCPU::TransformSample blur[2] = {{1, 1, 1}, {0, 1, 1}};
    Equal(ATCPU::TransformPixel(image, pitch, 2, 2, 1, 1,
        blur, 2, 0, 2, 0, 0), Pixel32f{{3, 0, 1, 0.3f}});
    const ATCPU::TransformSample zoom = {1, 1, 2};
    Equal(ATCPU::TransformPixel(image, pitch, 2, 2, 1, 1,
        &zoom, 1, 0, 2, 0, 0), Pixel32f{{5, 0, 2, 0.5f}});

    // Signed row stride is honored as well.
    Equal(ATCPU::SampleBilinear(image[1], -static_cast<std::ptrdiff_t>(pitch),
        2, 2, 0, 1), image[0][0]);
}
