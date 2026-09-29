// SPDX-License-Identifier: GPL-3.0-or-later
// OpenFX wrapper for the stochastic film-grain model adapted from
// Newson, Faraj, Galerne, Delon, "Realistic Film Grain Rendering", IPOL 2017.

#include "film_grain_core.h"
#include "ofxImageEffect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>

#if defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__)
#  define EXPORT __attribute__((visibility("default")))
#elif defined(_WIN32)
#  define EXPORT OfxExport
#else
#  error Unsupported operating system
#endif

namespace {

constexpr const char* kPluginIdentifier = "io.github.filmgrainofx.StochasticFilmGrain";
constexpr const char* kPluginLabel = "Stochastic Film Grain";
constexpr const char* kPluginGroup = "Film Emulation";
constexpr int kPluginVersionMajor = 0;
constexpr int kPluginVersionMinor = 1;

constexpr const char* kParamRadius = "radius";
constexpr const char* kParamRadiusStdFactor = "radiusStdFactor";
constexpr const char* kParamSigmaFilter = "sigmaFilter";
constexpr const char* kParamSamples = "samples";
constexpr const char* kParamMix = "mix";
constexpr const char* kParamColourGrain = "colourGrain";
constexpr const char* kParamAnimate = "animate";
constexpr const char* kParamSeed = "seed";

OfxHost* gHost = nullptr;
const OfxPropertySuiteV1* gPropertySuite = nullptr;
const OfxImageEffectSuiteV1* gImageEffectSuite = nullptr;
const OfxParameterSuiteV1* gParameterSuite = nullptr;

OfxStatus loadAction() {
    if (!gHost || !gHost->fetchSuite) return kOfxStatFailed;
    gPropertySuite = reinterpret_cast<const OfxPropertySuiteV1*>(
        gHost->fetchSuite(gHost->host, kOfxPropertySuite, 1));
    gImageEffectSuite = reinterpret_cast<const OfxImageEffectSuiteV1*>(
        gHost->fetchSuite(gHost->host, kOfxImageEffectSuite, 1));
    gParameterSuite = reinterpret_cast<const OfxParameterSuiteV1*>(
        gHost->fetchSuite(gHost->host, kOfxParameterSuite, 1));
    return (gPropertySuite && gImageEffectSuite && gParameterSuite) ? kOfxStatOK : kOfxStatFailed;
}

void setLabelAndHint(OfxPropertySetHandle props, const char* label, const char* hint) {
    gPropertySuite->propSetString(props, kOfxPropLabel, 0, label);
    if (hint && *hint) gPropertySuite->propSetString(props, kOfxParamPropHint, 0, hint);
}

void defineDoubleParam(OfxParamSetHandle params, const char* name, const char* label,
                       const char* hint, double def, double minV, double maxV,
                       double displayMin, double displayMax, double increment, int digits = 3) {
    OfxPropertySetHandle props = nullptr;
    gParameterSuite->paramDefine(params, kOfxParamTypeDouble, name, &props);
    setLabelAndHint(props, label, hint);
    gPropertySuite->propSetDouble(props, kOfxParamPropDefault, 0, def);
    gPropertySuite->propSetDouble(props, kOfxParamPropMin, 0, minV);
    gPropertySuite->propSetDouble(props, kOfxParamPropMax, 0, maxV);
    gPropertySuite->propSetDouble(props, kOfxParamPropDisplayMin, 0, displayMin);
    gPropertySuite->propSetDouble(props, kOfxParamPropDisplayMax, 0, displayMax);
    gPropertySuite->propSetDouble(props, kOfxParamPropIncrement, 0, increment);
    gPropertySuite->propSetInt(props, kOfxParamPropDigits, 0, digits);
}

void defineIntParam(OfxParamSetHandle params, const char* name, const char* label,
                    const char* hint, int def, int minV, int maxV, int displayMin, int displayMax) {
    OfxPropertySetHandle props = nullptr;
    gParameterSuite->paramDefine(params, kOfxParamTypeInteger, name, &props);
    setLabelAndHint(props, label, hint);
    gPropertySuite->propSetInt(props, kOfxParamPropDefault, 0, def);
    gPropertySuite->propSetInt(props, kOfxParamPropMin, 0, minV);
    gPropertySuite->propSetInt(props, kOfxParamPropMax, 0, maxV);
    gPropertySuite->propSetInt(props, kOfxParamPropDisplayMin, 0, displayMin);
    gPropertySuite->propSetInt(props, kOfxParamPropDisplayMax, 0, displayMax);
}

void defineBoolParam(OfxParamSetHandle params, const char* name, const char* label,
                     const char* hint, bool def) {
    OfxPropertySetHandle props = nullptr;
    gParameterSuite->paramDefine(params, kOfxParamTypeBoolean, name, &props);
    setLabelAndHint(props, label, hint);
    gPropertySuite->propSetInt(props, kOfxParamPropDefault, 0, def ? 1 : 0);
}

OfxStatus describeAction(OfxImageEffectHandle descriptor) {
    OfxPropertySetHandle props = nullptr;
    if (gImageEffectSuite->getPropertySet(descriptor, &props) != kOfxStatOK) return kOfxStatFailed;

    gPropertySuite->propSetString(props, kOfxPropLabel, 0, kPluginLabel);
    gPropertySuite->propSetString(props, kOfxImageEffectPluginPropGrouping, 0, kPluginGroup);
    gPropertySuite->propSetString(props, kOfxImageEffectPropSupportedContexts, 0, kOfxImageEffectContextFilter);
    gPropertySuite->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthFloat);
    gPropertySuite->propSetString(props, kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderFullySafe);

