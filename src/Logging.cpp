#include "Logging.h"

#include <spdlog/sinks/basic_file_sink.h>

namespace Logging
{
    void Init()
    {
        auto path = SKSE::log::log_directory();
        if (!path) {
            return;
        }

        const auto* plugin = SKSE::PluginDeclaration::GetSingleton();
        *path /= std::format("{}.log", plugin->GetName());

        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));

#ifndef NDEBUG
        log->set_level(spdlog::level::trace);
        log->flush_on(spdlog::level::trace);
#else
        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);
#endif

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%H:%M:%S.%e] [%l] %v");
    }

    void SetVerbose(bool a_verbose)
    {
        auto       log = spdlog::default_logger();
        const auto level = a_verbose ? spdlog::level::debug : spdlog::level::info;
        log->set_level(level);
        log->flush_on(level);
    }
}
