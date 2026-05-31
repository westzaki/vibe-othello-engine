#include "exact_labels/exact_label_dump.hpp"

#include "common/jsonl.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <ios>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace othello::tools::exact_labels {
namespace {

using Clock = std::chrono::steady_clock;

[[nodiscard]] bool is_ignored_line(std::string_view line) noexcept {
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t' ||
                             line.front() == '\r')) {
        line.remove_prefix(1);
    }
    return line.empty() || line.front() == '#';
}

[[nodiscard]] std::string position_id(std::size_t index) {
    std::ostringstream id;
    id << "pos-" << std::setw(6) << std::setfill('0') << index;
    return id.str();
}

[[nodiscard]] std::string join_board_block(const std::vector<std::string>& lines) {
    std::string text;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) {
            text += '\n';
        }
        text += lines[index];
    }
    return text;
}

[[nodiscard]] int empty_count(const Board& board) noexcept {
    return 64 - static_cast<int>(std::popcount(board.occupied()));
}

[[nodiscard]] int occupied_count(const Board& board) noexcept {
    return static_cast<int>(std::popcount(board.occupied()));
}

[[nodiscard]] std::string side_name(Side side) {
    return side == Side::Black ? "B" : "W";
}

[[nodiscard]] std::string format_label_square(Square square) {
    std::string text = to_string(square);
    if (!text.empty() && text.front() >= 'a' && text.front() <= 'h') {
        text.front() = static_cast<char>(text.front() - 'a' + 'A');
    }
    return text;
}

[[nodiscard]] std::vector<Square> squares_from_bitboard(Bitboard moves) {
    std::vector<Square> squares;
    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const std::optional<Square> square = Square::from_index(index);
        if (square.has_value() && (moves & square->bit()) != 0) {
            squares.push_back(*square);
        }
    }
    return squares;
}

[[nodiscard]] std::vector<std::string> legal_move_labels(const Board& board) {
    std::vector<std::string> moves;
    for (const Square square : squares_from_bitboard(legal_moves(board))) {
        moves.push_back(format_label_square(square));
    }
    if (moves.empty() && pass_turn(board).has_value()) {
        moves.push_back("PASS");
    }
    return moves;
}

[[nodiscard]] std::vector<MoveScoreLabel> compute_move_scores(const Board& board,
                                                              std::uint64_t& nodes) {
    std::vector<MoveScoreLabel> scores;
    const std::vector<Square> legal_squares = squares_from_bitboard(legal_moves(board));
    if (!legal_squares.empty()) {
        scores.reserve(legal_squares.size());
        for (const Square square : legal_squares) {
            const std::optional<Board> next = apply_move(board, square);
            if (!next.has_value()) {
                continue;
            }

            const ExactEndgameResult child = solve_exact_endgame(*next);
            nodes += child.nodes;
            scores.push_back(MoveScoreLabel{
                .move = format_label_square(square),
                .exact_score_side_to_move = -child.disc_margin,
            });
        }
        return scores;
    }

    const std::optional<Board> after_pass = pass_turn(board);
    if (after_pass.has_value()) {
        const ExactEndgameResult child = solve_exact_endgame(*after_pass);
        nodes += child.nodes;
        scores.push_back(MoveScoreLabel{
            .move = "PASS",
            .exact_score_side_to_move = -child.disc_margin,
        });
    }
    return scores;
}

[[nodiscard]] std::vector<std::string>
best_moves_from_scores(const std::vector<MoveScoreLabel>& move_scores) {
    std::vector<std::string> best_moves;
    if (move_scores.empty()) {
        return best_moves;
    }

    const auto best = std::max_element(
        move_scores.begin(), move_scores.end(),
        [](const MoveScoreLabel& lhs, const MoveScoreLabel& rhs) {
            return lhs.exact_score_side_to_move < rhs.exact_score_side_to_move;
        });
    if (best == move_scores.end()) {
        return best_moves;
    }

    for (const MoveScoreLabel& score : move_scores) {
        if (score.exact_score_side_to_move == best->exact_score_side_to_move) {
            best_moves.push_back(score.move);
        }
    }
    return best_moves;
}

[[nodiscard]] std::vector<std::string>
best_moves_from_root_result(const Board& board, const ExactEndgameResult& result) {
    if (result.best_move.has_value()) {
        return {format_label_square(*result.best_move)};
    }
    if (pass_turn(board).has_value()) {
        return {"PASS"};
    }
    return {};
}

void write_string_array(std::ostream& output, std::span<const std::string> values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        write_json_string(output, values[index]);
    }
    output << ']';
}

void write_nullable_string(std::ostream& output, const std::optional<std::string>& value) {
    if (!value.has_value()) {
        output << "null";
        return;
    }
    write_json_string(output, *value);
}

