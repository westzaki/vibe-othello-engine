#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <othello/board.hpp>
#include <othello/square.hpp>
#include <vector>

namespace othello {

// Exact solver-only counters. These are intentionally separate from SearchStats; when the
// private exact-endgame transposition table is disabled, all TT counters remain zero.
struct ExactEndgameStats {
    std::uint64_t nodes = 0;

    std::uint64_t tt_lookups = 0;
    std::uint64_t tt_hits = 0;
    std::uint64_t tt_exact_hits = 0;
    std::uint64_t tt_lower_hits = 0;
    std::uint64_t tt_upper_hits = 0;
    std::uint64_t tt_stores = 0;
    std::uint64_t tt_overwrites = 0;
    std::uint64_t tt_collisions = 0;
    std::uint64_t tt_rejected_stores = 0;
    std::uint64_t tt_move_ordering_probes = 0;
    std::uint64_t tt_move_ordering_hits = 0;
    std::uint64_t tt_move_ordering_used = 0;
};

namespace exact_endgame_stats_detail {

inline constexpr auto additive_members =
    std::to_array<std::uint64_t ExactEndgameStats::*>({
        &ExactEndgameStats::nodes,
        &ExactEndgameStats::tt_lookups,
        &ExactEndgameStats::tt_hits,
        &ExactEndgameStats::tt_exact_hits,
        &ExactEndgameStats::tt_lower_hits,
        &ExactEndgameStats::tt_upper_hits,
        &ExactEndgameStats::tt_stores,
        &ExactEndgameStats::tt_overwrites,
        &ExactEndgameStats::tt_collisions,
        &ExactEndgameStats::tt_rejected_stores,
        &ExactEndgameStats::tt_move_ordering_probes,
        &ExactEndgameStats::tt_move_ordering_hits,
        &ExactEndgameStats::tt_move_ordering_used,
    });

inline constexpr std::size_t tracked_member_count = additive_members.size();

} // namespace exact_endgame_stats_detail

inline void accumulate_exact_endgame_stats(ExactEndgameStats& total,
                                           const ExactEndgameStats& stats) noexcept {
    for (const auto member : exact_endgame_stats_detail::additive_members) {
        total.*member += stats.*member;
    }
}

struct ExactEndgameResult {
    std::optional<Square> best_move;
    int disc_margin = 0;
    int empties = 0;
    std::uint64_t nodes = 0;
    std::vector<Square> principal_variation;
    ExactEndgameStats stats;
};

struct ExactEndgameOptions {
    // Diagnostic knob for benchmark tools. nullopt keeps the root-empty-count based default
    // capacity. A value of 0 disables the private exact TT. Extremely large requested sizes are
    // treated as invalid and fall back to the default capacity.
    std::optional<std::size_t> transposition_table_entries = std::nullopt;
};

// Solves by exhaustive exact search to game end. This is intended for small-empty-count
// endgame positions; calling it on midgame or initial positions can be extremely expensive.
[[nodiscard]] ExactEndgameResult solve_exact_endgame(const Board& board) noexcept;
[[nodiscard]] ExactEndgameResult solve_exact_endgame(const Board& board,
                                                     const ExactEndgameOptions& options) noexcept;

} // namespace othello
