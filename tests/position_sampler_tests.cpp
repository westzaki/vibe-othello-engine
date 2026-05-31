#include "exact_labels/exact_label_dump.hpp"
#include "position_sampler/position_sampler.hpp"

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <othello/notation.hpp>
#include <span>
#include <sstream>
#include <string>
#include <vector>

namespace {

using othello::Board;
using othello::tools::position_sampler::SamplerOptions;
using othello::tools::position_sampler::SampleSummary;

[[nodiscard]] SamplerOptions small_options(std::uint64_t seed) {
    return SamplerOptions{
        .count = 8,
        .target_empties = {56, 58},
        .seed = seed,
        .max_plies = 16,
        .unique = true,
        .allow_terminal = false,
        .max_attempts = 500,
    };
}

[[nodiscard]] std::vector<Board> sample_or_fail(const SamplerOptions& options) {
    SampleSummary summary;
    std::string error;
    const auto samples = othello::tools::position_sampler::sample_positions(options, summary, error);
    INFO(error);
    REQUIRE(samples.has_value());
    return *samples;
}

[[nodiscard]] std::string serialize(std::span<const Board> boards) {
    std::ostringstream output;
    othello::tools::position_sampler::write_positions(output, boards);
    return output.str();
}

} // namespace

TEST_CASE("Position sampler is deterministic for the same seed", "[position-sampler]") {
    const std::vector<Board> first = sample_or_fail(small_options(20260531));
    const std::vector<Board> second = sample_or_fail(small_options(20260531));

    CHECK(serialize(first) == serialize(second));
}

TEST_CASE("Position sampler changes output for different seeds", "[position-sampler]") {
    const std::vector<Board> first = sample_or_fail(small_options(20260531));
    const std::vector<Board> second = sample_or_fail(small_options(20260532));

    CHECK(serialize(first) != serialize(second));
}

TEST_CASE("Position sampler respects count and target empties", "[position-sampler]") {
    const SamplerOptions options = small_options(7);

    const std::vector<Board> samples = sample_or_fail(options);

    REQUIRE(samples.size() == options.count);
    for (const Board& board : samples) {
        const int empties = othello::tools::position_sampler::empty_count(board);
        CHECK((empties == 56 || empties == 58));
    }
}

TEST_CASE("Position sampler output parses with exact-label board parser",
          "[position-sampler]") {
    const std::vector<Board> samples = sample_or_fail(small_options(99));
    std::string error;

    const auto parsed =
        othello::tools::exact_labels::parse_position_text(serialize(samples), error);

    INFO(error);
    REQUIRE(parsed.has_value());
    CHECK(parsed->size() == samples.size());
}

TEST_CASE("Position sampler rejects malformed target empties", "[position-sampler]") {
    std::string error;

    const auto empty_segment =
        othello::tools::position_sampler::parse_target_empties("8,,10", error);

    CHECK_FALSE(empty_segment.has_value());
    CHECK(error.contains("empty segment"));

    error.clear();
    const auto out_of_range =
        othello::tools::position_sampler::parse_target_empties("65", error);

    CHECK_FALSE(out_of_range.has_value());
    CHECK(error.contains("[0, 64]"));
}

TEST_CASE("Position sampler fails clearly when request is impossible", "[position-sampler]") {
    const SamplerOptions options{
        .count = 1,
        .target_empties = {64},
        .seed = 20260531,
        .max_plies = 4,
        .unique = true,
        .allow_terminal = false,
        .max_attempts = 3,
    };
    SampleSummary summary;
    std::string error;

    const auto samples = othello::tools::position_sampler::sample_positions(options, summary, error);

    CHECK_FALSE(samples.has_value());
    CHECK(error.contains("unable to sample"));
    CHECK(summary.sampled == 0);
    CHECK(summary.attempts == 3);
}
