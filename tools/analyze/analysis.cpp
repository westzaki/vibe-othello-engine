#include "analyze/analysis.hpp"

#include "common/formatting.hpp"
#include "common/stats.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace othello::tools::analyze {

namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] std::string_view side_name(Side side) noexcept {
    return side == Side::Black ? "black" : "white";
}

[[nodiscard]] std::string_view phase_name(EvaluationPhase phase) noexcept {
    switch (phase) {
    case EvaluationPhase::Opening:
        return "opening";
    case EvaluationPhase::Midgame:
        return "midgame";
    case EvaluationPhase::Late:
        return "late";
    }

    return "unknown";
}

[[nodiscard]] SearchOptions make_search_options(const AnalysisOptions& options) noexcept {
    return apply_evaluator_selection(
        SearchOptions{
            .max_depth = options.depth,
            .use_transposition_table = options.use_transposition_table,
            .transposition_table_entries = options.transposition_table_entries,
            .store_leaf_tt_entries = options.store_leaf_tt_entries,
            .tt_min_probe_depth = options.tt_min_probe_depth,
            .tt_min_store_depth = options.tt_min_store_depth,
            .exact_endgame_empty_threshold = options.exact_endgame_empty_threshold,
            .use_pvs = options.use_pvs,
            .use_aspiration_window = options.use_aspiration_window,
            .aspiration_window = options.aspiration_window,
            .aspiration_max_researches = options.aspiration_max_researches,
        },
        options.evaluator);
}

[[nodiscard]] SearchResult run_search_with_depth(const Board& board, const AnalysisOptions& options,
                                                 int depth) noexcept {
    AnalysisOptions child_options = options;
    child_options.depth = depth < 0 ? 0 : depth;
    const SearchOptions search_options = make_search_options(child_options);

    if (child_options.mode == AnalysisMode::Fixed) {
        return search(board, search_options);
    }
    return search_iterative(board, search_options);
}

