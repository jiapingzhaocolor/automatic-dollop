// Film-grain core adapted from the algorithm in:
// A. Newson, N. Faraj, B. Galerne, J. Delon,
// "Realistic Film Grain Rendering", IPOL 7 (2017), 165-183.
// Original software: GPL-3.0-or-later.
// This derivative is distributed under GPL-3.0-or-later.

#include "film_grain_core.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace filmgrain {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kGreyEpsilon = 0.1f;
constexpr float kNormal999 = 3.0902f;

inline float clamp01(float v) {
    return std::min(std::max(v, 0.0f), 1.0f);
}

std::uint32_t wangHash(std::uint32_t seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed ^= (seed >> 4u);
    seed *= 668265261u;
    seed ^= (seed >> 15u);
    return seed;
}

std::uint32_t cellSeed(int x, int y, std::uint32_t offset) {
    constexpr std::uint32_t period = 65536u;
    const std::uint32_t ux = static_cast<std::uint32_t>(x);
    const std::uint32_t uy = static_cast<std::uint32_t>(y);
    std::uint32_t s = ((uy % period) * period + (ux % period)) + offset;
    return s == 0u ? 1u : s;
}

struct Rng {
    explicit Rng(std::uint32_t seed) : state(wangHash(seed == 0u ? 1u : seed)) {}

    std::uint32_t nextU32() {
        state ^= (state << 13u);
        state ^= (state >> 17u);
        state ^= (state << 5u);
        return state;
    }

    float uniform01() {
        return static_cast<float>(nextU32()) / 4294967295.0f;
    }

    float normal01() {
        const float u = std::max(uniform01(), 1.0e-7f);
        const float v = uniform01();
        return std::sqrt(-2.0f * std::log(u)) * std::cos(2.0f * kPi * v);
    }

    unsigned poisson(float lambda, float expMinusLambda) {
        if (!(lambda > 0.0f)) return 0u;
        const float u = uniform01();
        unsigned x = 0u;
        float prod = expMinusLambda > 0.0f ? expMinusLambda : std::exp(-lambda);
        float sum = prod;
        const unsigned guard = static_cast<unsigned>(std::max(32.0f, std::floor(10000.0f * lambda)));
        while (u > sum && x < guard) {
            ++x;
            prod *= lambda / static_cast<float>(x);
            sum += prod;
        }
        return x;
    }

    std::uint32_t state;
};

inline float sqDistance(float x1, float y1, float x2, float y2) {
    const float dx = x1 - x2;
    const float dy = y1 - y2;
    return dx * dx + dy * dy;
}

} // namespace

const float* FloatImageView::pixel(int x, int y) const {
    if (!data || components <= 0 || x < x1 || x >= x2 || y < y1 || y >= y2) return nullptr;
    const auto* row = reinterpret_cast<const char*>(data) + static_cast<std::ptrdiff_t>(y - y1) * rowBytes;
    return reinterpret_cast<const float*>(row) + static_cast<std::ptrdiff_t>(x - x1) * components;
}

float FloatImageView::channelClamped(int x, int y, int c) const {
    if (!data || x2 <= x1 || y2 <= y1 || components <= 0) return 0.0f;
    x = std::min(std::max(x, x1), x2 - 1);
    y = std::min(std::max(y, y1), y2 - 1);
    c = std::min(std::max(c, 0), components - 1);
    const float* p = pixel(x, y);
    return p ? clamp01(p[c]) : 0.0f;
}

float FloatImageView::lumaClamped(int x, int y) const {
    if (components >= 3) {
        const float r = channelClamped(x, y, 0);
        const float g = channelClamped(x, y, 1);
        const float b = channelClamped(x, y, 2);
        return clamp01(0.2126f * r + 0.7152f * g + 0.0722f * b);
    }
    return channelClamped(x, y, 0);
}