    // The algorithm samples neighbouring cells, so ask the host for full-RoD images.
    gPropertySuite->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 0);
    gPropertySuite->propSetInt(props, kOfxImageEffectPluginPropHostFrameThreading, 0, 1);
    gPropertySuite->propSetInt(props, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);

    return kOfxStatOK;
}

OfxStatus describeInContextAction(OfxImageEffectHandle descriptor, OfxPropertySetHandle /*inArgs*/) {
    OfxPropertySetHandle props = nullptr;

    gImageEffectSuite->clipDefine(descriptor, kOfxImageEffectOutputClipName, &props);
    gPropertySuite->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
    gPropertySuite->propSetString(props, kOfxImageEffectPropSupportedComponents, 1, kOfxImageComponentRGB);
    gPropertySuite->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 0);

    gImageEffectSuite->clipDefine(descriptor, kOfxImageEffectSimpleSourceClipName, &props);
    gPropertySuite->propSetString(props, kOfxImageEffectPropSupportedComponents, 0, kOfxImageComponentRGBA);
    gPropertySuite->propSetString(props, kOfxImageEffectPropSupportedComponents, 1, kOfxImageComponentRGB);
    gPropertySuite->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 0);

    OfxParamSetHandle paramSet = nullptr;
    gImageEffectSuite->getParamSet(descriptor, &paramSet);

    defineDoubleParam(paramSet, kParamRadius, "Grain Radius",
                      "Average grain radius in image-pixel units. The source CLI default is 0.1.",
                      0.10, 0.005, 4.0, 0.02, 1.0, 0.005, 3);
    defineDoubleParam(paramSet, kParamRadiusStdFactor, "Radius Variation",
                      "Log-normal grain-radius standard deviation as a fraction of Grain Radius.",
                      0.0, 0.0, 3.0, 0.0, 1.0, 0.01, 3);
    defineDoubleParam(paramSet, kParamSigmaFilter, "Integration Blur",
                      "Gaussian integration jitter used by the Monte-Carlo estimator.",
                      0.80, 0.0, 4.0, 0.0, 2.0, 0.05, 2);
    defineIntParam(paramSet, kParamSamples, "Samples",
                   "Monte-Carlo samples per output pixel. 64 is interactive-ish; the source CLI default is 800.",
                   64, 1, 2000, 8, 800);
    defineDoubleParam(paramSet, kParamMix, "Mix",
                      "Blend between the source and the stochastic grain rendering.",
                      1.0, 0.0, 1.0, 0.0, 1.0, 0.01, 2);
    defineBoolParam(paramSet, kParamColourGrain, "Colour Grain",
                    "Render independent stochastic grain in R/G/B. Off uses a luma grain delta to preserve source chroma.",
                    false);
    defineBoolParam(paramSet, kParamAnimate, "Animate Grain",
                    "Change the stochastic seed with frame time. Disable for a locked grain pattern.",
                    true);
    defineIntParam(paramSet, kParamSeed, "Seed",
                   "Base deterministic seed used by the stochastic model.",
                   1, 1, 2147483647, 1, 10000);

    gParameterSuite->paramDefine(paramSet, kOfxParamTypePage, "Main", &props);
    const char* children[] = {
        kParamRadius, kParamRadiusStdFactor, kParamSigmaFilter, kParamSamples,
        kParamMix, kParamColourGrain, kParamAnimate, kParamSeed
    };
    for (int i = 0; i < static_cast<int>(sizeof(children) / sizeof(children[0])); ++i) {
        gPropertySuite->propSetString(props, kOfxParamPropPageChild, i, children[i]);
    }

    return kOfxStatOK;
}

