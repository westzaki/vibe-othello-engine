#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <othello/board.hpp>
#include <othello/square.hpp>
#include <vector>

namespace othello {

struct SearchStats {
    std::uint64_t nodes = 0;
    // Depth-limited search observability. game_over_nodes counts terminal
    // no-move/no-pass nodes reached by search, not an eager probe on every node.
    std::uint64_t beta_cutoffs = 0;
    std::uint64_t beta_cutoffs_first_move = 0;
    std::uint64_t searched_moves = 0;
    std::uint64_t legal_move_nodes = 0;
    std::uint64_t eval_calls = 0;
    std::uint64_t pass_nodes = 0;
    std::uint64_t game_over_nodes = 0;

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

    std::uint64_t pvs_scouts = 0;
    std::uint64_t pvs_researches = 0;
    std::uint64_t pvs_scout_cutoffs = 0;

    std::uint64_t dynamic_ordering_nodes = 0;
    std::uint64_t dynamic_ordering_moves = 0;
};

struct SearchResult {
    std::optional<Square> best_move;
    // Depth-limited searches report a heuristic score from the search/evaluator.
    // If SearchOptions::exact_endgame_empty_threshold triggers at the root, this
    // is the exact final disc margin converted onto the search score scale.
    int score = 0;
    // Requested/effective depth for depth-limited searches. Exact root endgame
    // results report the input board's empty count instead.
    int depth = 0;
    std::uint64_t nodes = 0;
    std::vector<Square> principal_variation;
    SearchStats stats;
};

struct SearchOptions {
    int max_depth = 5;
    bool use_transposition_table = false;
    std::size_t transposition_table_entries = 1 << 18;
    // Root-only exact endgame cutoff by empty square count. At or below this
    // threshold, search uses the exact endgame solver instead of depth-limited
    // search. Values <= 0 disable it.
    int exact_endgame_empty_threshold = 12;
    bool use_pvs = false;
};

[[nodiscard]] SearchResult search(const Board& board, const SearchOptions& options) noexcept;
[[nodiscard]] SearchResult search_fixed_depth(const Board& board, int depth) noexcept;
[[nodiscard]] SearchResult search_iterative(const Board& board,
                                            const SearchOptions& options) noexcept;

} // namespace othello
