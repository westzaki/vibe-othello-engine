#include "benchmarks/search_bench_runner.hpp"

#include "common/evaluator_selection.hpp"
#include "common/search_cli_options.hpp"
#include "common/stats.hpp"
#include "positions/metrics.hpp"
#include "positions/search_positions.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace othello::benchmarks::search_bench {

namespace {

using Clock = std::chrono::steady_clock;
using othello::benchmarks::count_bits;
using othello::benchmarks::edge_bits;
using othello::tools::add_search_stats;

constexpr int exact_endgame_score_scale = 1'000;

} // namespace

[[nodiscard]] std::optional<std::vector<othello::benchmarks::Position>>
make_positions(PositionSet position_set) {
    switch (position_set) {
    case PositionSet::Smoke:
        return othello::benchmarks::make_search_smoke_positions();
    case PositionSet::Suite:
        return othello::benchmarks::make_search_suite_positions();
    case PositionSet::Evaluation:
        return othello::benchmarks::make_search_evaluation_positions();
    case PositionSet::Threshold:
        return othello::benchmarks::make_search_threshold_positions();
    }

    return std::nullopt;
}

[[nodiscard]] int root_empty_count(const othello::Board& board) noexcept {
    return 64 - othello::disc_count(board, othello::Side::Black) -
           othello::disc_count(board, othello::Side::White);
}

[[nodiscard]] int opponent_legal_move_count(const othello::Board& board) noexcept {
    auto opponent_board = board;
    opponent_board.side_to_move = othello::opponent(board.side_to_move);
    return count_bits(othello::legal_moves(opponent_board));
}

[[nodiscard]] bool root_is_edge_heavy(const othello::Board& board) noexcept {
    return count_bits(board.occupied() & edge_bits()) >= 13;
}

[[nodiscard]] bool profile_uses_engine_gate(const ExactRootProfile& profile) noexcept {
    return profile.kind == ExactRootProfileKind::Engine;
}

[[nodiscard]] std::string_view
exact_root_skip_reason_name(othello::ExactEndgameRootSkipReason reason) noexcept {
    switch (reason) {
    case othello::ExactEndgameRootSkipReason::None:
        return "-";
    case othello::ExactEndgameRootSkipReason::Disabled:
        return "disabled";
    case othello::ExactEndgameRootSkipReason::AboveThreshold:
        return "above_threshold";
    case othello::ExactEndgameRootSkipReason::AdaptiveRootPass:
        return "adaptive_root_pass";
    case othello::ExactEndgameRootSkipReason::AdaptiveTooManyLegalMoves:
        return "adaptive_too_many_legal_moves";
    case othello::ExactEndgameRootSkipReason::AdaptiveOpponentTooManyLegalMoves:
        return "adaptive_opponent_too_many_legal_moves";
    }

    return "unknown";
}

[[nodiscard]] BenchmarkExactRootDecision
benchmark_exact_root_decision(const othello::Board& board, const othello::SearchOptions& options,
                              const ExactRootProfile& profile) noexcept {
    if (profile_uses_engine_gate(profile)) {
        const auto decision = othello::decide_exact_endgame_root(board, options);
        return BenchmarkExactRootDecision{
            .solve_exact = decision.solve_exact,
            .empty_count = decision.empty_count,
            .legal_moves_current = decision.legal_moves_current,
            .legal_moves_opponent = decision.legal_moves_opponent,
            .skip_reason = exact_root_skip_reason_name(decision.skip_reason),
        };
    }

    BenchmarkExactRootDecision decision{
        .empty_count = root_empty_count(board),
        .legal_moves_current = count_bits(othello::legal_moves(board)),
        .legal_moves_opponent = opponent_legal_move_count(board),
    };

    if (profile.threshold <= 0) {
        decision.skip_reason = "disabled";
        return decision;
    }
    if (decision.empty_count <= 14) {
        decision.solve_exact = true;
        return decision;
    }
    if (decision.empty_count > 16) {
        decision.skip_reason = "above_threshold";
        return decision;
    }

    const bool root_pass =
        decision.legal_moves_current == 0 && othello::pass_turn(board).has_value();
    if (root_pass) {
        decision.skip_reason = "adaptive_root_pass";
        return decision;
    }

    int current_legal_cap = 10;
    if (profile.kind == ExactRootProfileKind::Adaptive16Cap8) {
        current_legal_cap = 8;
    } else if (profile.kind == ExactRootProfileKind::Adaptive16Cap6) {
        current_legal_cap = 6;
    } else if (profile.kind == ExactRootProfileKind::Adaptive16Split &&
               decision.empty_count == 16) {
        current_legal_cap = 8;
    }

    if (decision.legal_moves_current > current_legal_cap) {
        decision.skip_reason = "adaptive_too_many_legal_moves";
        return decision;
    }

    if (profile.kind == ExactRootProfileKind::Adaptive16Opponent10 &&
        decision.legal_moves_opponent > 10) {
        decision.skip_reason = "adaptive_opponent_too_many_legal_moves";
        return decision;
    }

    if (profile.kind == ExactRootProfileKind::Adaptive16Shape &&
        (decision.legal_moves_current >= 9 || root_is_edge_heavy(board))) {
        decision.skip_reason = "adaptive_shape_guard";
        return decision;
    }

    decision.solve_exact = true;
    return decision;
}

