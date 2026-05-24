#include "analyze/analysis.hpp"

#include "common/formatting.hpp"

#include <iomanip>
#include <iostream>

namespace othello::tools::analyze {

namespace {

[[nodiscard]] std::string_view side_name(Side side) noexcept {
    return side == Side::Black ? "black" : "white";
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
    const SearchOptions search_options{
        .max_depth = options.depth,
        .use_transposition_table = options.use_transposition_table,
        .transposition_table_entries = options.transposition_table_entries,
        .exact_endgame_empty_threshold = options.exact_endgame_empty_threshold,
    };

    if (options.mode == AnalysisMode::Fixed) {
        return search(board, search_options);
    }
    return search_iterative(board, search_options);
}

void print_report(const Board& board, const AnalysisOptions& options, const SearchResult& result,
                  std::chrono::nanoseconds elapsed) {
    const Bitboard moves = legal_moves(board);
    const bool no_legal_moves = moves == 0;
    const bool pass_available = pass_turn(board).has_value();
    const bool game_over = is_game_over(board);
    const EvaluationBreakdown evaluation =
        evaluate_basic_breakdown(board, board.side_to_move);

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
              << "exact_endgame_threshold: " << options.exact_endgame_empty_threshold << '\n'
              << "elapsed_ms: " << std::fixed << std::setprecision(3)
              << elapsed_ms(elapsed) << '\n'
              << "best_move: " << format_square(result.best_move) << '\n'
              << "score: " << result.score << '\n'
              << "nodes: " << result.nodes << '\n'
              << "principal_variation: " << format_principal_variation(result.principal_variation)
              << '\n'
              << "game_over: " << (game_over ? "yes" : "no") << '\n'
              << "no_legal_moves: " << (no_legal_moves ? "yes" : "no") << '\n'
              << "pass_available: " << (pass_available ? "yes" : "no") << '\n'
              << '\n'
              << "evaluation_breakdown:\n"
              << "  side: " << side_name(board.side_to_move) << '\n'
              << "  terminal: " << (evaluation.terminal ? "yes" : "no") << '\n'
              << "  disc_difference: " << evaluation.disc_difference << '\n'
              << "  disc_difference_weight: " << evaluation.disc_difference_weight << '\n'
              << "  disc_difference_score: " << evaluation.disc_difference_score << '\n'
              << "  mobility: " << evaluation.mobility << '\n'
              << "  mobility_weight: " << evaluation.mobility_weight << '\n'
              << "  mobility_score: " << evaluation.mobility_score << '\n'
              << "  corner_occupancy: " << evaluation.corner_occupancy << '\n'
              << "  corner_occupancy_weight: " << evaluation.corner_occupancy_weight << '\n'
              << "  corner_occupancy_score: " << evaluation.corner_occupancy_score << '\n'
              << "  terminal_disc_difference: " << evaluation.terminal_disc_difference << '\n'
              << "  terminal_score_weight: " << evaluation.terminal_score_weight << '\n'
              << "  terminal_score: " << evaluation.terminal_score << '\n'
              << "  total: " << evaluation.total << '\n';
}

} // namespace othello::tools::analyze
