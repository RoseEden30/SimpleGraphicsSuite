#include "FieldOfView.h"

#include "Config.h"

namespace FieldOfView
{
    namespace
    {
        // The engine re-derives worldFOV/firstPersonFOV from the ini default
        // every frame, so a one-time write doesn't stick.
        struct UpdateCamera
        {
            static void thunk(RE::TESCamera* a_camera)
            {
                func(a_camera);

                const auto settings = ActiveSettings();
                if (!settings->masterEnabled || !settings->fieldOfView.customized)
                    return;

                auto* camera = skyrim_cast<RE::PlayerCamera*>(a_camera);
                if (!camera)
                    return;

                auto& runtimeData = camera->GetRuntimeData2();
                runtimeData.worldFOV = settings->fieldOfView.degrees;
                runtimeData.firstPersonFOV = settings->fieldOfView.degrees;
            }
            static inline REL::Relocation<decltype(thunk)> func;
        };
    }

    void InstallHooks()
    {
        const auto target = RELOCATION_ID(49852, 50784).address() + 0x1A6;
        UpdateCamera::func = SKSE::GetTrampoline().write_call<5>(target, UpdateCamera::thunk);

        logger::info("FieldOfView hooks installed");
    }
}
