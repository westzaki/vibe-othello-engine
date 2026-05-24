#pragma once

#include "types.hpp"

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace othello::match_runner {

struct EngineConfigParseResult {
    bool ok = false;
    bool has_config = false;
    ExternalEngineConfig config;
    std::string error;
};

[[nodiscard]] EngineConfigParseResult parse_engine_config_line(std::string_view line);
[[nodiscard]] std::optional<const ExternalEngineConfig*>
find_engine_config(std::string_view name, std::span<const ExternalEngineConfig> configs);

} // namespace othello::match_runner