OfxParamHandle getParam(OfxImageEffectHandle instance, const char* name) {
    OfxParamSetHandle paramSet = nullptr;
    if (gImageEffectSuite->getParamSet(instance, &paramSet) != kOfxStatOK) return nullptr;
    OfxParamHandle p = nullptr;
    if (gParameterSuite->paramGetHandle(paramSet, name, &p, nullptr) != kOfxStatOK) return nullptr;
    return p;
}

double getDoubleAtTime(OfxImageEffectHandle instance, const char* name, OfxTime time, double fallback) {
    OfxParamHandle p = getParam(instance, name);
    double v = fallback;
    if (p) gParameterSuite->paramGetValueAtTime(p, time, &v);
    return v;
}

int getIntAtTime(OfxImageEffectHandle instance, const char* name, OfxTime time, int fallback) {
    OfxParamHandle p = getParam(instance, name);
    int v = fallback;
    if (p) gParameterSuite->paramGetValueAtTime(p, time, &v);
    return v;
}

int componentCount(OfxPropertySetHandle image) {
    char* c = nullptr;
    if (gPropertySuite->propGetString(image, kOfxImageEffectPropComponents, 0, &c) != kOfxStatOK || !c) return 0;
    if (std::strcmp(c, kOfxImageComponentRGBA) == 0) return 4;
    if (std::strcmp(c, kOfxImageComponentRGB) == 0) return 3;
    return 0;
}

bool isFloatImage(OfxPropertySetHandle image) {
    char* depth = nullptr;
    return gPropertySuite->propGetString(image, kOfxImageEffectPropPixelDepth, 0, &depth) == kOfxStatOK &&
           depth && std::strcmp(depth, kOfxBitDepthFloat) == 0;
}

float* outputPixel(void* data, int rowBytes, const OfxRectI& bounds, int components, int x, int y) {
    if (!data || x < bounds.x1 || x >= bounds.x2 || y < bounds.y1 || y >= bounds.y2) return nullptr;
    auto* row = reinterpret_cast<char*>(data) + static_cast<std::ptrdiff_t>(y - bounds.y1) * rowBytes;
    return reinterpret_cast<float*>(row) + static_cast<std::ptrdiff_t>(x - bounds.x1) * components;
}

OfxStatus identityAction(OfxImageEffectHandle instance, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    OfxTime time = 0.0;
    gPropertySuite->propGetDouble(inArgs, kOfxPropTime, 0, &time);
    const double mix = getDoubleAtTime(instance, kParamMix, time, 1.0);
    if (mix <= 0.0) {
        gPropertySuite->propSetString(outArgs, kOfxPropName, 0, kOfxImageEffectSimpleSourceClipName);
        gPropertySuite->propSetDouble(outArgs, kOfxPropTime, 0, time);
        return kOfxStatOK;
    }
    return kOfxStatReplyDefault;
}

