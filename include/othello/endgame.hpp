#pragma once

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

struct ExactEndgameResult {
    std::optional<Square> best_move;
    int disc_margin = 0;
    int empties = 0;
    std::uint64_t nodes = 0;
    std::vector<Square> principal_variation;
    ExactEndgameStats stats;
};

struct ExactEndgameOptions {
    // nullopt keeps the root-empty-count based default capacity. A value of 0 disables the
    // private exact TT for diagnostics.
    std::optional<std::size_t> transposition_table_entries = std::nullopt;
};

// Solves by exhaustive exact search to game end. This is intended for small-empty-count
// endgame positions; calling it on midgame or initial positions can be extremely expensive.
[[nodiscard]] ExactEndgameResult solve_exact_endgame(const Board& board) noexcept;
[[nodiscard]] ExactEndgameResult solve_exact_endgame(const Board& board,
                                                     const ExactEndgameOptions& options) noexcept;

} // namespace othello
