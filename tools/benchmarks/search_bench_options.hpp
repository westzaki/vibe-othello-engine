#pragma once

#include "common/evaluator_selection.hpp"
#include "common/output_format.hpp"
#include "common/search_cli_options.hpp"

#include <cstdint>
#include <optional>
#include <othello/othello.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace othello::benchmarks::search_bench {

enum class SearchBenchmarkMode {
    Fixed,
    Iterative,
    Both,
};

enum class SearchRunMode {
    Fixed,
    Iterative,
};

enum class PositionSet {
    Smoke,
    Suite,
    Evaluation,
    Threshold,
};

enum class ExactRootProfileKind {
    Engine,
    Adaptive16Current,
    Adaptive16Cap8,
    Adaptive16Cap6,
    Adaptive16Opponent10,
    Adaptive16Shape,
    Adaptive16Split,
};

struct ExactRootProfile {
    std::string label;
    int threshold = 0;
    othello::ExactEndgameRootPolicy policy = othello::ExactEndgameRootPolicy::FixedThreshold;
    ExactRootProfileKind kind = ExactRootProfileKind::Engine;
};

struct BenchmarkOptions {
    std::vector<int> depths{1, 2, 3, 4, 5};
    std::uint64_t repetitions = 3;
    SearchBenchmarkMode mode = SearchBenchmarkMode::Fixed;
    PositionSet position_set = PositionSet::Smoke;
    bool describe_positions = false;
    bool by_position = false;
    bool emit_iterative_depth_rows = false;
    othello::tools::SearchPreset preset = othello::tools::SearchPreset::Default;
    othello::tools::SearchCliOptions search_cli;
    std::vector<ExactRootProfile> exact_root_profiles;
    othello::tools::OutputFormat output_format = othello::tools::OutputFormat::Text;
    othello::tools::EvaluatorSelection evaluator;

    BenchmarkOptions();
};

[[nodiscard]] ExactRootProfile fixed_exact_root_profile(int threshold);
[[nodiscard]] ExactRootProfile adaptive16_exact_root_profile();
[[nodiscard]] ExactRootProfile experimental_exact_root_profile(std::string label,
                                                               ExactRootProfileKind kind);

void print_usage(std::string_view program_name);

[[nodiscard]] std::string_view mode_name(SearchRunMode mode) noexcept;
[[nodiscard]] std::uint64_t mode_checksum(SearchRunMode mode) noexcept;
[[nodiscard]] std::string_view position_set_name(PositionSet position_set) noexcept;

[[nodiscard]] std::optional<std::vector<int>> parse_depths(std::string_view text);
[[nodiscard]] std::optional<ExactRootProfile> parse_exact_root_profile(std::string_view text);
[[nodiscard]] std::optional<std::vector<ExactRootProfile>>
parse_exact_root_profiles(std::string_view text);
[[nodiscard]] std::string exact_root_profile_list_text(std::span<const ExactRootProfile> profiles);
[[nodiscard]] std::optional<PositionSet> parse_position_set(std::string_view text);
[[nodiscard]] std::optional<SearchBenchmarkMode> parse_benchmark_mode(std::string_view text);
[[nodiscard]] std::optional<BenchmarkOptions> parse_options(std::span<char* const> args);

} // namespace othello::benchmarks::search_bench