[[nodiscard]] othello::SearchOptions
make_search_options(const BenchmarkOptions& options, int depth,
                    const ExactRootProfile& exact_root_profile) noexcept {
    auto search_options = othello::SearchOptions{.max_depth = depth};
    search_options = othello::tools::apply_search_cli_options(search_options, options.search_cli);
    search_options.exact_endgame_empty_threshold =
        profile_uses_engine_gate(exact_root_profile) ? exact_root_profile.threshold : 0;
    search_options.exact_endgame_root_policy = exact_root_profile.policy;
    return othello::tools::apply_evaluator_selection(search_options, options.evaluator);
}

[[nodiscard]] othello::SearchResult
exact_endgame_search_result(const othello::Board& board,
                            const othello::ExactEndgameOptions& options) {
    othello::ExactEndgameResult exact = othello::solve_exact_endgame(board, options);
    const othello::SearchStats stats{
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

    return othello::SearchResult{
        .best_move = exact.best_move,
        .score = exact.disc_margin * exact_endgame_score_scale,
        .depth = exact.empties,
        .nodes = exact.nodes,
        .principal_variation = std::move(exact.principal_variation),
        .stats = stats,
        .score_kind = othello::SearchScoreKind::ExactDiscMarginScaled,
        .used_exact_endgame = true,
        .exact_disc_margin = exact.disc_margin,
    };
}

struct IterativeDepthObserverData {
    std::vector<IterativeDepthBenchmarkResult>* rows = nullptr;
    std::string_view position_name;
    std::string_view phase;
    std::string_view tags;
    SearchRunMode mode = SearchRunMode::Iterative;
    bool use_transposition_table = false;
    bool store_leaf_tt_entries = true;
    bool use_pvs = false;
    bool use_aspiration_window = false;
    int aspiration_window = 0;
    int aspiration_max_researches = 0;
    othello::AspirationProfile aspiration_profile = othello::AspirationProfile::Fixed;
    std::size_t transposition_table_entries = 0;
    std::string exact_root_profile;
    int empty_count = 0;
    bool exact_root = false;
    std::string exact_skip_reason;
    std::uint64_t board_checksum = 0;
};

[[nodiscard]] othello::SearchResult
search_result_from_iteration_info(const othello::IterativeSearchDepthInfo& info) {
    return othello::SearchResult{
        .best_move = info.best_move,
        .score = info.score,
        .depth = info.completed_depth,
        .nodes = info.stats.nodes,
        .principal_variation = info.principal_variation,
        .stats = info.stats,
    };
}

void observe_iterative_depth(const othello::IterativeSearchDepthInfo& info, void* user_data) {
    auto* data = static_cast<IterativeDepthObserverData*>(user_data);
    if (data == nullptr || data->rows == nullptr) {
        return;
    }

    const othello::SearchResult result = search_result_from_iteration_info(info);
    std::uint64_t result_checksum = othello::benchmarks::mix_checksum(
        othello::benchmarks::search_result_checksum(result), data->board_checksum);
    result_checksum = othello::benchmarks::mix_checksum(result_checksum, mode_checksum(data->mode));
    std::uint64_t work_checksum = othello::benchmarks::mix_checksum(result_checksum, result.nodes);

    data->rows->push_back(IterativeDepthBenchmarkResult{
        .position_name = data->position_name,
        .phase = data->phase,
        .tags = data->tags,
        .mode = data->mode,
        .use_transposition_table = data->use_transposition_table,
        .store_leaf_tt_entries = data->store_leaf_tt_entries,
        .use_pvs = data->use_pvs,
        .use_aspiration_window = data->use_aspiration_window,
        .aspiration_window = data->aspiration_window,
        .aspiration_max_researches = data->aspiration_max_researches,
        .aspiration_profile = data->aspiration_profile,
        .transposition_table_entries = data->transposition_table_entries,
        .exact_root_profile = data->exact_root_profile,
        .empty_count = data->empty_count,
        .exact_root = data->exact_root,
        .exact_skip_reason = data->exact_skip_reason,
        .requested_depth = info.requested_depth,
        .completed_depth = info.completed_depth,
        .previous_score = info.previous_score,
        .score = info.score,
        .previous_score_delta = info.previous_score_delta,
        .best_move = info.best_move,
        .principal_variation = info.principal_variation,
        .elapsed = std::chrono::nanoseconds{info.elapsed_ns},
        .nodes = info.stats.nodes,
        .stats = info.stats,
        .result_checksum = result_checksum,
        .work_checksum = work_checksum,
    });
}

[[nodiscard]] std::optional<int>
best_move_initial_order_rank(std::span<const othello::RootMoveOrderingEntry> moves,
                             std::optional<othello::Square> best_move) noexcept {
    if (!best_move.has_value()) {
        return std::nullopt;
    }
    for (std::size_t index = 0; index < moves.size(); ++index) {
        if (moves[index].move == *best_move) {
            return static_cast<int>(index) + 1;
        }
    }
    return std::nullopt;
}

[[nodiscard]] othello::SearchResult run_search(const othello::Board& board,
                                               const othello::SearchOptions& options,
                                               SearchRunMode mode,
                                               const ExactRootProfile& exact_root_profile) {
    if (!profile_uses_engine_gate(exact_root_profile) &&
        benchmark_exact_root_decision(board, options, exact_root_profile).solve_exact) {
        return exact_endgame_search_result(
            board, othello::ExactEndgameOptions{.transposition_table_entries =
                                                    options.exact_endgame_tt_entries});
    }

    switch (mode) {
    case SearchRunMode::Fixed:
        return othello::search(board, options);
    case SearchRunMode::Iterative:
        return othello::search_iterative(board, options);
    }

    return othello::SearchResult{};
}

[[nodiscard]] SearchBenchmarkResult
benchmark_search(const std::vector<othello::benchmarks::Position>& positions, int depth,
                 std::uint64_t repetitions, const BenchmarkOptions& benchmark_options,
                 SearchRunMode mode, const ExactRootProfile& exact_root_profile) {
    const auto base_search_options =
        make_search_options(benchmark_options, depth, exact_root_profile);
    std::uint64_t result_checksum = 0;
    std::uint64_t work_checksum = 0;
    std::uint64_t searches = 0;
    std::uint64_t total_nodes = 0;
    std::uint64_t exact_root_positions = 0;
    othello::SearchStats total_stats;
    std::optional<othello::Square> sample_best_move;
    int sample_score = 0;
    std::vector<othello::Square> sample_principal_variation;
    othello::SearchScoreKind sample_score_kind = othello::SearchScoreKind::Heuristic;
    bool sample_used_exact_endgame = false;
    std::optional<int> sample_exact_disc_margin;
    std::vector<IterativeDepthBenchmarkResult> iterative_depth_rows;
    if (benchmark_options.emit_iterative_depth_rows && mode == SearchRunMode::Iterative) {
        iterative_depth_rows.reserve(positions.size() * repetitions *
                                     static_cast<std::uint64_t>(std::max(depth, 0)));
    }

    for (const auto& position : positions) {
        if (benchmark_exact_root_decision(position.board, base_search_options, exact_root_profile)
                .solve_exact) {
            ++exact_root_positions;
        }
    }

    const auto start = Clock::now();
    for (std::uint64_t repetition = 0; repetition < repetitions; ++repetition) {
        for (const auto& position : positions) {
            auto search_options = base_search_options;
            const auto exact_decision =
                benchmark_exact_root_decision(position.board, search_options, exact_root_profile);
            IterativeDepthObserverData observer_data;
            if (benchmark_options.emit_iterative_depth_rows && mode == SearchRunMode::Iterative &&
                !exact_decision.solve_exact) {
                observer_data = IterativeDepthObserverData{
                    .rows = &iterative_depth_rows,
                    .position_name = position.name,
                    .phase = position.phase,
                    .tags = position.tags,
                    .mode = mode,
                    .use_transposition_table = search_options.use_transposition_table,
                    .store_leaf_tt_entries = search_options.store_leaf_tt_entries,
                    .use_pvs = search_options.use_pvs,
                    .use_aspiration_window = search_options.use_aspiration_window,
                    .aspiration_window = search_options.aspiration_window,
                    .aspiration_max_researches = search_options.aspiration_max_researches,
                    .aspiration_profile = search_options.aspiration_profile,
                    .transposition_table_entries = search_options.transposition_table_entries,
                    .exact_root_profile = exact_root_profile.label,
                    .empty_count = exact_decision.empty_count,
                    .exact_root = exact_decision.solve_exact,
                    .exact_skip_reason = std::string{exact_decision.skip_reason},
                    .board_checksum = othello::benchmarks::board_checksum(position.board),
                };
                search_options.instrumentation.iterative_depth_observer = observe_iterative_depth;
                search_options.instrumentation.iterative_depth_observer_user_data = &observer_data;
            }
            const auto result =
                run_search(position.board, search_options, mode, exact_root_profile);
            auto stable_result_checksum = othello::benchmarks::mix_checksum(
                othello::benchmarks::search_result_checksum(result),
                othello::benchmarks::board_checksum(position.board));
            stable_result_checksum =
                othello::benchmarks::mix_checksum(stable_result_checksum, mode_checksum(mode));

            result_checksum =
                othello::benchmarks::mix_checksum(result_checksum, stable_result_checksum);
            work_checksum =
                othello::benchmarks::mix_checksum(work_checksum, stable_result_checksum);
            work_checksum = othello::benchmarks::mix_checksum(work_checksum, result.nodes);

            if (searches == 0) {
                sample_best_move = result.best_move;
                sample_score = result.score;
                sample_score_kind = result.score_kind;
                sample_used_exact_endgame = result.used_exact_endgame;
                sample_exact_disc_margin = result.exact_disc_margin;
                sample_principal_variation = result.principal_variation;
            }
            total_nodes += result.nodes;
            add_search_stats(total_stats, result.stats);
            ++searches;
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return SearchBenchmarkResult{
        .name = "search",
        .mode = mode,
        .use_transposition_table = base_search_options.use_transposition_table,
        .store_leaf_tt_entries = base_search_options.store_leaf_tt_entries,
        .use_pvs = base_search_options.use_pvs,
        .use_aspiration_window = base_search_options.use_aspiration_window,
        .aspiration_window = base_search_options.aspiration_window,
        .aspiration_max_researches = base_search_options.aspiration_max_researches,
        .aspiration_profile = base_search_options.aspiration_profile,
        .transposition_table_entries = base_search_options.transposition_table_entries,
        .exact_root_profile = exact_root_profile.label,
        .exact_root_positions = exact_root_positions,
        .exact_root_searches = exact_root_positions * repetitions,
        .position_count = positions.size(),
        .depth = depth,
        .sample_best_move = sample_best_move,
        .sample_score = sample_score,
        .sample_score_kind = sample_score_kind,
        .sample_used_exact_endgame = sample_used_exact_endgame,
        .sample_exact_disc_margin = sample_exact_disc_margin,
        .sample_principal_variation = sample_principal_variation,
        .searches = searches,
        .elapsed = elapsed,
        .total_nodes = total_nodes,
        .total_stats = total_stats,
        .result_checksum = result_checksum,
        .work_checksum = work_checksum,
        .iterative_depth_rows = std::move(iterative_depth_rows),
    };
}

[[nodiscard]] PositionBenchmarkResult
benchmark_position(const othello::benchmarks::Position& position, int depth,
                   std::uint64_t repetitions, const BenchmarkOptions& benchmark_options,
                   SearchRunMode mode, const ExactRootProfile& exact_root_profile) {
    const auto base_search_options =
        make_search_options(benchmark_options, depth, exact_root_profile);
    const int empty_count = root_empty_count(position.board);
    const auto exact_decision =
        benchmark_exact_root_decision(position.board, base_search_options, exact_root_profile);
    const bool exact_root = exact_decision.solve_exact;
    std::uint64_t searches = 0;
    std::uint64_t total_nodes = 0;
    std::uint64_t result_checksum = 0;
    std::uint64_t work_checksum = 0;
    othello::SearchStats total_stats;
    std::optional<othello::Square> sample_best_move;
    int sample_score = 0;
    std::vector<othello::Square> sample_principal_variation;
    othello::SearchScoreKind sample_score_kind = othello::SearchScoreKind::Heuristic;
    bool sample_used_exact_endgame = false;
    std::optional<int> sample_exact_disc_margin;
    std::vector<IterativeDepthBenchmarkResult> iterative_depth_rows;
    if (benchmark_options.emit_iterative_depth_rows && mode == SearchRunMode::Iterative) {
        iterative_depth_rows.reserve(repetitions * static_cast<std::uint64_t>(std::max(depth, 0)));
    }
    RootMoveOrderingDiagnostic root_move_ordering;

    const auto start = Clock::now();
    for (std::uint64_t repetition = 0; repetition < repetitions; ++repetition) {
        auto search_options = base_search_options;
        std::vector<othello::RootMoveOrderingEntry> root_ordering_snapshot;
        if (benchmark_options.emit_iterative_depth_rows && repetition == 0) {
            search_options.instrumentation.root_move_ordering_snapshot = &root_ordering_snapshot;
        }
        IterativeDepthObserverData observer_data;
        if (benchmark_options.emit_iterative_depth_rows && mode == SearchRunMode::Iterative &&
            !exact_root) {
            observer_data = IterativeDepthObserverData{
                .rows = &iterative_depth_rows,
                .position_name = position.name,
                .phase = position.phase,
                .tags = position.tags,
                .mode = mode,
                .use_transposition_table = search_options.use_transposition_table,
                .store_leaf_tt_entries = search_options.store_leaf_tt_entries,
                .use_pvs = search_options.use_pvs,
                .use_aspiration_window = search_options.use_aspiration_window,
                .aspiration_window = search_options.aspiration_window,
                .aspiration_max_researches = search_options.aspiration_max_researches,
                .aspiration_profile = search_options.aspiration_profile,
                .transposition_table_entries = search_options.transposition_table_entries,
                .exact_root_profile = exact_root_profile.label,
                .empty_count = empty_count,
                .exact_root = exact_root,
                .exact_skip_reason = std::string{exact_decision.skip_reason},
                .board_checksum = othello::benchmarks::board_checksum(position.board),
            };
            search_options.instrumentation.iterative_depth_observer = observe_iterative_depth;
            search_options.instrumentation.iterative_depth_observer_user_data = &observer_data;
        }
        const auto result = run_search(position.board, search_options, mode, exact_root_profile);
        auto stable_result_checksum =
            othello::benchmarks::mix_checksum(othello::benchmarks::search_result_checksum(result),
                                              othello::benchmarks::board_checksum(position.board));
        stable_result_checksum =
            othello::benchmarks::mix_checksum(stable_result_checksum, mode_checksum(mode));

        result_checksum =
            othello::benchmarks::mix_checksum(result_checksum, stable_result_checksum);
        work_checksum = othello::benchmarks::mix_checksum(work_checksum, stable_result_checksum);
        work_checksum = othello::benchmarks::mix_checksum(work_checksum, result.nodes);

        if (searches == 0) {
            sample_best_move = result.best_move;
            sample_score = result.score;
            sample_score_kind = result.score_kind;
            sample_used_exact_endgame = result.used_exact_endgame;
            sample_exact_disc_margin = result.exact_disc_margin;
            sample_principal_variation = result.principal_variation;
            root_move_ordering.moves = std::move(root_ordering_snapshot);
            root_move_ordering.best_move_initial_order_rank =
                best_move_initial_order_rank(root_move_ordering.moves, result.best_move);
        }
        total_nodes += result.nodes;
        add_search_stats(total_stats, result.stats);
        ++searches;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);

    return PositionBenchmarkResult{
        .position_name = position.name,
        .phase = position.phase,
        .tags = position.tags,
        .mode = mode,
        .use_transposition_table = base_search_options.use_transposition_table,
        .store_leaf_tt_entries = base_search_options.store_leaf_tt_entries,
        .use_pvs = base_search_options.use_pvs,
        .use_aspiration_window = base_search_options.use_aspiration_window,
        .aspiration_window = base_search_options.aspiration_window,
        .aspiration_max_researches = base_search_options.aspiration_max_researches,
        .aspiration_profile = base_search_options.aspiration_profile,
        .transposition_table_entries = base_search_options.transposition_table_entries,
        .exact_root_profile = exact_root_profile.label,
        .empty_count = empty_count,
        .exact_root = exact_root,
        .exact_skip_reason = std::string{exact_decision.skip_reason},
        .depth = depth,
        .sample_best_move = sample_best_move,
        .sample_score = sample_score,
        .sample_score_kind = sample_score_kind,
        .sample_used_exact_endgame = sample_used_exact_endgame,
        .sample_exact_disc_margin = sample_exact_disc_margin,
        .sample_principal_variation = sample_principal_variation,
        .searches = searches,
        .elapsed = elapsed,
        .total_nodes = total_nodes,
        .total_stats = total_stats,
        .result_checksum = result_checksum,
        .work_checksum = work_checksum,
        .iterative_depth_rows = std::move(iterative_depth_rows),
        .root_move_ordering = std::move(root_move_ordering),
    };
}

} // namespace othello::benchmarks::search_bench
