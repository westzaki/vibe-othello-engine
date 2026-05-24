#include "engine_config.hpp"

#include "common/cli.hpp"
#include "protocols/nboard/parser.hpp"

#include <string>
#include <utility>

namespace othello::match_runner {
namespace {

[[nodiscard]] std::vector<std::string> split_pipe_fields(std::string_view line) {
    std::vector<std::string> fields;
    while (true) {
        const std::size_t delimiter = line.find('|');
        fields.push_back(tools::nboard::trim_ascii(line.substr(0, delimiter)));
        if (delimiter == std::string_view::npos) {
            break;
        }
        line.remove_prefix(delimiter + 1);
    }
    return fields;
}

} // namespace

EngineConfigParseResult parse_engine_config_line(std::string_view line) {
    const std::string trimmed = tools::nboard::trim_ascii(line);
    if (trimmed.empty() || trimmed.starts_with('#')) {
        return EngineConfigParseResult{.ok = true, .has_config = false};
    }

    const std::vector<std::string> fields = split_pipe_fields(trimmed);
    if (fields.size() < 4) {
        return EngineConfigParseResult{.ok = false,
                                       .error = "expected name|depth|cwd|cmd|arg..."};
    }
    if (fields[0].empty()) {
        return EngineConfigParseResult{.ok = false, .error = "missing engine name"};
    }
    const std::optional<int> depth = tools::parse_positive_int(fields[1]);
    if (!depth.has_value()) {
        return EngineConfigParseResult{.ok = false, .error = "invalid engine depth"};
    }
    if (fields[3].empty()) {
        return EngineConfigParseResult{.ok = false, .error = "missing engine command"};
    }

    ExternalEngineConfig config{
        .name = fields[0],
        .depth = *depth,
        .cwd = fields[2].empty() ? std::nullopt : std::optional<std::string>{fields[2]},
    };
    config.command.reserve(fields.size() - 3);
    for (std::size_t index = 3; index < fields.size(); ++index) {
        config.command.push_back(fields[index]);
    }

    return EngineConfigParseResult{.ok = true, .has_config = true, .config = std::move(config)};
}

std::optional<const ExternalEngineConfig*>
find_engine_config(std::string_view name, std::span<const ExternalEngineConfig> configs) {
    for (const ExternalEngineConfig& config : configs) {
        if (config.name == name) {
            return &config;
        }
    }
    return std::nullopt;
}

} // namespace othello::match_runner
