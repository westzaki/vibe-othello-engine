#include "search_aspiration.hpp"
#include "search_common.hpp"
#include "search_core.hpp"
#include "search_result_adapter.hpp"
#include "search_root_policy.hpp"
#include "search_runtime_options.hpp"
#include "search_session.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <othello/endgame.hpp>
#include <othello/hash.hpp>
#include <othello/search.hpp>
#include <vector>

namespace othello {
namespace {

using search_detail::diagnostics_options_from;
using search_detail::engine_options_from;
using search_detail::exact_endgame_search_result;
using search_detail::principal_variation_from_vector;
using search_detail::principal_variation_to_vector;
using search_detail::PrincipalVariation;
using search_detail::PrincipalVariationHint;
using search_detail::SearchContext;
using search_detail::SearchDiagnosticsOptions;
using search_detail::SearchEngineOptions;
using search_detail::SearchSessionState;

} // namespace

SearchSession::SearchSession() : state_{std::make_unique<search_detail::SearchSessionState>()} {}

SearchSession::SearchSession(const SearchOptions& options)
    : state_{std::make_unique<search_detail::SearchSessionState>(options)} {}

SearchSession::SearchSession(SearchSession&&) noexcept = default;

SearchSession& SearchSession::operator=(SearchSession&&) noexcept = default;

SearchSession::~SearchSession() = default;

void SearchSession::reset() noexcept {
    if (state_ != nullptr) {
        state_->reset();
    }
}

std::uint32_t SearchSession::generation() const noexcept {
    return state_ == nullptr ? 0 : state_->generation;
}

SearchMode SearchSession::mode() const noexcept {
    return state_ == nullptr ? SearchMode::FixedDepth : state_->mode;
}

std::optional<Square> SearchSession::previous_best_move() const noexcept {
    return state_ == nullptr ? std::nullopt : state_->previous_best_move;
}

std::vector<Square> SearchSession::root_principal_variation() const {
    if (state_ == nullptr) {
        return {};
    }
    return principal_variation_to_vector(state_->root_principal_variation);
}

SearchResult search(SearchSession& session, const Board& board,
                    const SearchOptions& options) noexcept {
    if (session.state_ == nullptr) {
        session.state_ = std::make_unique<search_detail::SearchSessionState>();
    }

    const SearchEngineOptions engine_options = engine_options_from(options);
    const SearchDiagnosticsOptions diagnostics_options = diagnostics_options_from(options);
    begin_session_search(*session.state_, engine_options, resolve_evaluation_config(options),
                         SearchMode::FixedDepth);

    if (should_solve_exact_endgame_at_root(board, options)) {
        session.state_->mode = SearchMode::ExactEndgame;
        SearchResult result = exact_endgame_search_result(
            board, ExactEndgameOptions{.transposition_table_entries =
                                           engine_options.exact_endgame_tt_entries});
        finish_session_search(*session.state_, zobrist_hash(board), result);
        return result;
    }

    const int search_depth = options.max_depth < 0 ? 0 : options.max_depth;
    SearchContext context{*session.state_, engine_options, diagnostics_options, true};
    const PrincipalVariationHint pv_hint{
        .principal_variation = session.state_->root_principal_variation.size == 0
                                   ? nullptr
                                   : &session.state_->root_principal_variation,
        .index = 0,
        .matches_prefix = session.state_->root_principal_variation.size > 0,
    };
    SearchResult result = search_with_context(board, search_depth, context,
                                              session.state_->previous_best_move, pv_hint);
    finish_session_search(*session.state_, zobrist_hash(board), result);
    return result;
}

SearchResult search(const Board& board, const SearchOptions& options) noexcept {
    SearchSession session{options};
    return search(session, board, options);
}

SearchResult search_fixed_depth(const Board& board, int depth) noexcept {
    return search(board, SearchOptions{.max_depth = depth});
}

SearchResult search_iterative(SearchSession& session, const Board& board,
                              const SearchOptions& options) noexcept {
    if (session.state_ == nullptr) {
        session.state_ = std::make_unique<search_detail::SearchSessionState>();
    }

    const SearchEngineOptions engine_options = engine_options_from(options);
    const SearchDiagnosticsOptions diagnostics_options = diagnostics_options_from(options);
    begin_session_search(*session.state_, engine_options, resolve_evaluation_config(options),
                         SearchMode::Iterative);

    if (should_solve_exact_endgame_at_root(board, options)) {
        session.state_->mode = SearchMode::ExactEndgame;
        SearchResult result = exact_endgame_search_result(
            board, ExactEndgameOptions{.transposition_table_entries =
                                           engine_options.exact_endgame_tt_entries});
        finish_session_search(*session.state_, zobrist_hash(board), result);
        return result;
    }

    const int search_depth = options.max_depth < 0 ? 0 : options.max_depth;
    SearchContext context{*session.state_, engine_options, diagnostics_options, true};

    if (search_depth == 0) {
        SearchResult result = search_with_context(
            board, 0, context, session.state_->previous_best_move, PrincipalVariationHint{});
        finish_session_search(*session.state_, zobrist_hash(board), result);
        return result;
    }

    SearchResult result;
    std::optional<Square> previous_best_move = session.state_->previous_best_move;
    std::optional<int> previous_score;
    std::optional<int> previous_score_delta;
    PrincipalVariation previous_principal_variation = session.state_->root_principal_variation;
    for (int depth = 1; depth <= search_depth; ++depth) {
        const SearchStats stats_before_depth = context.stats;
        const auto depth_start = std::chrono::steady_clock::now();
        const PrincipalVariationHint pv_hint{
            .principal_variation =
                previous_principal_variation.size == 0 ? nullptr : &previous_principal_variation,
            .index = 0,
            .matches_prefix = previous_principal_variation.size > 0,
        };
        if (engine_options.use_aspiration_window && previous_score.has_value()) {
            const int initial_window =
                aspiration_initial_window(engine_options, previous_score_delta);
            result =
                search_aspirated_with_context(board, depth, context, previous_best_move, pv_hint,
                                              *previous_score, initial_window, engine_options);
        } else {
            result = search_with_context(board, depth, context, previous_best_move, pv_hint);
        }
        const auto depth_elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - depth_start);
        if (diagnostics_options.iterative_depth_observer != nullptr) {
            const SearchStats depth_stats = search_stats_delta(context.stats, stats_before_depth);
            const IterativeSearchDepthInfo info{
                .requested_depth = search_depth,
                .completed_depth = depth,
                .previous_score = previous_score,
                .score = result.score,
                .previous_score_delta =
                    previous_score.has_value() ? result.score - *previous_score : 0,
                .best_move = result.best_move,
                .principal_variation = result.principal_variation,
                .stats = depth_stats,
                .elapsed_ns = static_cast<std::uint64_t>(depth_elapsed.count()),
            };
            diagnostics_options.iterative_depth_observer(
                info, diagnostics_options.iterative_depth_observer_user_data);
        }
        previous_score_delta = previous_score.has_value()
                                   ? std::optional<int>{result.score - *previous_score}
                                   : std::nullopt;
        previous_best_move = result.best_move;
        previous_score = result.score;
        previous_principal_variation = principal_variation_from_vector(result.principal_variation);
    }

    finish_session_search(*session.state_, zobrist_hash(board), result);
    return result;
}

SearchResult search_iterative(const Board& board, const SearchOptions& options) noexcept {
    SearchSession session{options};
    return search_iterative(session, board, options);
}

} // namespace othello
