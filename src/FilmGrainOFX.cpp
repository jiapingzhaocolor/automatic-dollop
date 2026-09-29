// SPDX-License-Identifier: GPL-3.0-or-later
// OpenFX wrapper for the stochastic film-grain model adapted from
// Newson, Faraj, Galerne, Delon, "Realistic Film Grain Rendering", IPOL 2017.

#include "film_grain_core.h"
#include "ofxImageEffect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#if defined(__APPLE__) || defined(__linux__) || defined(__FreeBSD__)
#  define EXPORT __attribute__((visibility("default")))
#elif defined(_WIN32)
#  define EXPORT OfxExport
#else
#  error Unsupported operating system
#endif

namespace {

constexpr const char* kPluginIdentifier = "io.github.filmgrainofx.FilmGrain";
constexpr const char* kPluginLabel = "Film Grain";
constexpr const char* kPluginGroup = "Film Grain";
constexpr int kPluginVersionMajor = 0;
constexpr int kPluginVersionMinor = 2;

constexpr const char* kParamAmount = "amount";
constexpr const char* kParamRadius = "radius";
constexpr const char* kParamRadiusStdFactor = "radiusStdFactor";
constexpr const char* kParamSigmaFilter = "sigmaFilter";
constexpr const char* kParamSamples = "samples";
constexpr const char* kParamColourGrain = "colourGrain";
constexpr const char* kParamAnimate = "animate";
constexpr const char* kParamSeed = "seed";

OfxHost* gHost = nullptr;
const OfxPropertySuiteV1* gPropertySuite = nullptr;
const OfxImageEffectSuiteV1* gImageEffectSuite = nullptr;
const OfxParameterSuiteV1* gParameterSuite = nullptr;

enum class PixelDepth {
    kUnknown,
    kByte,
    kShort,
    kFloat
};

inline float clamp01(float v) {
    return std::min(std::max(v, 0.0f), 1.0f);
}

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
    gPropertySuite->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 0, kOfxBitDepthByte);
    gPropertySuite->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 1, kOfxBitDepthShort);
    gPropertySuite->propSetString(props, kOfxImageEffectPropSupportedPixelDepths, 2, kOfxBitDepthFloat);
    gPropertySuite->propSetString(props, kOfxImageEffectPluginRenderThreadSafety, 0, kOfxImageEffectRenderFullySafe);

    gPropertySuite->propSetInt(props, kOfxImageEffectPropSupportsTiles, 0, 0);
    gPropertySuite->propSetInt(props, kOfxImageEffectPluginPropHostFrameThreading, 0, 0);
    gPropertySuite->propSetInt(props, kOfxImageEffectPropSupportsMultipleClipDepths, 0, 0);
    gPropertySuite->propSetInt(props, kOfxImageEffectPropTemporalClipAccess, 0, 0);

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

    defineDoubleParam(paramSet, kParamAmount, "Amount",
                      "Overall strength of the grain effect.",
                      1.0, 0.0, 1.0, 0.0, 1.0, 0.01, 2);
    defineDoubleParam(paramSet, kParamRadius, "Grain Size",
                      "Average grain radius in pixel units. Smaller values give finer grain.",
                      0.10, 0.005, 4.0, 0.02, 1.0, 0.005, 3);
    defineDoubleParam(paramSet, kParamRadiusStdFactor, "Size Variation",
                      "Variation of grain radius as a fraction of Grain Size.",
                      0.0, 0.0, 3.0, 0.0, 1.0, 0.01, 3);
    defineDoubleParam(paramSet, kParamSigmaFilter, "Softness",
                      "Integration blur of the Monte-Carlo estimator. Higher values soften the grain.",
                      0.80, 0.0, 4.0, 0.0, 2.0, 0.05, 2);
    defineIntParam(paramSet, kParamSamples, "Quality Samples",
                   "Monte-Carlo samples per pixel. Use 16-64 while working and higher values for final renders.",
                   64, 1, 2000, 8, 800);
    defineBoolParam(paramSet, kParamColourGrain, "Color Grain",
                    "Generate independent grain in R, G and B. Disable for monochrome grain.",
                    false);
    defineBoolParam(paramSet, kParamAnimate, "Animated",
                    "Vary the grain pattern over time. Disable to lock the pattern frame to frame.",
                    true);
    defineIntParam(paramSet, kParamSeed, "Seed",
                   "Base seed for the stochastic grain generator.",
                   1, 1, 2147483647, 1, 10000);

    gParameterSuite->paramDefine(paramSet, kOfxParamTypePage, "Controls", &props);
    gPropertySuite->propSetString(props, kOfxPropLabel, 0, "Controls");
    const char* children[] = {
        kParamAmount, kParamRadius, kParamRadiusStdFactor, kParamSigmaFilter,
        kParamSamples, kParamColourGrain, kParamAnimate, kParamSeed
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

PixelDepth pixelDepth(OfxPropertySetHandle image) {
    char* depth = nullptr;
    if (gPropertySuite->propGetString(image, kOfxImageEffectPropPixelDepth, 0, &depth) != kOfxStatOK || !depth) {
        return PixelDepth::kUnknown;
    }
    if (std::strcmp(depth, kOfxBitDepthByte) == 0) return PixelDepth::kByte;
    if (std::strcmp(depth, kOfxBitDepthShort) == 0) return PixelDepth::kShort;
    if (std::strcmp(depth, kOfxBitDepthFloat) == 0) return PixelDepth::kFloat;
    return PixelDepth::kUnknown;
}

float readPixelComponent(const void* data, int rowBytes, const OfxRectI& bounds,
                         int components, PixelDepth depth, int x, int y, int c) {
    if (!data || x < bounds.x1 || x >= bounds.x2 || y < bounds.y1 || y >= bounds.y2 ||
        c < 0 || c >= components) {
        return 0.0f;
    }

    const auto* row = reinterpret_cast<const char*>(data) +
                      static_cast<std::ptrdiff_t>(y - bounds.y1) * rowBytes;
    const std::ptrdiff_t offset = static_cast<std::ptrdiff_t>(x - bounds.x1) * components + c;

    switch (depth) {
        case PixelDepth::kByte:
            return static_cast<const unsigned char*>(static_cast<const void*>(row))[offset] / 255.0f;
        case PixelDepth::kShort:
            return static_cast<const unsigned short*>(static_cast<const void*>(row))[offset] / 65535.0f;
        case PixelDepth::kFloat:
            return reinterpret_cast<const float*>(row)[offset];
        default:
            return 0.0f;
    }
}

void writePixelComponent(void* data, int rowBytes, const OfxRectI& bounds,
                         int components, PixelDepth depth, int x, int y, int c, float value) {
    if (!data || x < bounds.x1 || x >= bounds.x2 || y < bounds.y1 || y >= bounds.y2 ||
        c < 0 || c >= components) {
        return;
    }

    auto* row = reinterpret_cast<char*>(data) +
                static_cast<std::ptrdiff_t>(y - bounds.y1) * rowBytes;
    const std::ptrdiff_t offset = static_cast<std::ptrdiff_t>(x - bounds.x1) * components + c;
    value = clamp01(value);

    switch (depth) {
        case PixelDepth::kByte:
            static_cast<unsigned char*>(static_cast<void*>(row))[offset] =
                static_cast<unsigned char>(std::lround(value * 255.0f));
            break;
        case PixelDepth::kShort:
            reinterpret_cast<unsigned short*>(row)[offset] =
                static_cast<unsigned short>(std::lround(value * 65535.0f));
            break;
        case PixelDepth::kFloat:
            reinterpret_cast<float*>(row)[offset] = value;
            break;
        default:
            break;
    }
}

std::vector<float> convertSourceToFloat(const void* srcData, int srcRowBytes, const OfxRectI& srcBounds,
                                        int srcComps, PixelDepth srcDepth) {
    const int width = srcBounds.x2 - srcBounds.x1;
    const int height = srcBounds.y2 - srcBounds.y1;
    std::vector<float> out(static_cast<std::size_t>(width * height * srcComps), 0.0f);
    for (int y = srcBounds.y1; y < srcBounds.y2; ++y) {
        for (int x = srcBounds.x1; x < srcBounds.x2; ++x) {
            for (int c = 0; c < srcComps; ++c) {
                const std::size_t idx = static_cast<std::size_t>(((y - srcBounds.y1) * width + (x - srcBounds.x1)) * srcComps + c);
                out[idx] = readPixelComponent(srcData, srcRowBytes, srcBounds, srcComps, srcDepth, x, y, c);
            }
        }
    }
    return out;
}

OfxStatus identityAction(OfxImageEffectHandle instance, OfxPropertySetHandle inArgs, OfxPropertySetHandle outArgs) {
    OfxTime time = 0.0;
    gPropertySuite->propGetDouble(inArgs, kOfxPropTime, 0, &time);
    const double amount = getDoubleAtTime(instance, kParamAmount, time, 1.0);
    if (amount <= 0.0) {
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
        const PixelDepth srcDepth = pixelDepth(sourceImage);
        const PixelDepth dstDepth = pixelDepth(outputImage);
        if ((srcComps != 3 && srcComps != 4) || (dstComps != 3 && dstComps != 4) ||
            srcDepth == PixelDepth::kUnknown || dstDepth == PixelDepth::kUnknown) {
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
        const float amount = static_cast<float>(std::clamp(getDoubleAtTime(instance, kParamAmount, time, 1.0), 0.0, 1.0));
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

        const std::vector<float> sourceFloat = convertSourceToFloat(srcData, srcRowBytes, srcBounds, srcComps, srcDepth);
        filmgrain::FloatImageView sourceView;
        sourceView.data = sourceFloat.data();
        sourceView.rowBytes = (srcBounds.x2 - srcBounds.x1) * srcComps * static_cast<int>(sizeof(float));
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
                float src[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                for (int c = 0; c < srcComps; ++c) src[c] = readPixelComponent(srcData, srcRowBytes, srcBounds, srcComps, srcDepth, x, y, c);

                float out[4] = {src[0], src[1], src[2], (srcComps == 4 ? src[3] : 1.0f)};
                if (amount > 0.0f) {
                    if (colourGrain) {
                        const float simulated[3] = {
                            redSampler->renderChannel(sourceView, x, y, 0),
                            greenSampler->renderChannel(sourceView, x, y, 1),
                            blueSampler->renderChannel(sourceView, x, y, 2)
                        };
                        for (int c = 0; c < 3; ++c) out[c] = src[c] + amount * (simulated[c] - src[c]);
                    } else {
                        const float sourceLuma = sourceView.lumaClamped(x, y);
                        const float simulatedLuma = lumaSampler->renderLuma(sourceView, x, y);
                        const float delta = simulatedLuma - sourceLuma;
                        for (int c = 0; c < 3; ++c) out[c] = src[c] + amount * delta;
                    }
                }

                for (int c = 0; c < std::min(3, dstComps); ++c) {
                    writePixelComponent(dstData, dstRowBytes, dstBounds, dstComps, dstDepth, x, y, c, out[c]);
                }
                if (dstComps == 4) {
                    writePixelComponent(dstData, dstRowBytes, dstBounds, dstComps, dstDepth, x, y, 3, out[3]);
                }
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
