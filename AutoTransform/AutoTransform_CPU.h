#pragma once

#include <cmath>
#include <cstddef>

namespace ATCPU {

// Premiere BGRA and VUYA 32f both store three color channels followed by alpha.
// Filtering each channel independently preserves either layout and HDR values.
struct Pixel32f { float channels[4]; };
static_assert(sizeof(Pixel32f) == 16, "Premiere 32f pixels require 16 bytes");

struct TransformSample {
    double posX;
    double posY;
    double scale;
};

inline Pixel32f ReadPixel(const void* data, std::ptrdiff_t rowbytes,
                         int width, int height, int x, int y)
{
    if (x < 0 || x >= width || y < 0 || y >= height) return Pixel32f{};
    const auto* row = reinterpret_cast<const Pixel32f*>(
        static_cast<const char*>(data) + y * rowbytes);
    return row[x];
}

inline Pixel32f SampleBilinear(const void* data, std::ptrdiff_t rowbytes,
                             int width, int height, double x, double y)
{
    Pixel32f result = {};
    if (!std::isfinite(x) || !std::isfinite(y) ||
        x < -1 || y < -1 || x >= width || y >= height) return result;

    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    // Match the GPU kernels: pixels outside the image are transparent.
    const Pixel32f p00 = ReadPixel(data, rowbytes, width, height, x0, y0);
    const Pixel32f p10 = ReadPixel(data, rowbytes, width, height, x0 + 1, y0);
    const Pixel32f p01 = ReadPixel(data, rowbytes, width, height, x0, y0 + 1);
    const Pixel32f p11 = ReadPixel(data, rowbytes, width, height, x0 + 1, y0 + 1);
    const double fx = x - x0, fy = y - y0;
    for (int c = 0; c < 4; ++c) {
        result.channels[c] = static_cast<float>(
            p00.channels[c] * (1 - fx) * (1 - fy) +
            p10.channels[c] * fx * (1 - fy) +
            p01.channels[c] * (1 - fx) * fy +
            p11.channels[c] * fx * fy);
    }
    return result;
}

inline Pixel32f TransformPixel(const void* data, std::ptrdiff_t rowbytes,
                              int width, int height, double cx, double cy,
                              const TransformSample* samples, int count,
                              double cropLeft, double cropRight, int x, int y)
{
    Pixel32f result = {};
    if (x < cropLeft || x >= cropRight || count <= 0) return result;
    double sum[4] = {};
    for (int i = 0; i < count; ++i) {
        const auto& s = samples[i];
        const Pixel32f p = SampleBilinear(data, rowbytes, width, height,
            cx + (x - s.posX) / s.scale, cy + (y - s.posY) / s.scale);
        for (int c = 0; c < 4; ++c) sum[c] += p.channels[c];
    }
    for (int c = 0; c < 4; ++c)
        result.channels[c] = static_cast<float>(sum[c] / count);
    return result;
}

} // namespace ATCPU