void print_evaluation_breakdown(const EvaluationBreakdown& evaluation, std::string_view indent) {
    std::cout
        << indent << "phase: " << phase_name(evaluation.phase) << '\n'
        << indent << "occupied_count: " << evaluation.occupied_count << '\n'
        << indent << "empty_count: " << evaluation.empty_count << '\n'
        << indent << "terminal: " << (evaluation.terminal ? "yes" : "no") << '\n'
        << indent << "disc_difference: " << evaluation.disc_difference << '\n'
        << indent << "disc_difference_weight: " << evaluation.disc_difference_weight << '\n'
        << indent << "disc_difference_score: " << evaluation.disc_difference_score << '\n'
        << indent << "mobility: " << evaluation.mobility << '\n'
        << indent << "mobility_weight: " << evaluation.mobility_weight << '\n'
        << indent << "mobility_score: " << evaluation.mobility_score << '\n'
        << indent << "corner_occupancy: " << evaluation.corner_occupancy << '\n'
        << indent << "corner_occupancy_weight: " << evaluation.corner_occupancy_weight << '\n'
        << indent << "corner_occupancy_score: " << evaluation.corner_occupancy_score << '\n'
        << indent << "potential_mobility: " << evaluation.potential_mobility << '\n'
        << indent << "potential_mobility_weight: " << evaluation.potential_mobility_weight << '\n'
        << indent << "potential_mobility_score: " << evaluation.potential_mobility_score << '\n'
        << indent << "corner_access: " << evaluation.corner_access << '\n'
        << indent << "corner_access_weight: " << evaluation.corner_access_weight << '\n'
        << indent << "corner_access_score: " << evaluation.corner_access_score << '\n'
        << indent << "x_square_danger: " << evaluation.x_square_danger << '\n'
        << indent << "x_square_danger_weight: " << evaluation.x_square_danger_weight << '\n'
        << indent << "x_square_danger_score: " << evaluation.x_square_danger_score << '\n'
        << indent << "frontier: " << evaluation.frontier << '\n'
        << indent << "frontier_weight: " << evaluation.frontier_weight << '\n'
        << indent << "frontier_score: " << evaluation.frontier_score << '\n'
        << indent << "corner_local_2x3: " << evaluation.corner_local_2x3 << '\n'
        << indent << "corner_local_2x3_weight: " << evaluation.corner_local_2x3_weight << '\n'
        << indent << "corner_local_2x3_score: " << evaluation.corner_local_2x3_score << '\n'
        << indent << "corner_2x3_pattern: " << evaluation.corner_2x3_pattern << '\n'
        << indent << "corner_2x3_pattern_weight: " << evaluation.corner_2x3_pattern_weight << '\n'
        << indent << "corner_2x3_pattern_score: " << evaluation.corner_2x3_pattern_score << '\n'
        << indent << "edge_stability_lite: " << evaluation.edge_stability_lite << '\n'
        << indent << "edge_stability_lite_weight: " << evaluation.edge_stability_lite_weight << '\n'
        << indent << "edge_stability_lite_score: " << evaluation.edge_stability_lite_score << '\n'
        << indent << "edge_8_pattern: " << evaluation.edge_8_pattern << '\n'
        << indent << "edge_8_pattern_weight: " << evaluation.edge_8_pattern_weight << '\n'
        << indent << "edge_8_pattern_score: " << evaluation.edge_8_pattern_score << '\n'
        << indent << "pattern_table: " << evaluation.pattern_table << '\n'
        << indent << "pattern_table_weight: " << evaluation.pattern_table_weight << '\n'
        << indent << "pattern_table_score: " << evaluation.pattern_table_score << '\n'
        << indent << "terminal_disc_difference: " << evaluation.terminal_disc_difference << '\n'
        << indent << "terminal_score_weight: " << evaluation.terminal_score_weight << '\n'
        << indent << "terminal_score: " << evaluation.terminal_score << '\n'
        << indent << "total: " << evaluation.total << '\n';
}

[[nodiscard]] double tt_hit_percentage(const SearchStats& stats) noexcept {
    if (stats.tt_lookups == 0) {
        return 0.0;
    }
    return 100.0 * static_cast<double>(stats.tt_hits) / static_cast<double>(stats.tt_lookups);
}

void print_candidate_stats(const SearchStats& stats, std::string_view indent) {
    std::cout << indent << "tt_hit_pct: " << std::fixed << std::setprecision(3)
              << tt_hit_percentage(stats) << '\n'
              << indent << "pvs_scouts: " << stats.pvs_scouts << '\n'
              << indent << "pvs_researches: " << stats.pvs_researches << '\n'
              << indent << "pvs_scout_cutoffs: " << stats.pvs_scout_cutoffs << '\n'
              << indent << "beta_cut_first_move_pct: " << std::fixed << std::setprecision(3)
              << beta_cut_first_move_percentage(stats) << '\n'
              << indent << "aspiration_searches: " << stats.aspiration_searches << '\n'
              << indent << "aspiration_researches: " << stats.aspiration_researches << '\n'
              << indent << "aspiration_fail_lows: " << stats.aspiration_fail_lows << '\n'
              << indent << "aspiration_fail_highs: " << stats.aspiration_fail_highs << '\n'
              << indent << "aspiration_fallbacks: " << stats.aspiration_full_window_fallbacks
              << '\n';
}

void print_indented_board(const Board& board, std::string_view indent) {
    std::istringstream lines{to_string(board)};
    std::string line;
    while (std::getline(lines, line)) {
        std::cout << indent << line << '\n';
    }
}

[[nodiscard]] int candidate_sort_index(const RootCandidateAnalysis& candidate) noexcept {
    if (!candidate.move.has_value()) {
        return -1;
    }
    return candidate.move->index();
}