OfxStatus renderAction(OfxImageEffectHandle instance, OfxPropertySetHandle inArgs) {
    OfxTime time = 0.0;
    OfxRectI renderWindow{};
    gPropertySuite->propGetDouble(inArgs, kOfxPropTime, 0, &time);
    gPropertySuite->propGetIntN(inArgs, kOfxImageEffectPropRenderWindow, 4, &renderWindow.x1);

    OfxImageClipHandle sourceClip = nullptr;
    OfxImageClipHandle outputClip = nullptr;
    if (gImageEffectSuite->clipGetHandle(instance, kOfxImageEffectSimpleSourceClipName, &sourceClip, nullptr) != kOfxStatOK ||
        gImageEffectSuite->clipGetHandle(instance, kOfxImageEffectOutputClipName, &outputClip, nullptr) != kOfxStatOK) {
        return kOfxStatFailed;
    }

    OfxPropertySetHandle sourceImage = nullptr;
    OfxPropertySetHandle outputImage = nullptr;
    OfxStatus result = kOfxStatOK;

    if (gImageEffectSuite->clipGetImage(sourceClip, time, nullptr, &sourceImage) != kOfxStatOK || !sourceImage) {
        return gImageEffectSuite->abort(instance) ? kOfxStatOK : kOfxStatFailed;
    }
    if (gImageEffectSuite->clipGetImage(outputClip, time, nullptr, &outputImage) != kOfxStatOK || !outputImage) {
        gImageEffectSuite->clipReleaseImage(sourceImage);
        return gImageEffectSuite->abort(instance) ? kOfxStatOK : kOfxStatFailed;
    }

    do {
        const int srcComps = componentCount(sourceImage);
        const int dstComps = componentCount(outputImage);
        if ((srcComps != 3 && srcComps != 4) || (dstComps != 3 && dstComps != 4) ||
            !isFloatImage(sourceImage) || !isFloatImage(outputImage)) {
            result = kOfxStatErrUnsupported;
            break;
        }

        void* srcData = nullptr;
        void* dstData = nullptr;
        int srcRowBytes = 0;
        int dstRowBytes = 0;
        OfxRectI srcBounds{};
        OfxRectI dstBounds{};
        gPropertySuite->propGetPointer(sourceImage, kOfxImagePropData, 0, &srcData);
        gPropertySuite->propGetPointer(outputImage, kOfxImagePropData, 0, &dstData);
        gPropertySuite->propGetInt(sourceImage, kOfxImagePropRowBytes, 0, &srcRowBytes);
        gPropertySuite->propGetInt(outputImage, kOfxImagePropRowBytes, 0, &dstRowBytes);
        gPropertySuite->propGetIntN(sourceImage, kOfxImagePropBounds, 4, &srcBounds.x1);
        gPropertySuite->propGetIntN(outputImage, kOfxImagePropBounds, 4, &dstBounds.x1);
        if (!srcData || !dstData) {
            result = kOfxStatFailed;
            break;
        }

        const double radius = getDoubleAtTime(instance, kParamRadius, time, 0.10);
        const double radiusStdFactor = getDoubleAtTime(instance, kParamRadiusStdFactor, time, 0.0);
        const double sigmaFilter = getDoubleAtTime(instance, kParamSigmaFilter, time, 0.80);
        const int samples = getIntAtTime(instance, kParamSamples, time, 64);
        const float mix = static_cast<float>(std::clamp(getDoubleAtTime(instance, kParamMix, time, 1.0), 0.0, 1.0));
        const bool colourGrain = getIntAtTime(instance, kParamColourGrain, time, 0) != 0;
        const bool animate = getIntAtTime(instance, kParamAnimate, time, 1) != 0;
        const int seedValue = std::max(1, getIntAtTime(instance, kParamSeed, time, 1));

        std::uint32_t frameSeed = static_cast<std::uint32_t>(seedValue);
        if (animate) {
            const std::int64_t frame = static_cast<std::int64_t>(std::llround(time));
            const std::uint64_t mixed = static_cast<std::uint64_t>(frame) * 747796405ull + 2891336453ull;
            frameSeed ^= static_cast<std::uint32_t>(mixed ^ (mixed >> 32u));
            if (frameSeed == 0u) frameSeed = 1u;
        }

        filmgrain::Params params;
        params.radius = static_cast<float>(radius);
        params.radiusStdFactor = static_cast<float>(radiusStdFactor);
        params.sigmaFilter = static_cast<float>(sigmaFilter);
        params.samples = samples;
        params.seed = frameSeed;

        filmgrain::FloatImageView sourceView;
        sourceView.data = srcData;
        sourceView.rowBytes = srcRowBytes;
        sourceView.x1 = srcBounds.x1;
        sourceView.y1 = srcBounds.y1;
        sourceView.x2 = srcBounds.x2;
        sourceView.y2 = srcBounds.y2;
        sourceView.components = srcComps;

        std::unique_ptr<filmgrain::Sampler> lumaSampler;
        std::unique_ptr<filmgrain::Sampler> redSampler;
        std::unique_ptr<filmgrain::Sampler> greenSampler;
        std::unique_ptr<filmgrain::Sampler> blueSampler;

        if (colourGrain) {
            filmgrain::Params r = params; r.seed = frameSeed ^ 0x243F6A88u;
            filmgrain::Params g = params; g.seed = frameSeed ^ 0x85A308D3u;
            filmgrain::Params b = params; b.seed = frameSeed ^ 0x13198A2Eu;
            redSampler = std::make_unique<filmgrain::Sampler>(r);
            greenSampler = std::make_unique<filmgrain::Sampler>(g);
            blueSampler = std::make_unique<filmgrain::Sampler>(b);
        } else {
            lumaSampler = std::make_unique<filmgrain::Sampler>(params);
        }

        for (int y = renderWindow.y1; y < renderWindow.y2; ++y) {
            if ((y & 7) == 0 && gImageEffectSuite->abort(instance)) break;
            for (int x = renderWindow.x1; x < renderWindow.x2; ++x) {
                float* dst = outputPixel(dstData, dstRowBytes, dstBounds, dstComps, x, y);
                const float* src = sourceView.pixel(x, y);
                if (!dst) continue;
                if (!src) {
                    for (int c = 0; c < dstComps; ++c) dst[c] = 0.0f;
                    continue;
                }

                if (mix <= 0.0f) {
                    for (int c = 0; c < std::min(srcComps, dstComps); ++c) dst[c] = src[c];
                    if (dstComps == 4 && srcComps == 3) dst[3] = 1.0f;
                    continue;
                }

                if (colourGrain) {
                    const float simulated[3] = {
                        redSampler->renderChannel(sourceView, x, y, 0),
                        greenSampler->renderChannel(sourceView, x, y, 1),
                        blueSampler->renderChannel(sourceView, x, y, 2)
                    };
                    for (int c = 0; c < 3; ++c) dst[c] = src[c] + mix * (simulated[c] - src[c]);
                } else {
                    const float sourceLuma = sourceView.lumaClamped(x, y);
                    const float simulatedLuma = lumaSampler->renderLuma(sourceView, x, y);
                    const float delta = simulatedLuma - sourceLuma;
                    for (int c = 0; c < 3; ++c) dst[c] = src[c] + mix * delta;
                }

                if (dstComps == 4) dst[3] = (srcComps == 4) ? src[3] : 1.0f;
            }
        }
    } while (false);

    gImageEffectSuite->clipReleaseImage(sourceImage);
    gImageEffectSuite->clipReleaseImage(outputImage);
    return result;
}