Sampler::Sampler(const Params& input) : params_(input) {
    params_.radius = std::max(params_.radius, 0.001f);
    params_.radiusStdFactor = std::max(params_.radiusStdFactor, 0.0f);
    params_.sigmaFilter = std::max(params_.sigmaFilter, 0.0f);
    params_.samples = std::max(params_.samples, 1);
    if (params_.seed == 0u) params_.seed = 1u;

    grainStd_ = params_.radius * params_.radiusStdFactor;
    grainRadiusSq_ = params_.radius * params_.radius;
    cellSize_ = 1.0f / std::ceil(1.0f / params_.radius);
    maxRadius_ = params_.radius;

    if (grainStd_ > 0.0f) {
        logSigma_ = std::sqrt(std::log((grainStd_ / params_.radius) * (grainStd_ / params_.radius) + 1.0f));
        const float sigmaSq = logSigma_ * logSigma_;
        logMu_ = std::log(params_.radius) - sigmaSq * 0.5f;
        maxRadius_ = std::exp(logMu_ + logSigma_ * kNormal999);
    }

    // The uploaded implementation uses a 256-level intensity lookup. Keep that
    // quantisation, but allocate all 256 entries (the original code writes 0..255).
    for (int i = 0; i < 256; ++i) {
        const float u = static_cast<float>(i) / (255.0f + kGreyEpsilon);
        const float denom = kPi * (grainRadiusSq_ + grainStd_ * grainStd_);
        const float lambda = -(cellSize_ * cellSize_ / denom) * std::log(std::max(1.0f - u, 1.0e-8f));
        lambda_[i] = lambda;
        expMinusLambda_[i] = std::exp(-lambda);
    }

    gaussianX_.resize(static_cast<std::size_t>(params_.samples));
    gaussianY_.resize(static_cast<std::size_t>(params_.samples));
    Rng monteCarlo(params_.seed ^ 0xA341316Cu);
    for (int i = 0; i < params_.samples; ++i) {
        gaussianX_[static_cast<std::size_t>(i)] = monteCarlo.normal01();
        gaussianY_[static_cast<std::size_t>(i)] = monteCarlo.normal01();
    }
}

float Sampler::renderChannel(const FloatImageView& image, int x, int y, int channel) const {
    return render(image, x, y, channel, false);
}

float Sampler::renderLuma(const FloatImageView& image, int x, int y) const {
    return render(image, x, y, 0, true);
}

float Sampler::render(const FloatImageView& image, int x, int y, int channel, bool useLuma) const {
    const float xIn = static_cast<float>(x) + 0.5f;
    const float yIn = static_cast<float>(y) + 0.5f;
    float coveredCount = 0.0f;

    for (int sample = 0; sample < params_.samples; ++sample) {
        // The paper/model describes Gaussian integration jitter. We use standard
        // normal samples and apply sigmaFilter once (the uploaded pixel-wise code
        // effectively applies it twice because the list is already sigma-scaled).
        const float xGaussian = xIn + params_.sigmaFilter * gaussianX_[static_cast<std::size_t>(sample)];
        const float yGaussian = yIn + params_.sigmaFilter * gaussianY_[static_cast<std::size_t>(sample)];

        const int minX = static_cast<int>(std::floor((xGaussian - maxRadius_) / cellSize_));
        const int maxX = static_cast<int>(std::floor((xGaussian + maxRadius_) / cellSize_));
        const int minY = static_cast<int>(std::floor((yGaussian - maxRadius_) / cellSize_));
        const int maxY = static_cast<int>(std::floor((yGaussian + maxRadius_) / cellSize_));

        bool covered = false;
        for (int ncx = minX; ncx <= maxX && !covered; ++ncx) {
            for (int ncy = minY; ncy <= maxY && !covered; ++ncy) {
                const float cellCornerX = cellSize_ * static_cast<float>(ncx);
                const float cellCornerY = cellSize_ * static_cast<float>(ncy);

                const int sx = static_cast<int>(std::floor(cellCornerX));
                const int sy = static_cast<int>(std::floor(cellCornerY));
                const float intensity = useLuma ? image.lumaClamped(sx, sy)
                                                : image.channelClamped(sx, sy, channel);
                const int lutIndex = std::min(255, std::max(0,
                    static_cast<int>(std::floor(intensity * (255.0f + kGreyEpsilon)))));

                Rng rng(cellSeed(ncx, ncy, params_.seed));
                const unsigned grains = rng.poisson(lambda_[lutIndex], expMinusLambda_[lutIndex]);
                for (unsigned k = 0; k < grains; ++k) {
                    const float centreX = cellCornerX + cellSize_ * rng.uniform01();
                    const float centreY = cellCornerY + cellSize_ * rng.uniform01();

                    float radiusSq = grainRadiusSq_;
                    if (grainStd_ > 0.0f) {
                        const float radius = std::min(std::exp(logMu_ + logSigma_ * rng.normal01()), maxRadius_);
                        radiusSq = radius * radius;
                    }

                    if (sqDistance(centreX, centreY, xGaussian, yGaussian) < radiusSq) {
                        covered = true;
                        coveredCount += 1.0f;
                        break;
                    }
                }
            }
        }
    }

    return coveredCount / static_cast<float>(params_.samples);
}

} // namespace filmgrain