[[nodiscard]] std::vector<Square>
candidate_principal_variation(std::optional<Square> move,
                              const std::vector<Square>& child_principal_variation) {
    std::vector<Square> principal_variation;
    if (move.has_value()) {
        principal_variation.push_back(*move);
    }
    principal_variation.insert(principal_variation.end(), child_principal_variation.begin(),
                               child_principal_variation.end());
    return principal_variation;
}

} // namespace

std::string_view mode_name(AnalysisMode mode) noexcept {
    switch (mode) {
    case AnalysisMode::Fixed:
        return "fixed";
    case AnalysisMode::Iterative:
        return "iterative";
    }

    return "unknown";
}

SearchResult run_search(const Board& board, const AnalysisOptions& options) noexcept {
    const SearchOptions search_options = make_search_options(options);

    if (options.mode == AnalysisMode::Fixed) {
        return search(board, search_options);
    }
    return search_iterative(board, search_options);
}

std::vector<RootCandidateAnalysis> analyze_root_candidates(const Board& board,
                                                           const AnalysisOptions& options) {
    std::vector<RootCandidateAnalysis> candidates;
    const Side root_side = board.side_to_move;
    const int child_depth = options.depth <= 0 ? 0 : options.depth - 1;
    const Bitboard moves = legal_moves(board);
    const EvaluationConfig evaluation_config =
        resolve_evaluation_config(make_search_options(options));

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const std::optional<Square> square = Square::from_index(index);
        if (!square.has_value() || (moves & square->bit()) == 0) {
            continue;
        }

        const std::optional<Board> child_board = apply_move(board, *square);
        if (!child_board.has_value()) {
            continue;
        }

        const auto start = Clock::now();
        const SearchResult child_search = run_search_with_depth(*child_board, options, child_depth);
        const auto end = Clock::now();

        candidates.push_back(RootCandidateAnalysis{
            .move = square,
            .pass = false,
            .child_board = *child_board,
            .depth = child_depth,
            .score = -child_search.score,
            .child_search = child_search,
            .principal_variation =
                candidate_principal_variation(square, child_search.principal_variation),
            .evaluation_after_move =
                evaluate_basic_breakdown(*child_board, root_side, evaluation_config),
            .elapsed = end - start,
        });
    }

    if (moves == 0) {
        const std::optional<Board> passed_board = pass_turn(board);
        if (passed_board.has_value()) {
            const auto start = Clock::now();
            const SearchResult child_search =
                run_search_with_depth(*passed_board, options, child_depth);
            const auto end = Clock::now();
            candidates.push_back(RootCandidateAnalysis{
                .move = std::nullopt,
                .pass = true,
                .child_board = *passed_board,
                .depth = child_depth,
                .score = -child_search.score,
                .child_search = child_search,
                .principal_variation = child_search.principal_variation,
                .evaluation_after_move =
                    evaluate_basic_breakdown(*passed_board, root_side, evaluation_config),
                .elapsed = end - start,
            });
        }
    }

    std::ranges::sort(candidates, [](const RootCandidateAnalysis& lhs,
                                     const RootCandidateAnalysis& rhs) noexcept {
        if (lhs.score != rhs.score) {
            return lhs.score > rhs.score;
        }
        return candidate_sort_index(lhs) < candidate_sort_index(rhs);
    });
    return candidates;
}

