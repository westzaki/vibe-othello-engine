#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <ostream>
#include <othello/board.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace othello::tools::position_sampler {

struct SamplerOptions {
    std::size_t count = 0;
    std::vector<int> target_empties;
    std::uint64_t seed = 0;
    int max_plies = 128;
    bool unique = true;
    bool allow_terminal = false;
    std::size_t max_attempts = 0;
};

struct SampleSummary {
    std::size_t attempts = 0;
    std::size_t sampled = 0;
    std::size_t duplicates = 0;
    std::size_t discarded_terminal = 0;
    std::size_t discarded_max_plies = 0;
};

[[nodiscard]] int empty_count(const Board& board) noexcept;
[[nodiscard]] std::optional<std::vector<int>>
parse_target_empties(std::string_view text, std::string& error);
[[nodiscard]] std::optional<std::vector<Board>>
sample_positions(const SamplerOptions& options, SampleSummary& summary, std::string& error);
void write_positions(std::ostream& output, std::span<const Board> positions);

} // namespace othello::tools::position_sampler
