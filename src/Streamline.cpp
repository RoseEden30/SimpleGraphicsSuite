#include "Streamline.h"

#include <d3d11.h>

namespace Streamline
{
    namespace
    {
        bool       g_attempted = false;
        bool       g_available = false;
        bool       g_dlssSupported = false;
        HMODULE    g_interposer = nullptr;

        PFun_slInit*                  s_slInit = nullptr;
        PFun_slShutdown*              s_slShutdown = nullptr;
        PFun_slIsFeatureSupported*    s_slIsFeatureSupported = nullptr;
        PFun_slSetD3DDevice*          s_slSetD3DDevice = nullptr;
        PFun_slGetNewFrameToken*      s_slGetNewFrameToken = nullptr;
        PFun_slSetConstants*          s_slSetConstants = nullptr;
        PFun_slSetTag*                s_slSetTag = nullptr;
        PFun_slEvaluateFeature*       s_slEvaluateFeature = nullptr;
        PFun_slGetFeatureFunction*    s_slGetFeatureFunction = nullptr;
        PFun_slUpgradeInterface*      s_slUpgradeInterface = nullptr;

        PFun_slDLSSSetOptions*         s_slDLSSSetOptions = nullptr;

        // Same layout Community Shaders uses for these binaries.
        std::filesystem::path PluginDir()
        {
            const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
            return std::filesystem::absolute(
                std::format("Data/Shaders/{}/Streamline", plugin->GetName()));
        }

        void LoggingCallback(sl::LogType a_type, const char* a_msg)
        {
            switch (a_type) {
            case sl::LogType::eWarn:
                logger::warn("Streamline: {}", a_msg);
                break;
            case sl::LogType::eError:
                logger::error("Streamline: {}", a_msg);
                break;
            default:
                logger::info("Streamline: {}", a_msg);
                break;
            }
        }
    }

    void EnsureInitialized()
    {
        if (g_attempted)
            return;
        g_attempted = true;

        const auto interposerPath = PluginDir() / "sl.interposer.dll";
        g_interposer = LoadLibraryW(interposerPath.c_str());
        if (!g_interposer) {
            logger::warn("Streamline: couldn't load {} (error {:#x})", interposerPath.string(), GetLastError());
            return;
        }

        s_slInit = reinterpret_cast<PFun_slInit*>(GetProcAddress(g_interposer, "slInit"));
        s_slShutdown = reinterpret_cast<PFun_slShutdown*>(GetProcAddress(g_interposer, "slShutdown"));
        s_slIsFeatureSupported =
            reinterpret_cast<PFun_slIsFeatureSupported*>(GetProcAddress(g_interposer, "slIsFeatureSupported"));
        s_slSetD3DDevice = reinterpret_cast<PFun_slSetD3DDevice*>(GetProcAddress(g_interposer, "slSetD3DDevice"));
        s_slGetNewFrameToken =
            reinterpret_cast<PFun_slGetNewFrameToken*>(GetProcAddress(g_interposer, "slGetNewFrameToken"));
        s_slSetConstants = reinterpret_cast<PFun_slSetConstants*>(GetProcAddress(g_interposer, "slSetConstants"));
        s_slSetTag = reinterpret_cast<PFun_slSetTag*>(GetProcAddress(g_interposer, "slSetTag"));
        s_slEvaluateFeature =
            reinterpret_cast<PFun_slEvaluateFeature*>(GetProcAddress(g_interposer, "slEvaluateFeature"));
        s_slGetFeatureFunction =
            reinterpret_cast<PFun_slGetFeatureFunction*>(GetProcAddress(g_interposer, "slGetFeatureFunction"));
        s_slUpgradeInterface =
            reinterpret_cast<PFun_slUpgradeInterface*>(GetProcAddress(g_interposer, "slUpgradeInterface"));

        if (!s_slInit || !s_slSetD3DDevice || !s_slGetNewFrameToken || !s_slSetConstants || !s_slSetTag ||
            !s_slEvaluateFeature || !s_slGetFeatureFunction || !s_slUpgradeInterface) {
            logger::warn("Streamline: interposer loaded but missing expected exports");
            return;
        }

        const sl::Feature featuresToLoad[] = { sl::kFeatureDLSS };

        sl::Preferences pref;
        pref.featuresToLoad = featuresToLoad;
        pref.numFeaturesToLoad = _countof(featuresToLoad);
        // Off by default, same as Community Shaders - eDefault/eVerbose spam
        // the log with NGX's own internal init/tensor/preset chatter on
        // every DLSS context (re)creation.
        pref.logLevel = sl::LogLevel::eOff;
        pref.logMessageCallback = LoggingCallback;
        pref.showConsole = false;

        const auto pluginDirAbsolute = PluginDir();
        static std::wstring pluginDirW = pluginDirAbsolute.wstring();
        static const wchar_t* pluginPaths[1] = { pluginDirW.c_str() };
        pref.pathsToPlugins = pluginPaths;
        pref.numPathsToPlugins = 1;

        pref.engine = sl::EngineType::eCustom;
        pref.engineVersion = "1.0.0";
        pref.projectId = "a1e6c99e-3b8b-4a1b-9e2b-6f6b7b1f9c9e";
        pref.renderAPI = sl::RenderAPI::eD3D11;
        pref.flags = sl::PreferenceFlags::eUseManualHooking;

        if (SL_FAILED(result, s_slInit(pref, sl::kSDKVersion))) {
            logger::warn("Streamline: slInit failed (result {})", static_cast<int>(result));
            return;
        }

        g_available = true;
        logger::info("Streamline: initialized");
    }