void write_move_scores(std::ostream& output, std::span<const MoveScoreLabel> move_scores) {
    output << '[';
    for (std::size_t index = 0; index < move_scores.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        output << "{\"move\":";
        write_json_string(output, move_scores[index].move);
        output << ",\"exact_score_side_to_move\":"
               << move_scores[index].exact_score_side_to_move << '}';
    }
    output << ']';
}

} // namespace

std::optional<std::vector<InputPosition>> parse_position_text(std::string_view text,
                                                              std::string& error) {
    std::istringstream input{std::string{text}};
    std::vector<InputPosition> positions;
    std::vector<std::string> block;
    int block_start_line = 0;
    int line_number = 0;
    std::string line;

    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (is_ignored_line(line)) {
            continue;
        }

        if (block.empty()) {
            block_start_line = line_number;
        }
        block.push_back(line);

        if (block.size() == 9) {
            const std::string board_text = join_board_block(block);
            const std::optional<Board> board = board_from_string(board_text);
            if (!board.has_value()) {
                error = "invalid board block starting at line " + std::to_string(block_start_line);
                return std::nullopt;
            }

            const std::size_t input_index = positions.size() + 1;
            positions.push_back(InputPosition{
                .position_id = position_id(input_index),
                .board = *board,
                .source_line = block_start_line,
            });
            block.clear();
        }
    }

    if (!block.empty()) {
        error = "incomplete board block starting at line " + std::to_string(block_start_line) +
                ": expected 9 non-comment lines";
        return std::nullopt;
    }

    return positions;
}

ExactLabel make_exact_label(const InputPosition& position, bool include_move_scores) {
    const auto started = Clock::now();
    const ExactEndgameResult result = solve_exact_endgame(position.board);
    std::uint64_t nodes = result.nodes;
    std::vector<MoveScoreLabel> move_scores;
    if (include_move_scores) {
        move_scores = compute_move_scores(position.board, nodes);
    }
    const auto elapsed = Clock::now() - started;

    std::vector<std::string> best_moves =
        include_move_scores ? best_moves_from_scores(move_scores)
                            : best_moves_from_root_result(position.board, result);
    std::optional<std::string> best_move;
    if (!best_moves.empty()) {
        best_move = best_moves.front();
    }

    return ExactLabel{
        .schema = std::string{exact_label_schema},
        .position_id = position.position_id,
        .board_text = to_string(position.board),
        .side_to_move = side_name(position.board.side_to_move),
        .occupied = occupied_count(position.board),
        .empties = empty_count(position.board),
        .legal_moves = legal_move_labels(position.board),
        .exact_score_side_to_move = result.disc_margin,
        .best_moves = best_moves,
        .best_move = best_move,
        .move_scores = move_scores,
        .include_move_scores = include_move_scores,
        .elapsed_ms = std::chrono::duration<double, std::milli>{elapsed}.count(),
        .nodes = nodes,
    };
}

void write_jsonl_record(std::ostream& output, const ExactLabel& label) {
    output << "{\"schema\":";
    write_json_string(output, label.schema);
    output << ",\"position_id\":";
    write_json_string(output, label.position_id);
    output << ",\"board\":";
    write_json_string(output, label.board_text);
    output << ",\"side_to_move\":";
    write_json_string(output, label.side_to_move);
    output << ",\"occupied\":" << label.occupied;
    output << ",\"empties\":" << label.empties;
    output << ",\"legal_moves\":";
    write_string_array(output, label.legal_moves);
    output << ",\"exact_score_side_to_move\":" << label.exact_score_side_to_move;
    output << ",\"best_moves\":";
    write_string_array(output, label.best_moves);
    output << ",\"best_move\":";
    write_nullable_string(output, label.best_move);
    if (label.include_move_scores) {
        output << ",\"move_scores\":";
        write_move_scores(output, label.move_scores);
    }
    output << ",\"elapsed_ms\":" << std::fixed << std::setprecision(3) << label.elapsed_ms
           << std::defaultfloat;
    output << ",\"nodes\":" << label.nodes;
    output << "}\n";
}

bool write_exact_label_jsonl(std::span<const InputPosition> positions, const DumpOptions& options,
                             std::ostream& output, DumpSummary& summary, std::string& error) {
    summary = DumpSummary{.input_positions = positions.size()};

    for (const InputPosition& position : positions) {
        if (options.limit.has_value() && summary.labeled >= *options.limit) {
            break;
        }

        const int empties = empty_count(position.board);
        if (empties > options.max_empties) {
            ++summary.skipped_too_many_empties;
            continue;
        }

        const ExactLabel label = make_exact_label(position, options.include_move_scores);
        write_jsonl_record(output, label);
        ++summary.labeled;
    }

    if (!output) {
        error = "failed to write JSONL output";
        return false;
    }
    return true;
}

} // namespace othello::tools::exact_labels
