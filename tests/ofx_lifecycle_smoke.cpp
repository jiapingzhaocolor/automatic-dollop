// Load the actual packaged module, then exercise the actions used by a host.
// In particular, ReplyDefault is not accepted for instance creation by Resolve.
#include "ofxImageEffect.h"
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
struct Properties {
    std::map<std::string, std::map<int, std::string>> strings;
};
Properties descriptor;
std::map<std::string, Properties> parameters, clips;
std::map<std::string, std::string> parameterTypes;
OfxPropertySuiteV1 properties{};
OfxImageEffectSuiteV1 effects{};
OfxParameterSuiteV1 params{};

OfxPropertySetHandle asHandle(Properties& p) {
    return reinterpret_cast<OfxPropertySetHandle>(&p);
}
OfxStatus setString(OfxPropertySetHandle h, const char* key, int index, const char* value) {
    if (!h) return kOfxStatErrBadHandle;
    reinterpret_cast<Properties*>(h)->strings[key][index] = value;
    return kOfxStatOK;
}
OfxStatus setInt(OfxPropertySetHandle h, const char*, int, int) {
    return h ? kOfxStatOK : kOfxStatErrBadHandle;
}
OfxStatus setDouble(OfxPropertySetHandle h, const char*, int, double) {
    return h ? kOfxStatOK : kOfxStatErrBadHandle;
}
OfxStatus getProperties(OfxImageEffectHandle, OfxPropertySetHandle* out) {
    *out = asHandle(descriptor);
    return kOfxStatOK;
}
OfxStatus getParams(OfxImageEffectHandle, OfxParamSetHandle* out) {
    *out = reinterpret_cast<OfxParamSetHandle>(&parameters);
    return kOfxStatOK;
}
OfxStatus defineClip(OfxImageEffectHandle, const char* name, OfxPropertySetHandle* out) {
    *out = asHandle(clips[name]);
    return kOfxStatOK;
}
OfxStatus defineParam(OfxParamSetHandle, const char* type, const char* name, OfxPropertySetHandle* out) {
    parameterTypes[name] = type;
    *out = asHandle(parameters[name]);
    return kOfxStatOK;
}
const void* fetchSuite(OfxPropertySetHandle, const char* name, int version) {
    if (version != 1) return nullptr;
    if (std::strcmp(name, kOfxPropertySuite) == 0) return &properties;
    if (std::strcmp(name, kOfxImageEffectSuite) == 0) return &effects;
    if (std::strcmp(name, kOfxParameterSuite) == 0) return &params;
    return nullptr;
}
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    try {
        require(argc == 2, "Usage: ofx_lifecycle_smoke path/to/FilmGrainOFX.ofx");
#ifdef _WIN32
        auto module = LoadLibraryA(argv[1]);
        require(module != nullptr, "Could not load the OFX binary (check architecture/dependencies)");
        auto count = reinterpret_cast<int (*)()>(GetProcAddress(module, "OfxGetNumberOfPlugins"));
        auto get = reinterpret_cast<OfxPlugin* (*)(int)>(GetProcAddress(module, "OfxGetPlugin"));
#else
        auto module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        if (!module) throw std::runtime_error(dlerror());
        auto count = reinterpret_cast<int (*)()>(dlsym(module, "OfxGetNumberOfPlugins"));
        auto get = reinterpret_cast<OfxPlugin* (*)(int)>(dlsym(module, "OfxGetPlugin"));
#endif
        require(count && get, "Missing OpenFX exports");
        require(count() == 1, "Expected one plugin");
        require(get(-1) == nullptr && get(1) == nullptr, "Invalid plugin index must return null");
        auto* plugin = get(0);
        require(plugin && plugin->setHost && plugin->mainEntry, "Invalid plugin entry points");
        require(std::strcmp(plugin->pluginIdentifier, "io.github.filmgrainofx.FilmGrain") == 0,
                "Plugin identity changed; existing projects would lose the effect");
        require(std::strcmp(plugin->pluginApi, kOfxImageEffectPluginApi) == 0 &&
                plugin->apiVersion == kOfxImageEffectPluginApiVersion, "Wrong OpenFX API");

        properties.propSetString = setString;
        properties.propSetInt = setInt;
        properties.propSetDouble = setDouble;
        effects.getPropertySet = getProperties;
        effects.getParamSet = getParams;
        effects.clipDefine = defineClip;
        params.paramDefine = defineParam;
        OfxHost host{asHandle(descriptor), fetchSuite};
        plugin->setHost(&host);
        auto call = [&](const char* action, const void* handle = nullptr,
                        OfxPropertySetHandle inArgs = nullptr) {
            const auto status = plugin->mainEntry(action, handle, inArgs, nullptr);
            if (status != kOfxStatOK)
                throw std::runtime_error(std::string(action) + " returned " + std::to_string(status) + " instead of kOfxStatOK");
        };
        call(kOfxActionLoad);
        call(kOfxActionDescribe, &descriptor);
        require(descriptor.strings[kOfxImageEffectPropSupportedContexts][0] == kOfxImageEffectContextFilter,
                "Filter context missing");
        Properties context;
        setString(asHandle(context), kOfxImageEffectPropContext, 0, kOfxImageEffectContextFilter);
        call(kOfxImageEffectActionDescribeInContext, &descriptor, asHandle(context));
        require(clips.count("Source") && clips.count("Output"), "Required clips missing");
        const char* controls[] = {"amount", "radius", "radiusStdFactor", "sigmaFilter", "samples", "colourGrain", "animate", "seed"};
        require(parameters.size() == 9, "Expected eight controls and one page");
        require(parameterTypes["Controls"] == kOfxParamTypePage, "Controls page missing");
        for (int i = 0; i < 8; ++i) {
            require(parameters.count(controls[i]) == 1, "Control missing");
            require(!parameters[controls[i]].strings[kOfxPropLabel][0].empty(), "Control label missing");
            require(parameters["Controls"].strings[kOfxParamPropPageChild][i] == controls[i], "Control missing from page");
        }
        Properties instance;
        call(kOfxActionCreateInstance, &instance);
        require(plugin->mainEntry("UnknownAction", &instance, nullptr, nullptr) == kOfxStatReplyDefault,
                "Unknown actions must retain default handling");
        call(kOfxActionDestroyInstance, &instance);
        call(kOfxActionUnload);
#ifdef _WIN32
        FreeLibrary(module);
#else
        dlclose(module);
#endif
        std::cout << "OFX load, describe, eight controls, create/destroy and unload passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
