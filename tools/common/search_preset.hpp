#pragma once

#include <othello/search.hpp>
#include <optional>
#include <string_view>

namespace othello::tools {

enum class SearchPreset {
    Default,
    StrongV1,
    StrongV2,
};

struct SearchPresetOptions {
    SearchOptions search_options;
    bool use_iterative_search = false;
};

[[nodiscard]] std::string_view search_preset_name(SearchPreset preset) noexcept;
[[nodiscard]] std::optional<SearchPreset> parse_search_preset(std::string_view text) noexcept;
[[nodiscard]] SearchPresetOptions search_preset_options(SearchPreset preset) noexcept;
[[nodiscard]] SearchOptions apply_strong_v1_search_options(SearchOptions options) noexcept;
[[nodiscard]] SearchOptions apply_strong_v2_search_options(SearchOptions options) noexcept;

} // namespace othello::tools
