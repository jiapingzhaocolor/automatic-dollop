#pragma once

#include <cstdint>
#include <vector>

namespace filmgrain {

struct FloatImageView {
    const void* data = nullptr;
    int rowBytes = 0;
    int x1 = 0;
    int y1 = 0;
    int x2 = 0;
    int y2 = 0;
    int components = 4;

    const float* pixel(int x, int y) const;
    float channelClamped(int x, int y, int c) const;
    float lumaClamped(int x, int y) const;
};

struct Params {
    float radius = 0.10f;
    float radiusStdFactor = 0.0f;
    float sigmaFilter = 0.80f;
    int samples = 64;
    std::uint32_t seed = 1u;
};

class Sampler {
public:
    explicit Sampler(const Params& params);

    float renderChannel(const FloatImageView& image, int x, int y, int channel) const;
    float renderLuma(const FloatImageView& image, int x, int y) const;

private:
    float render(const FloatImageView& image, int x, int y, int channel, bool useLuma) const;

    Params params_;
    float grainStd_ = 0.0f;
    float grainRadiusSq_ = 0.0f;
    float cellSize_ = 1.0f;
    float maxRadius_ = 0.0f;
    float logMu_ = 0.0f;
    float logSigma_ = 0.0f;
    std::vector<float> gaussianX_;
    std::vector<float> gaussianY_;
    float lambda_[256]{};
    float expMinusLambda_[256]{};
};

} // namespace filmgrain