void setHostFunc(OfxHost* host) {
    gHost = host;
}

OfxStatus mainEntryPoint(const char* action, const void* handle,
                         OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    if (!action) return kOfxStatReplyDefault;
    auto effect = reinterpret_cast<OfxImageEffectHandle>(const_cast<void*>(handle));

    if (std::strcmp(action, kOfxActionLoad) == 0) return loadAction();
    if (std::strcmp(action, kOfxActionUnload) == 0) return kOfxStatOK;
    if (std::strcmp(action, kOfxActionDescribe) == 0) return describeAction(effect);
    if (std::strcmp(action, kOfxImageEffectActionDescribeInContext) == 0)
        return describeInContextAction(effect, inArgs);
    if (std::strcmp(action, kOfxImageEffectActionIsIdentity) == 0)
        return identityAction(effect, inArgs, outArgs);
    if (std::strcmp(action, kOfxImageEffectActionRender) == 0)
        return renderAction(effect, inArgs);

    return kOfxStatReplyDefault;
}

OfxPlugin gPlugin = {
    kOfxImageEffectPluginApi,
    kOfxImageEffectPluginApiVersion,
    kPluginIdentifier,
    kPluginVersionMajor,
    kPluginVersionMinor,
    setHostFunc,
    mainEntryPoint
};

} // namespace

extern "C" {

EXPORT int OfxGetNumberOfPlugins(void) {
    return 1;
}

EXPORT OfxPlugin* OfxGetPlugin(int nth) {
    return nth == 0 ? &gPlugin : nullptr;
}

} // extern "C"
