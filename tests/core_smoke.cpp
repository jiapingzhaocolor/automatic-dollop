#include "film_grain_core.h"

#include <cmath>
#include <iostream>
#include <vector>

int main() {
    constexpr int w = 8;
    constexpr int h = 8;
    std::vector<float> rgba(w * h * 4, 0.0f);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float v = static_cast<float>(x + y) / 14.0f;
            float* p = &rgba[(y * w + x) * 4];
            p[0] = v;
            p[1] = v * 0.8f;
            p[2] = v * 0.6f;
            p[3] = 1.0f;
        }
    }

    filmgrain::FloatImageView view;
    view.data = rgba.data();
    view.rowBytes = w * 4 * static_cast<int>(sizeof(float));
    view.x1 = 0; view.y1 = 0; view.x2 = w; view.y2 = h; view.components = 4;

    filmgrain::Params p;
    p.radius = 0.1f;
    p.radiusStdFactor = 0.25f;
    p.sigmaFilter = 0.8f;
    p.samples = 16;
    p.seed = 12345u;

    filmgrain::Sampler a(p);
    filmgrain::Sampler b(p);
    const float va = a.renderLuma(view, 3, 4);
    const float vb = b.renderLuma(view, 3, 4);

    if (!(va >= 0.0f && va <= 1.0f)) return 1;
    if (std::fabs(va - vb) > 1.0e-7f) return 2; // deterministic render is essential for OFX caching.

    std::cout << "core smoke ok: " << va << "\n";
    return 0;
}