    bool IsAvailable() { return g_available; }

    bool IsDLSSSupported() { return g_dlssSupported; }

    sl::Result UpgradeInterface(void** a_interface)
    {
        return s_slUpgradeInterface ? s_slUpgradeInterface(a_interface) : sl::Result::eErrorNotInitialized;
    }

    void SetDevice(ID3D11Device* a_device)
    {
        if (!g_available || !s_slSetD3DDevice)
            return;

        if (SL_FAILED(result, s_slSetD3DDevice(a_device))) {
            logger::warn("Streamline: slSetD3DDevice failed (result {})", static_cast<int>(result));
            return;
        }

        sl::AdapterInfo adapterInfo{};
        if (SL_FAILED(result, s_slIsFeatureSupported(sl::kFeatureDLSS, adapterInfo))) {
            logger::warn("Streamline: DLSS not supported via Streamline (result {})", static_cast<int>(result));
            return;
        }

        if (s_slGetFeatureFunction(sl::kFeatureDLSS, "slDLSSSetOptions",
                reinterpret_cast<void*&>(s_slDLSSSetOptions)) != sl::Result::eOk) {
            logger::warn("Streamline: couldn't resolve slDLSSSetOptions");
            return;
        }

        g_dlssSupported = true;
        logger::info("Streamline: DLSS available via Streamline");
    }

    sl::Result GetNewFrameToken(sl::FrameToken*& a_token)
    {
        return s_slGetNewFrameToken ? s_slGetNewFrameToken(a_token, nullptr) : sl::Result::eErrorNotInitialized;
    }

    sl::Result SetConstants(const sl::Constants& a_values, const sl::FrameToken& a_frame, const sl::ViewportHandle& a_viewport)
    {
        return s_slSetConstants ? s_slSetConstants(a_values, a_frame, a_viewport) : sl::Result::eErrorNotInitialized;
    }

    sl::Result SetTag(const sl::ViewportHandle& a_viewport, const sl::ResourceTag* a_tags, std::uint32_t a_count, void* a_context)
    {
        return s_slSetTag ? s_slSetTag(a_viewport, a_tags, a_count, a_context) : sl::Result::eErrorNotInitialized;
    }

    sl::Result EvaluateFeature(sl::Feature a_feature, const sl::FrameToken& a_frame, const sl::BaseStructure** a_inputs,
        std::uint32_t a_count, void* a_context)
    {
        return s_slEvaluateFeature ? s_slEvaluateFeature(a_feature, a_frame, a_inputs, a_count, a_context)
                                    : sl::Result::eErrorNotInitialized;
    }

    sl::Result DLSSSetOptions(const sl::ViewportHandle& a_viewport, const sl::DLSSOptions& a_options)
    {
        return s_slDLSSSetOptions ? s_slDLSSSetOptions(a_viewport, a_options) : sl::Result::eErrorNotInitialized;
    }
}
