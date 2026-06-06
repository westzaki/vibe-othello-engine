#pragma once

#include "search_common.hpp"
#include "search_ordering.hpp"
#include "search_runtime_options.hpp"
#include "search_session.hpp"
#include "search_tt.hpp"

#include <cstddef>
#include <optional>
#include <othello/evaluation.hpp>
#include <othello/search.hpp>
#include <othello/square.hpp>

namespace othello::search_detail {

struct PrincipalVariationHint {
    const PrincipalVariation* principal_variation = nullptr;
    std::size_t index = 0;
    bool matches_prefix = false;
};

struct MoveOrderingParams {
    int static_corner_score = 3'000;
    int static_edge_score = 2'000;
    int static_normal_score = 1'000;
    int static_x_square_score = 0;

    int dynamic_corner_bonus = 100'000;
    int dynamic_edge_bonus = 1'000;
    int dynamic_opponent_corner_penalty = 80'000;
    int dynamic_opponent_mobility_penalty = 500;
    int dynamic_potential_mobility_penalty = 25;
    int dynamic_static_risk_penalty = 25;
    HistoryKillerOrderingParams history_killer =
        default_history_killer_ordering_params;

    int dynamic_min_depth = 3;
    std::size_t dynamic_min_moves = 5;
};

constexpr MoveOrderingParams default_move_ordering_params{};

struct SearchContext {
    explicit SearchContext(SearchSessionState& session_state, SearchEngineOptions engine,
                           SearchDiagnosticsOptions diagnostics_options,
                           bool enable_dynamic_move_ordering) noexcept
        : session{session_state}, engine_options{engine},
          transpositions{session_state.transpositions},
          transposition_scope{.mode = session_state.mode,
                              .eval_identity = session_state.evaluation_identity},
          dynamic_move_ordering{enable_dynamic_move_ordering},
          use_pvs{engine_options.use_pvs},
          store_leaf_tt_entries{engine_options.store_leaf_tt_entries},
          evaluation_config{session_state.evaluation_config}, diagnostics{diagnostics_options} {}

    SearchStats stats;
    SearchSessionState& session;
    SearchEngineOptions engine_options;
    TranspositionTable& transpositions;
    TranspositionScope transposition_scope;
    MoveOrderingParams move_ordering_params = default_move_ordering_params;
    bool dynamic_move_ordering = false;
    bool use_pvs = false;
    bool store_leaf_tt_entries = true;
    EvaluationConfig evaluation_config = default_evaluation_config();
    SearchDiagnosticsOptions diagnostics;
};

} // namespace othello::search_detail