void print_report(const Board& board, const AnalysisOptions& options, const SearchResult& result,
                  std::chrono::nanoseconds elapsed) {
    const Bitboard moves = legal_moves(board);
    const bool no_legal_moves = moves == 0;
    const bool pass_available = pass_turn(board).has_value();
    const bool game_over = is_game_over(board);
    const EvaluationConfig evaluation_config =
        resolve_evaluation_config(make_search_options(options));
    const EvaluationBreakdown evaluation =
        evaluate_basic_breakdown(board, board.side_to_move, evaluation_config);

    std::cout << "Othello position analysis\n"
              << '\n'
              << "input_board:\n"
              << to_string(board) << '\n'
              << '\n'
              << "side_to_move: " << side_name(board.side_to_move) << '\n'
              << "legal_moves: " << format_moves(moves) << '\n'
              << "mode: " << mode_name(options.mode) << '\n'
              << "depth: " << options.depth << '\n'
              << "tt: " << (options.use_transposition_table ? "on" : "off") << '\n'
              << "tt_entries: " << options.transposition_table_entries << '\n'
              << "tt_store_leaf: " << (options.store_leaf_tt_entries ? "on" : "off") << '\n'
              << "tt_min_probe_depth: " << options.tt_min_probe_depth << '\n'
              << "tt_min_store_depth: " << options.tt_min_store_depth << '\n'
              << "pvs: " << (options.use_pvs ? "on" : "off") << '\n'
              << "aspiration: " << (options.use_aspiration_window ? "on" : "off") << '\n'
              << "aspiration_window: " << options.aspiration_window << '\n'
              << "aspiration_max_researches: " << options.aspiration_max_researches << '\n'
              << "exact_endgame_threshold: " << options.exact_endgame_empty_threshold << '\n'
              << "eval_config: "
              << (options.evaluator.config_path.has_value() ? *options.evaluator.config_path
                                                            : "built-in default")
              << '\n'
              << "elapsed_ms: " << std::fixed << std::setprecision(3) << elapsed_ms(elapsed) << '\n'
              << "best_move: " << format_square(result.best_move) << '\n'
              << "score: " << result.score << '\n'
              << "score_kind: " << search_score_kind_name(result.score_kind) << '\n'
              << "used_exact_endgame: " << (result.used_exact_endgame ? "yes" : "no") << '\n'
              << "exact_disc_margin: ";
    if (result.exact_disc_margin.has_value()) {
        std::cout << *result.exact_disc_margin << '\n';
    } else {
        std::cout << "none\n";
    }
    std::cout << "nodes: " << result.nodes << '\n'
              << "principal_variation: " << format_principal_variation(result.principal_variation)
              << '\n'
              << "game_over: " << (game_over ? "yes" : "no") << '\n'
              << "no_legal_moves: " << (no_legal_moves ? "yes" : "no") << '\n'
              << "pass_available: " << (pass_available ? "yes" : "no") << '\n'
              << '\n'
              << "evaluation_breakdown:\n"
              << "  side: " << side_name(board.side_to_move) << '\n';
    print_evaluation_breakdown(evaluation, "  ");

    if (!options.root_candidates) {
        return;
    }

    const std::vector<RootCandidateAnalysis> candidates = analyze_root_candidates(board, options);
    std::cout << '\n' << "root_candidates:\n";
    if (candidates.empty()) {
        std::cout << "  - none\n";
        return;
    }

    for (const RootCandidateAnalysis& candidate : candidates) {
        std::cout << "  - move: " << (candidate.pass ? "pass" : format_square(candidate.move))
                  << '\n'
                  << "    child_board:\n";
        print_indented_board(candidate.child_board, "      ");
        std::cout << "    child_side_to_move: " << side_name(candidate.child_board.side_to_move)
                  << '\n'
                  << "    depth: " << candidate.depth << '\n'
                  << "    score: " << candidate.score << '\n'
                  << "    child_search_score: " << candidate.child_search.score << '\n'
                  << "    pv: " << format_principal_variation(candidate.principal_variation) << '\n'
                  << "    nodes: " << candidate.child_search.nodes << '\n'
                  << "    elapsed_ms: " << std::fixed << std::setprecision(3)
                  << elapsed_ms(candidate.elapsed) << '\n'
                  << "    search_stats:\n";
        print_candidate_stats(candidate.child_search.stats, "      ");
        std::cout << "    evaluation_after_move:\n"
                  << "      side: " << side_name(board.side_to_move) << '\n';
        print_evaluation_breakdown(candidate.evaluation_after_move, "      ");
    }
}

} // namespace othello::tools::analyze
