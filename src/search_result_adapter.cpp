#include "search_result_adapter.hpp"

#include <othello/endgame.hpp>
#include <othello/search.hpp>
#include <utility>

namespace othello::search_detail {

SearchResult exact_endgame_search_result(const Board& board,
                                         const ExactEndgameOptions& options) noexcept {
    ExactEndgameResult exact = solve_exact_endgame(board, options);
    const SearchStats stats{
        .nodes = exact.nodes,
        .tt_lookups = exact.stats.tt_lookups,
        .tt_hits = exact.stats.tt_hits,
        .tt_exact_hits = exact.stats.tt_exact_hits,
        .tt_lower_hits = exact.stats.tt_lower_hits,
        .tt_upper_hits = exact.stats.tt_upper_hits,
        .tt_stores = exact.stats.tt_stores,
        .tt_overwrites = exact.stats.tt_overwrites,
        .tt_collisions = exact.stats.tt_collisions,
        .tt_rejected_stores = exact.stats.tt_rejected_stores,
        .tt_move_ordering_probes = exact.stats.tt_move_ordering_probes,
        .tt_move_ordering_hits = exact.stats.tt_move_ordering_hits,
        .tt_move_ordering_used = exact.stats.tt_move_ordering_used,
    };

    return SearchResult{
        .best_move = exact.best_move,
        .score = exact.disc_margin * exact_endgame_score_scale,
        .depth = exact.empties,
        .nodes = exact.nodes,
        .principal_variation = std::move(exact.principal_variation),
        .stats = stats,
        .score_kind = SearchScoreKind::ExactDiscMarginScaled,
        .used_exact_endgame = true,
        .exact_disc_margin = exact.disc_margin,
    };
}

} // namespace othello::search_detail
