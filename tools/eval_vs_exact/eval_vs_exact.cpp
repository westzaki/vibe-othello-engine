#include "eval_vs_exact/eval_vs_exact.hpp"

#include "exact_labels/exact_label_dump.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <limits>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace othello::tools::eval_vs_exact {
namespace {

enum class Sign {
    Negative,
    Zero,
    Positive,
};

struct ExactLabelRecord {
    std::string schema;
    std::string position_id;
    std::string board_text;
    std::string side_to_move;
    int empties = 0;
    std::vector<std::string> legal_moves;
    int exact_score_side_to_move = 0;
    std::vector<std::string> best_moves;
    std::optional<std::string> best_move;
    std::vector<exact_labels::MoveScoreLabel> move_scores;
    bool has_move_scores = false;
};

struct FieldSeen {
    bool schema = false;
    bool position_id = false;
    bool board = false;
    bool side_to_move = false;
    bool empties = false;
    bool legal_moves = false;
    bool exact_score_side_to_move = false;
    bool best_moves = false;
    bool best_move = false;
    bool move_scores = false;
};

struct MoveScoreFieldSeen {
    bool move = false;
    bool exact_score_side_to_move = false;
};

class JsonParser {
public:
    explicit JsonParser(std::string_view text) : text_(text) {}

    [[nodiscard]] std::optional<ExactLabelRecord> parse_record(std::string& error) {
        skip_space();
        if (!consume('{')) {
            error = "expected JSON object";
            return std::nullopt;
        }

        ExactLabelRecord record;
        FieldSeen seen;
        skip_space();
        if (consume('}')) {
            error = "missing required field: schema";
            return std::nullopt;
        }

        while (true) {
            std::optional<std::string> key = parse_string(error);
            if (!key.has_value()) {
                return std::nullopt;
            }
            skip_space();
            if (!consume(':')) {
                error = "expected ':' after key: " + *key;
                return std::nullopt;
            }
            skip_space();

            if (*key == "schema") {
                if (!parse_required_string(record.schema, seen.schema, *key, error)) {
                    return std::nullopt;
                }
            } else if (*key == "position_id") {
                if (!parse_required_string(record.position_id, seen.position_id, *key, error)) {
                    return std::nullopt;
                }
            } else if (*key == "board") {
                if (!parse_required_string(record.board_text, seen.board, *key, error)) {
                    return std::nullopt;
                }
            } else if (*key == "side_to_move") {
                if (!parse_required_string(record.side_to_move, seen.side_to_move, *key, error)) {
                    return std::nullopt;
                }
            } else if (*key == "empties") {
                if (!parse_required_int(record.empties, seen.empties, *key, error)) {
                    return std::nullopt;
                }
            } else if (*key == "legal_moves") {
                if (!parse_required_string_array(record.legal_moves, seen.legal_moves, *key,
                                                 error)) {
                    return std::nullopt;
                }
            } else if (*key == "exact_score_side_to_move") {
                if (!parse_required_int(record.exact_score_side_to_move,
                                        seen.exact_score_side_to_move, *key, error)) {
                    return std::nullopt;
                }
            } else if (*key == "best_moves") {
                if (!parse_required_string_array(record.best_moves, seen.best_moves, *key, error)) {
                    return std::nullopt;
                }
            } else if (*key == "best_move") {
                if (seen.best_move) {
                    error = "duplicate field: best_move";
                    return std::nullopt;
                }
                seen.best_move = true;
                record.best_move = parse_nullable_string(error);
                if (!error.empty()) {
                    return std::nullopt;
                }
            } else if (*key == "move_scores") {
                if (seen.move_scores) {
                    error = "duplicate field: move_scores";
                    return std::nullopt;
                }
                seen.move_scores = true;
                record.has_move_scores = true;
                std::optional<std::vector<exact_labels::MoveScoreLabel>> move_scores =
                    parse_move_scores_array(error);
                if (!move_scores.has_value()) {
                    error = "invalid move_scores: " + error;
                    return std::nullopt;
                }
                record.move_scores = *std::move(move_scores);
            } else if (!skip_value(error)) {
                return std::nullopt;
            }

            skip_space();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                error = "expected ',' or '}'";
                return std::nullopt;
            }
            skip_space();
        }

        skip_space();
        if (position_ != text_.size()) {
            error = "unexpected trailing JSON content";
            return std::nullopt;
        }
        if (!validate_required_fields(seen, error)) {
            return std::nullopt;
        }
        if (record.schema != exact_labels::exact_label_schema) {
            error = "unsupported schema: " + record.schema;
            return std::nullopt;
        }
        return record;
    }

private:
    std::string_view text_;
    std::size_t position_ = 0;

    void skip_space() noexcept {
        while (position_ < text_.size()) {
            const char ch = text_[position_];
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                break;
            }
            ++position_;
        }
    }

    [[nodiscard]] bool consume(char expected) noexcept {
        if (position_ >= text_.size() || text_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] std::optional<std::string> parse_string(std::string& error) {
        if (!consume('"')) {
            error = "expected JSON string";
            return std::nullopt;
        }

        std::string value;
        while (position_ < text_.size()) {
            const char ch = text_[position_++];
            if (ch == '"') {
                return value;
            }
            if (ch != '\\') {
                value += ch;
                continue;
            }
            if (position_ >= text_.size()) {
                error = "unterminated JSON escape";
                return std::nullopt;
            }
            const char escaped = text_[position_++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                value += escaped;
                break;
            case 'b':
                value += '\b';
                break;
            case 'f':
                value += '\f';
                break;
            case 'n':
                value += '\n';
                break;
            case 'r':
                value += '\r';
                break;
            case 't':
                value += '\t';
                break;
            case 'u':
                error = "unicode escapes are not supported";
                return std::nullopt;
            default:
                error = "invalid JSON escape";
                return std::nullopt;
            }
        }

        error = "unterminated JSON string";
        return std::nullopt;
    }

    [[nodiscard]] std::optional<int> parse_int(std::string& error) {
        const std::size_t begin = position_;
        if (position_ < text_.size() && text_[position_] == '-') {
            ++position_;
        }
        if (position_ >= text_.size() || text_[position_] < '0' || text_[position_] > '9') {
            error = "expected integer";
            return std::nullopt;
        }
        while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
            ++position_;
        }

        int value = 0;
        const char* first = text_.data() + begin;
        const char* last = text_.data() + position_;
        const auto result = std::from_chars(first, last, value);
        if (result.ec != std::errc{} || result.ptr != last) {
            error = "integer out of range";
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] bool consume_literal(std::string_view literal) noexcept {
        if (text_.substr(position_, literal.size()) != literal) {
            return false;
        }
        position_ += literal.size();
        return true;
    }

    [[nodiscard]] std::optional<std::string> parse_nullable_string(std::string& error) {
        if (text_.substr(position_, 4) == "null") {
            position_ += 4;
            return std::nullopt;
        }
        return parse_string(error);
    }

    [[nodiscard]] std::optional<std::vector<std::string>> parse_string_array(std::string& error) {
        if (!consume('[')) {
            error = "expected string array";
            return std::nullopt;
        }

        std::vector<std::string> values;
        skip_space();
        if (consume(']')) {
            return values;
        }

        while (true) {
            skip_space();
            std::optional<std::string> value = parse_string(error);
            if (!value.has_value()) {
                return std::nullopt;
            }
            values.push_back(*std::move(value));
            skip_space();
            if (consume(']')) {
                return values;
            }
            if (!consume(',')) {
                error = "expected ',' or ']' in string array";
                return std::nullopt;
            }
        }
    }

    [[nodiscard]] std::optional<exact_labels::MoveScoreLabel>
    parse_move_score_object(std::string& error) {
        if (!consume('{')) {
            error = "expected move score object";
            return std::nullopt;
        }

        exact_labels::MoveScoreLabel score;
        MoveScoreFieldSeen seen;
        skip_space();
        if (consume('}')) {
            error = "missing required move_scores field: move";
            return std::nullopt;
        }

        while (true) {
            std::optional<std::string> key = parse_string(error);
            if (!key.has_value()) {
                return std::nullopt;
            }
            skip_space();
            if (!consume(':')) {
                error = "expected ':' after move_scores key: " + *key;
                return std::nullopt;
            }
            skip_space();

            if (*key == "move") {
                if (!parse_required_string(score.move, seen.move, *key, error)) {
                    return std::nullopt;
                }
            } else if (*key == "exact_score_side_to_move") {
                if (!parse_required_int(score.exact_score_side_to_move,
                                        seen.exact_score_side_to_move, *key, error)) {
                    return std::nullopt;
                }
            } else if (!skip_value(error)) {
                return std::nullopt;
            }

            skip_space();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                error = "expected ',' or '}' in move_scores object";
                return std::nullopt;
            }
            skip_space();
        }

        if (!seen.move) {
            error = "missing required move_scores field: move";
            return std::nullopt;
        }
        if (!seen.exact_score_side_to_move) {
            error = "missing required move_scores field: exact_score_side_to_move";
            return std::nullopt;
        }
        return score;
    }

    [[nodiscard]] std::optional<std::vector<exact_labels::MoveScoreLabel>>
    parse_move_scores_array(std::string& error) {
        if (!consume('[')) {
            error = "expected move_scores array";
            return std::nullopt;
        }

        std::vector<exact_labels::MoveScoreLabel> values;
        skip_space();
        if (consume(']')) {
            return values;
        }

        while (true) {
            skip_space();
            std::optional<exact_labels::MoveScoreLabel> value = parse_move_score_object(error);
            if (!value.has_value()) {
                return std::nullopt;
            }
            values.push_back(*std::move(value));
            skip_space();
            if (consume(']')) {
                return values;
            }
            if (!consume(',')) {
                error = "expected ',' or ']' in move_scores array";
                return std::nullopt;
            }
        }
    }

    [[nodiscard]] bool parse_required_string(std::string& target, bool& seen, std::string_view key,
                                             std::string& error) {
        if (seen) {
            error = "duplicate field: " + std::string{key};
            return false;
        }
        seen = true;
        std::optional<std::string> value = parse_string(error);
        if (!value.has_value()) {
            error = "invalid string for field: " + std::string{key};
            return false;
        }
        target = *std::move(value);
        return true;
    }

    [[nodiscard]] bool parse_required_int(int& target, bool& seen, std::string_view key,
                                          std::string& error) {
        if (seen) {
            error = "duplicate field: " + std::string{key};
            return false;
        }
        seen = true;
        std::optional<int> value = parse_int(error);
        if (!value.has_value()) {
            error = "invalid integer for field: " + std::string{key};
            return false;
        }
        target = *value;
        return true;
    }

    [[nodiscard]] bool parse_required_string_array(std::vector<std::string>& target, bool& seen,
                                                   std::string_view key, std::string& error) {
        if (seen) {
            error = "duplicate field: " + std::string{key};
            return false;
        }
        seen = true;
        std::optional<std::vector<std::string>> value = parse_string_array(error);
        if (!value.has_value()) {
            error = "invalid string array for field: " + std::string{key};
            return false;
        }
        target = *std::move(value);
        return true;
    }

    [[nodiscard]] bool skip_number(std::string& error) {
        const std::size_t begin = position_;
        while (position_ < text_.size()) {
            const char ch = text_[position_];
            if ((ch < '0' || ch > '9') && ch != '-' && ch != '+' && ch != '.' && ch != 'e' &&
                ch != 'E') {
                break;
            }
            ++position_;
        }
        if (begin == position_) {
            error = "expected JSON value";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool skip_array(std::string& error) {
        if (!consume('[')) {
            error = "expected '['";
            return false;
        }
        skip_space();
        if (consume(']')) {
            return true;
        }
        while (true) {
            if (!skip_value(error)) {
                return false;
            }
            skip_space();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                error = "expected ',' or ']'";
                return false;
            }
            skip_space();
        }
    }

    [[nodiscard]] bool skip_object(std::string& error) {
        if (!consume('{')) {
            error = "expected '{'";
            return false;
        }
        skip_space();
        if (consume('}')) {
            return true;
        }
        while (true) {
            std::optional<std::string> key = parse_string(error);
            if (!key.has_value()) {
                return false;
            }
            skip_space();
            if (!consume(':')) {
                error = "expected ':' in object";
                return false;
            }
            skip_space();
            if (!skip_value(error)) {
                return false;
            }
            skip_space();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                error = "expected ',' or '}'";
                return false;
            }
            skip_space();
        }
    }

    [[nodiscard]] bool skip_value(std::string& error) {
        skip_space();
        if (position_ >= text_.size()) {
            error = "expected JSON value";
            return false;
        }
        const char ch = text_[position_];
        if (ch == '"') {
            return parse_string(error).has_value();
        }
        if (ch == '{') {
            return skip_object(error);
        }
        if (ch == '[') {
            return skip_array(error);
        }
        if (consume_literal("true") || consume_literal("false") || consume_literal("null")) {
            return true;
        }
        return skip_number(error);
    }

    [[nodiscard]] static bool validate_required_fields(const FieldSeen& seen, std::string& error) {
        if (!seen.schema) {
            error = "missing required field: schema";
        } else if (!seen.position_id) {
            error = "missing required field: position_id";
        } else if (!seen.board) {
            error = "missing required field: board";
        } else if (!seen.side_to_move) {
            error = "missing required field: side_to_move";
        } else if (!seen.empties) {
            error = "missing required field: empties";
        } else if (!seen.legal_moves) {
            error = "missing required field: legal_moves";
        } else if (!seen.exact_score_side_to_move) {
            error = "missing required field: exact_score_side_to_move";
        } else if (!seen.best_moves) {
            error = "missing required field: best_moves";
        } else if (!seen.best_move) {
            error = "missing required field: best_move";
        }
        return error.empty();
    }
};

struct EvaluatedMove {
    std::string move;
    int exact_score_side_to_move = 0;
    int eval_score = 0;
    bool exact_best = false;
};

struct MoveRankAnalysis {
    std::string position_id;
    std::string side_to_move;
    int empties = 0;
    std::vector<std::string> exact_best_moves;
    int exact_best_score = 0;
    std::string evaluator_top_move;
    int evaluator_top_eval_score = 0;
    int evaluator_top_exact_score = 0;
    std::vector<std::string> evaluator_top_moves;
    int evaluator_top_group_best_exact_score = 0;
    bool evaluator_top_group_has_exact_best = false;
    int exact_best_rank_under_evaluator = 0;
    int exact_best_eval_score = 0;
    int evaluator_score_gap_top_minus_exact_best = 0;
    int exact_score_gap_exact_best_minus_top = 0;
    std::vector<EvaluatedMove> ranked_moves;
};

struct RecordAnalysis {
    ExactLabelRecord label;
    Board board;
    int eval_score = 0;
    EvaluationBreakdown breakdown;
    std::optional<MoveRankAnalysis> move_rank;
    std::string move_rank_skip_reason;
};

struct BucketStats {
    std::size_t count = 0;
    std::size_t sign_agreements = 0;
    std::size_t sign_disagreements = 0;
    std::size_t wrong_direction = 0;
    std::size_t exact_wins = 0;
    std::size_t exact_losses = 0;
    std::size_t exact_draws = 0;
    std::size_t eval_positive = 0;
    std::size_t eval_negative = 0;
    std::size_t eval_zero = 0;
};

struct FeatureContribution {
    std::string_view name;
    int score = 0;
};

[[nodiscard]] bool is_empty_line(std::string_view line) noexcept {
    for (const char ch : line) {
        if (ch != ' ' && ch != '\t' && ch != '\r') {
            return false;
        }
    }
    return true;
}

[[nodiscard]] Sign sign_of(int value) noexcept {
    if (value > 0) {
        return Sign::Positive;
    }
    if (value < 0) {
        return Sign::Negative;
    }
    return Sign::Zero;
}

[[nodiscard]] bool signs_agree(Sign exact, Sign eval) noexcept {
    return exact == eval;
}

[[nodiscard]] bool wrong_direction(Sign exact, Sign eval) noexcept {
    return (exact == Sign::Positive && eval == Sign::Negative) ||
           (exact == Sign::Negative && eval == Sign::Positive);
}

[[nodiscard]] std::string_view sign_name(Sign sign) noexcept {
    switch (sign) {
    case Sign::Negative:
        return "negative";
    case Sign::Zero:
        return "zero";
    case Sign::Positive:
        return "positive";
    }

    return "unknown";
}

[[nodiscard]] std::string side_name(Side side) {
    return side == Side::Black ? "B" : "W";
}

[[nodiscard]] int empty_count(const Board& board) noexcept {
    return 64 - std::popcount(board.occupied());
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

[[nodiscard]] std::string eval_selection_description(const EvaluatorSelection& selection) {
    if (selection.config_path.has_value()) {
        return "config `" + *selection.config_path + "`";
    }
    return "preset `" + std::string{evaluation_preset_name(selection.preset)} + "`";
}

[[nodiscard]] std::vector<FeatureContribution>
top_contributions(const EvaluationBreakdown& breakdown) {
    std::vector<FeatureContribution> contributions{
        {.name = "disc_difference", .score = breakdown.disc_difference_score},
        {.name = "mobility", .score = breakdown.mobility_score},
        {.name = "potential_mobility", .score = breakdown.potential_mobility_score},
        {.name = "corner_occupancy", .score = breakdown.corner_occupancy_score},
        {.name = "corner_access", .score = breakdown.corner_access_score},
        {.name = "x_square_danger", .score = breakdown.x_square_danger_score},
        {.name = "frontier", .score = breakdown.frontier_score},
        {.name = "corner_local_2x3", .score = breakdown.corner_local_2x3_score},
        {.name = "corner_2x3_pattern", .score = breakdown.corner_2x3_pattern_score},
        {.name = "edge_stability_lite", .score = breakdown.edge_stability_lite_score},
        {.name = "edge_8_pattern", .score = breakdown.edge_8_pattern_score},
        {.name = "pattern_table", .score = breakdown.pattern_table_score},
        {.name = "terminal_score", .score = breakdown.terminal_score},
    };

    std::erase_if(contributions, [](const FeatureContribution& contribution) noexcept {
        return contribution.score == 0;
    });
    std::ranges::sort(contributions,
                      [](const FeatureContribution& lhs, const FeatureContribution& rhs) noexcept {
                          const int lhs_abs = std::abs(lhs.score);
                          const int rhs_abs = std::abs(rhs.score);
                          if (lhs_abs != rhs_abs) {
                              return lhs_abs > rhs_abs;
                          }
                          return lhs.name < rhs.name;
                      });
    if (contributions.size() > 3) {
        contributions.resize(3);
    }
    return contributions;
}

[[nodiscard]] std::optional<MoveRankAnalysis>
analyze_move_rank_record(const ExactLabelRecord& label, const Board& board,
                         const EvaluationConfig& config, std::string& skip_reason,
                         std::string& error) {
    skip_reason.clear();
    if (!label.has_move_scores) {
        skip_reason = "move_scores missing";
        return std::nullopt;
    }

    std::map<std::string, int> exact_scores;
    for (const exact_labels::MoveScoreLabel& score : label.move_scores) {
        const auto inserted = exact_scores.emplace(score.move, score.exact_score_side_to_move);
        if (!inserted.second) {
            error = "position_id " + label.position_id + ": duplicate move in move_scores: " +
                    score.move;
            return std::nullopt;
        }
    }

    std::map<std::string, Board> child_boards;
    for (const Square square : squares_from_bitboard(legal_moves(board))) {
        const std::optional<Board> child = apply_move(board, square);
        if (!child.has_value()) {
            continue;
        }
        child_boards.emplace(format_label_square(square), *child);
    }

    if (child_boards.empty()) {
        const std::optional<Board> after_pass = pass_turn(board);
        if (after_pass.has_value()) {
            child_boards.emplace("PASS", *after_pass);
        }
    }

    if (child_boards.empty()) {
        skip_reason = "no legal root moves";
        return std::nullopt;
    }
    if (exact_scores.empty()) {
        error = "position_id " + label.position_id +
                ": move_scores empty for non-terminal root";
        return std::nullopt;
    }

    for (const auto& [move, _] : child_boards) {
        if (!exact_scores.contains(move)) {
            error = "position_id " + label.position_id + ": move_scores missing legal move: " +
                    move;
            return std::nullopt;
        }
    }
    for (const auto& [move, _] : exact_scores) {
        if (!child_boards.contains(move)) {
            error = "position_id " + label.position_id + ": move_scores contains illegal move: " +
                    move;
            return std::nullopt;
        }
    }

    int exact_best_score = label.move_scores.front().exact_score_side_to_move;
    for (const auto& [_, exact_score] : exact_scores) {
        exact_best_score = std::max(exact_best_score, exact_score);
    }

    std::vector<EvaluatedMove> ranked_moves;
    ranked_moves.reserve(child_boards.size());
    for (const auto& [move, child] : child_boards) {
        const int exact_score = exact_scores.at(move);
        const EvaluationBreakdown child_breakdown =
            evaluate_basic_breakdown(child, board.side_to_move, config);
        ranked_moves.push_back(EvaluatedMove{
            .move = move,
            .exact_score_side_to_move = exact_score,
            .eval_score = child_breakdown.total,
            .exact_best = exact_score == exact_best_score,
        });
    }

    std::ranges::sort(ranked_moves, [](const EvaluatedMove& lhs,
                                       const EvaluatedMove& rhs) noexcept {
        if (lhs.eval_score != rhs.eval_score) {
            return lhs.eval_score > rhs.eval_score;
        }
        return lhs.move < rhs.move;
    });

    std::vector<std::string> exact_best_moves;
    int exact_best_eval_score = std::numeric_limits<int>::min();
    for (const EvaluatedMove& move : ranked_moves) {
        if (move.exact_best) {
            exact_best_moves.push_back(move.move);
            exact_best_eval_score = std::max(exact_best_eval_score, move.eval_score);
        }
    }

    int exact_best_rank = 1;
    for (const EvaluatedMove& move : ranked_moves) {
        if (move.eval_score > exact_best_eval_score) {
            ++exact_best_rank;
        }
    }

    const EvaluatedMove& top = ranked_moves.front();
    std::vector<std::string> evaluator_top_moves;
    int evaluator_top_group_best_exact_score = top.exact_score_side_to_move;
    bool evaluator_top_group_has_exact_best = false;
    for (const EvaluatedMove& move : ranked_moves) {
        if (move.eval_score != top.eval_score) {
            break;
        }
        evaluator_top_moves.push_back(move.move);
        evaluator_top_group_best_exact_score =
            std::max(evaluator_top_group_best_exact_score, move.exact_score_side_to_move);
        if (move.exact_best) {
            evaluator_top_group_has_exact_best = true;
        }
    }

    return MoveRankAnalysis{
        .position_id = label.position_id,
        .side_to_move = label.side_to_move,
        .empties = label.empties,
        .exact_best_moves = exact_best_moves,
        .exact_best_score = exact_best_score,
        .evaluator_top_move = top.move,
        .evaluator_top_eval_score = top.eval_score,
        .evaluator_top_exact_score = top.exact_score_side_to_move,
        .evaluator_top_moves = evaluator_top_moves,
        .evaluator_top_group_best_exact_score = evaluator_top_group_best_exact_score,
        .evaluator_top_group_has_exact_best = evaluator_top_group_has_exact_best,
        .exact_best_rank_under_evaluator = exact_best_rank,
        .exact_best_eval_score = exact_best_eval_score,
        .evaluator_score_gap_top_minus_exact_best = top.eval_score - exact_best_eval_score,
        .exact_score_gap_exact_best_minus_top =
            exact_best_score - evaluator_top_group_best_exact_score,
        .ranked_moves = ranked_moves,
    };
}

void update_summary(AnalyzerSummary& summary, const RecordAnalysis& analysis) noexcept {
    const Sign exact_sign = sign_of(analysis.label.exact_score_side_to_move);
    const Sign eval_sign = sign_of(analysis.eval_score);
    ++summary.analyzed;

    switch (exact_sign) {
    case Sign::Positive:
        ++summary.exact_wins;
        break;
    case Sign::Negative:
        ++summary.exact_losses;
        break;
    case Sign::Zero:
        ++summary.exact_draws;
        ++summary.exact_draw_handling;
        break;
    }

    switch (eval_sign) {
    case Sign::Positive:
        ++summary.eval_positive;
        break;
    case Sign::Negative:
        ++summary.eval_negative;
        break;
    case Sign::Zero:
        ++summary.eval_zero;
        break;
    }

    if (signs_agree(exact_sign, eval_sign)) {
        ++summary.sign_agreements;
    } else {
        ++summary.sign_disagreements;
    }
    if (wrong_direction(exact_sign, eval_sign)) {
        ++summary.wrong_direction;
    }
}

void update_move_rank_summary(AnalyzerSummary& summary, const RecordAnalysis& analysis) noexcept {
    if (analysis.move_rank.has_value()) {
        ++summary.move_rank_records_with_scores;
        ++summary.move_rank_analyzed;
        summary.move_rank_exact_best_rank_sum += static_cast<std::size_t>(
            analysis.move_rank->exact_best_rank_under_evaluator);
        summary.move_rank_eval_score_gap_sum +=
            analysis.move_rank->evaluator_score_gap_top_minus_exact_best;
        summary.move_rank_exact_score_gap_sum +=
            analysis.move_rank->exact_score_gap_exact_best_minus_top;
        if (analysis.move_rank->evaluator_top_group_has_exact_best) {
            ++summary.move_rank_top_exact_best;
        } else {
            ++summary.move_rank_top_non_best;
        }
        return;
    }

    if (analysis.move_rank_skip_reason == "move_scores missing") {
        ++summary.move_rank_records_missing_scores;
    } else if (analysis.move_rank_skip_reason == "no legal root moves") {
        ++summary.move_rank_records_with_scores;
        ++summary.move_rank_records_no_legal_moves;
    }
}

void update_bucket(BucketStats& bucket, const RecordAnalysis& analysis) noexcept {
    const Sign exact_sign = sign_of(analysis.label.exact_score_side_to_move);
    const Sign eval_sign = sign_of(analysis.eval_score);
    ++bucket.count;
    if (signs_agree(exact_sign, eval_sign)) {
        ++bucket.sign_agreements;
    } else {
        ++bucket.sign_disagreements;
    }
    if (wrong_direction(exact_sign, eval_sign)) {
        ++bucket.wrong_direction;
    }
    switch (exact_sign) {
    case Sign::Positive:
        ++bucket.exact_wins;
        break;
    case Sign::Negative:
        ++bucket.exact_losses;
        break;
    case Sign::Zero:
        ++bucket.exact_draws;
        break;
    }
    switch (eval_sign) {
    case Sign::Positive:
        ++bucket.eval_positive;
        break;
    case Sign::Negative:
        ++bucket.eval_negative;
        break;
    case Sign::Zero:
        ++bucket.eval_zero;
        break;
    }
}

[[nodiscard]] std::string percentage(std::size_t numerator, std::size_t denominator) {
    if (denominator == 0) {
        return "n/a";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << (100.0 * static_cast<double>(numerator) / static_cast<double>(denominator)) << '%';
    return out.str();
}

[[nodiscard]] std::string mean_text(long long sum, std::size_t count) {
    if (count == 0) {
        return "n/a";
    }
    std::ostringstream out;
    out << std::fixed << std::setprecision(3)
        << (static_cast<double>(sum) / static_cast<double>(count));
    return out.str();
}

void write_bucket_table(std::ostream& out, const std::map<std::string, BucketStats>& buckets) {
    out << "| Bucket | Count | Sign Agreement | Wrong Direction | Exact + / - / 0 | Eval + / - / 0 "
           "|\n"
        << "| --- | ---: | ---: | ---: | ---: | ---: |\n";
    for (const auto& [name, bucket] : buckets) {
        out << "| " << name << " | " << bucket.count << " | "
            << percentage(bucket.sign_agreements, bucket.count) << " | " << bucket.wrong_direction
            << " | " << bucket.exact_wins << " / " << bucket.exact_losses << " / "
            << bucket.exact_draws << " | " << bucket.eval_positive << " / " << bucket.eval_negative
            << " / " << bucket.eval_zero << " |\n";
    }
}

void write_case_list(std::ostream& out, std::span<const RecordAnalysis> cases,
                     const AnalyzerOptions& options) {
    if (cases.empty()) {
        out << "None.\n\n";
        return;
    }

    const std::size_t limit = std::min(options.top, cases.size());
    for (std::size_t index = 0; index < limit; ++index) {
        const RecordAnalysis& analysis = cases[index];
        out << "## " << (index + 1) << ". " << analysis.label.position_id << '\n'
            << "- side_to_move: `" << analysis.label.side_to_move << "`\n"
            << "- empties: " << analysis.label.empties << '\n'
            << "- exact_score_side_to_move: " << analysis.label.exact_score_side_to_move << " ("
            << sign_name(sign_of(analysis.label.exact_score_side_to_move)) << ")\n"
            << "- eval_score: " << analysis.eval_score << " ("
            << sign_name(sign_of(analysis.eval_score)) << ", heuristic units)\n"
            << "- phase: `" << phase_name(analysis.breakdown.phase) << "`\n"
            << "- legal_moves: ";
        if (analysis.label.legal_moves.empty()) {
            out << "`-`";
        } else {
            for (std::size_t move_index = 0; move_index < analysis.label.legal_moves.size();
                 ++move_index) {
                if (move_index != 0) {
                    out << ' ';
                }
                out << '`' << analysis.label.legal_moves[move_index] << '`';
            }
        }
        out << '\n' << "- best_move: `";
        if (analysis.label.best_move.has_value()) {
            out << *analysis.label.best_move;
        } else {
            out << "-";
        }
        out << "`\n" << "- best_moves: ";
        if (analysis.label.best_moves.empty()) {
            out << "`-`";
        } else {
            for (std::size_t move_index = 0; move_index < analysis.label.best_moves.size();
                 ++move_index) {
                if (move_index != 0) {
                    out << ' ';
                }
                out << '`' << analysis.label.best_moves[move_index] << '`';
            }
        }
        out << '\n';

        const std::vector<FeatureContribution> contributions =
            top_contributions(analysis.breakdown);
        out << "- top_breakdown_scores: ";
        if (contributions.empty()) {
            out << "`none`";
        } else {
            for (std::size_t contribution_index = 0; contribution_index < contributions.size();
                 ++contribution_index) {
                if (contribution_index != 0) {
                    out << ", ";
                }
                out << '`' << contributions[contribution_index].name << '='
                    << contributions[contribution_index].score << '`';
            }
        }
        out << "\n";

        if (options.include_positions) {
            out << "\n```text\n" << analysis.label.board_text << "\n```\n";
        }
        out << '\n';
    }
}

[[nodiscard]] bool analyze_record(const ExactLabelRecord& label, const EvaluationConfig& config,
                                  bool move_rank_analysis, RecordAnalysis& analysis,
                                  std::string& error) {
    const std::optional<Board> board = board_from_string(label.board_text);
    if (!board.has_value()) {
        error = "position_id " + label.position_id + ": failed to parse board";
        return false;
    }
    if (side_name(board->side_to_move) != label.side_to_move) {
        error = "position_id " + label.position_id + ": side_to_move mismatch";
        return false;
    }
    const int parsed_empties = empty_count(*board);
    if (parsed_empties != label.empties) {
        error = "position_id " + label.position_id + ": empties mismatch";
        return false;
    }

    const EvaluationBreakdown breakdown =
        evaluate_basic_breakdown(*board, board->side_to_move, config);
    analysis = RecordAnalysis{
        .label = label,
        .board = *board,
        .eval_score = breakdown.total,
        .breakdown = breakdown,
    };

    if (move_rank_analysis) {
        std::string skip_reason;
        std::optional<MoveRankAnalysis> rank_analysis =
            analyze_move_rank_record(label, *board, config, skip_reason, error);
        if (!error.empty()) {
            return false;
        }
        analysis.move_rank = std::move(rank_analysis);
        analysis.move_rank_skip_reason = std::move(skip_reason);
    }
    return true;
}

[[nodiscard]] std::optional<ExactLabelRecord> parse_line(std::string_view line, int line_number,
                                                         std::string& error) {
    JsonParser parser{line};
    std::optional<ExactLabelRecord> record = parser.parse_record(error);
    if (!record.has_value()) {
        error = "line " + std::to_string(line_number) + ": " + error;
    }
    return record;
}

[[nodiscard]] std::vector<RecordAnalysis>
wrong_direction_cases(std::span<const RecordAnalysis> analyses) {
    std::vector<RecordAnalysis> cases;
    for (const RecordAnalysis& analysis : analyses) {
        if (wrong_direction(sign_of(analysis.label.exact_score_side_to_move),
                            sign_of(analysis.eval_score))) {
            cases.push_back(analysis);
        }
    }
    std::ranges::sort(cases, [](const RecordAnalysis& lhs, const RecordAnalysis& rhs) noexcept {
        const int lhs_abs = std::abs(lhs.eval_score);
        const int rhs_abs = std::abs(rhs.eval_score);
        if (lhs_abs != rhs_abs) {
            return lhs_abs > rhs_abs;
        }
        return std::abs(lhs.label.exact_score_side_to_move) >
               std::abs(rhs.label.exact_score_side_to_move);
    });
    return cases;
}

[[nodiscard]] std::vector<RecordAnalysis>
high_confidence_cases(std::span<const RecordAnalysis> analyses, int threshold) {
    std::vector<RecordAnalysis> cases;
    for (const RecordAnalysis& analysis : analyses) {
        if (std::abs(analysis.eval_score) >= threshold &&
            wrong_direction(sign_of(analysis.label.exact_score_side_to_move),
                            sign_of(analysis.eval_score))) {
            cases.push_back(analysis);
        }
    }
    std::ranges::sort(cases, [](const RecordAnalysis& lhs, const RecordAnalysis& rhs) noexcept {
        return std::abs(lhs.eval_score) > std::abs(rhs.eval_score);
    });
    return cases;
}

[[nodiscard]] std::vector<MoveRankAnalysis>
move_rank_miss_cases(std::span<const RecordAnalysis> analyses) {
    std::vector<MoveRankAnalysis> cases;
    for (const RecordAnalysis& analysis : analyses) {
        if (analysis.move_rank.has_value() &&
            !analysis.move_rank->evaluator_top_group_has_exact_best) {
            cases.push_back(*analysis.move_rank);
        }
    }
    std::ranges::sort(cases, [](const MoveRankAnalysis& lhs,
                                const MoveRankAnalysis& rhs) noexcept {
        if (lhs.evaluator_score_gap_top_minus_exact_best !=
            rhs.evaluator_score_gap_top_minus_exact_best) {
            return lhs.evaluator_score_gap_top_minus_exact_best >
                   rhs.evaluator_score_gap_top_minus_exact_best;
        }
        if (lhs.exact_score_gap_exact_best_minus_top !=
            rhs.exact_score_gap_exact_best_minus_top) {
            return lhs.exact_score_gap_exact_best_minus_top >
                   rhs.exact_score_gap_exact_best_minus_top;
        }
        return lhs.empties < rhs.empties;
    });
    return cases;
}

[[nodiscard]] std::vector<MoveRankAnalysis>
move_rank_rank_cases(std::span<const RecordAnalysis> analyses) {
    std::vector<MoveRankAnalysis> cases;
    for (const RecordAnalysis& analysis : analyses) {
        if (analysis.move_rank.has_value()) {
            cases.push_back(*analysis.move_rank);
        }
    }
    std::ranges::sort(cases, [](const MoveRankAnalysis& lhs,
                                const MoveRankAnalysis& rhs) noexcept {
        if (lhs.exact_best_rank_under_evaluator != rhs.exact_best_rank_under_evaluator) {
            return lhs.exact_best_rank_under_evaluator > rhs.exact_best_rank_under_evaluator;
        }
        if (lhs.evaluator_score_gap_top_minus_exact_best !=
            rhs.evaluator_score_gap_top_minus_exact_best) {
            return lhs.evaluator_score_gap_top_minus_exact_best >
                   rhs.evaluator_score_gap_top_minus_exact_best;
        }
        if (lhs.exact_score_gap_exact_best_minus_top !=
            rhs.exact_score_gap_exact_best_minus_top) {
            return lhs.exact_score_gap_exact_best_minus_top >
                   rhs.exact_score_gap_exact_best_minus_top;
        }
        return lhs.empties < rhs.empties;
    });
    return cases;
}

void write_move_name_list(std::ostream& out, std::span<const std::string> moves) {
    if (moves.empty()) {
        out << "`-`";
        return;
    }
    for (std::size_t index = 0; index < moves.size(); ++index) {
        if (index != 0) {
            out << ' ';
        }
        out << '`' << moves[index] << '`';
    }
}

void write_ranked_move_list(std::ostream& out, std::span<const EvaluatedMove> moves) {
    if (moves.empty()) {
        out << "`-`";
        return;
    }
    for (std::size_t index = 0; index < moves.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        const EvaluatedMove& move = moves[index];
        out << '`' << (index + 1) << ':' << move.move << " eval=" << move.eval_score
            << " exact=" << move.exact_score_side_to_move;
        if (move.exact_best) {
            out << " exact-best";
        }
        out << '`';
    }
}

void write_move_rank_case_list(std::ostream& out, std::span<const MoveRankAnalysis> cases,
                               const AnalyzerOptions& options) {
    if (cases.empty()) {
        out << "None.\n\n";
        return;
    }

    const std::size_t limit = std::min(options.top, cases.size());
    for (std::size_t index = 0; index < limit; ++index) {
        const MoveRankAnalysis& analysis = cases[index];
        out << "### " << (index + 1) << ". " << analysis.position_id << '\n'
            << "- side_to_move: `" << analysis.side_to_move << "`\n"
            << "- empties: " << analysis.empties << '\n'
            << "- evaluator_selected_top_move: `" << analysis.evaluator_top_move << "`"
            << " (eval_score=" << analysis.evaluator_top_eval_score
            << ", exact_score_side_to_move=" << analysis.evaluator_top_exact_score << ")\n"
            << "- evaluator_top_score_group: ";
        write_move_name_list(out, analysis.evaluator_top_moves);
        out << " (eval_score=" << analysis.evaluator_top_eval_score
            << ", best_exact_score_side_to_move="
            << analysis.evaluator_top_group_best_exact_score << ")\n"
            << "- exact_best_in_evaluator_top_score_group: "
            << (analysis.evaluator_top_group_has_exact_best ? "true" : "false") << '\n'
            << "- exact_best_moves: ";
        write_move_name_list(out, analysis.exact_best_moves);
        out << " (exact_score_side_to_move=" << analysis.exact_best_score << ")\n"
            << "- exact_best_move_rank_under_evaluator: "
            << analysis.exact_best_rank_under_evaluator << '\n'
            << "- evaluator_score_gap_top_minus_exact_best: "
            << analysis.evaluator_score_gap_top_minus_exact_best << " heuristic units\n"
            << "- exact_score_gap_exact_best_minus_top_group: "
            << analysis.exact_score_gap_exact_best_minus_top << " discs\n"
            << "- ranked_moves: ";
        write_ranked_move_list(out, analysis.ranked_moves);
        out << "\n\n";
    }
}

void write_move_rank_section(std::ostream& out, const AnalyzerOptions& options,
                             const AnalyzerSummary& summary,
                             std::span<const RecordAnalysis> analyses) {
    if (!options.move_rank_analysis) {
        return;
    }

    out << "\n## Move-Rank Analysis\n\n"
        << "This optional diagnostic compares evaluator root-child ordering with exact "
           "per-move scores. It is not Elo, not a tuner, and not an automatic promotion "
           "gate. Evaluator scores can tie; top-hit counts use the full top eval score "
           "group, while the selected top move is only the deterministic first move in "
           "that sorted group.\n\n"
        << "- records_with_move_scores: " << summary.move_rank_records_with_scores << '\n'
        << "- records_missing_move_scores: " << summary.move_rank_records_missing_scores
        << '\n'
        << "- records_no_legal_root_moves: " << summary.move_rank_records_no_legal_moves
        << '\n'
        << "- move_rank_analyzed: " << summary.move_rank_analyzed << '\n';

    if (summary.move_rank_analyzed == 0) {
        out << "\nMove-rank analysis was requested, but no records with usable "
               "`move_scores` were available. Generate labels with "
               "`othello_exact_label_dump --include-move-scores` to enable this "
               "diagnostic.\n\n";
        return;
    }

    out << "- evaluator_top_score_group_exact_best_rate: "
        << percentage(summary.move_rank_top_exact_best, summary.move_rank_analyzed) << '\n'
        << "- evaluator_top_score_group_non_best_count: " << summary.move_rank_top_non_best
        << '\n'
        << "- mean_exact_best_move_rank_under_evaluator: "
        << mean_text(static_cast<long long>(summary.move_rank_exact_best_rank_sum),
                     summary.move_rank_analyzed)
        << '\n'
        << "- mean_evaluator_score_gap_top_minus_exact_best: "
        << mean_text(summary.move_rank_eval_score_gap_sum, summary.move_rank_analyzed)
        << " heuristic units\n"
        << "- mean_exact_score_gap_exact_best_minus_top_group: "
        << mean_text(summary.move_rank_exact_score_gap_sum, summary.move_rank_analyzed)
        << " discs\n";

    if (summary.move_rank_records_missing_scores != 0) {
        out << "\nCaveat: " << summary.move_rank_records_missing_scores
            << " records did not include `move_scores` and were skipped for move-rank "
               "analysis.\n";
    }

    const std::vector<MoveRankAnalysis> rank_cases = move_rank_rank_cases(analyses);
    out << "\n### Exact-Best Rank Cases\n\n"
        << "Sorted by exact-best rank under the evaluator, then evaluator and exact score "
           "gaps. These rows show the deterministic selected top move, the evaluator top "
           "score group, and whether that group contains an exact-best move.\n\n";
    write_move_rank_case_list(out, rank_cases, options);

    const std::vector<MoveRankAnalysis> misses = move_rank_miss_cases(analyses);
    out << "\n### Worst Evaluator Top-Move Misses\n\n"
        << "Sorted by the evaluator score gap between the evaluator top score group and the "
           "highest-ranked exact-best move. A case is a miss only when no exact-best move "
           "appears in the evaluator top score group.\n\n";
    write_move_rank_case_list(out, misses, options);
}

[[nodiscard]] std::string make_markdown_report(const AnalyzerOptions& options,
                                               const AnalyzerSummary& summary,
                                               std::span<const RecordAnalysis> analyses) {
    std::map<std::string, BucketStats> empties_buckets;
    std::map<std::string, BucketStats> phase_buckets;
    long long exact_win_eval_sum = 0;
    long long exact_loss_eval_sum = 0;
    std::size_t exact_win_eval_count = 0;
    std::size_t exact_loss_eval_count = 0;

    for (const RecordAnalysis& analysis : analyses) {
        update_bucket(empties_buckets["empties=" + std::to_string(analysis.label.empties)],
                      analysis);
        update_bucket(phase_buckets[std::string{phase_name(analysis.breakdown.phase)}], analysis);
        if (analysis.label.exact_score_side_to_move > 0) {
            exact_win_eval_sum += analysis.eval_score;
            ++exact_win_eval_count;
        } else if (analysis.label.exact_score_side_to_move < 0) {
            exact_loss_eval_sum += analysis.eval_score;
            ++exact_loss_eval_count;
        }
    }

    const std::vector<RecordAnalysis> wrong_cases = wrong_direction_cases(analyses);
    const std::vector<RecordAnalysis> high_confidence =
        high_confidence_cases(analyses, options.high_confidence_threshold);

    std::ostringstream out;
    out << "# Eval vs Exact Report\n\n"
        << "No strength claim. Exact labels are final disc margins; evaluator scores are "
           "heuristic units.\n\n"
        << "## Metadata\n\n"
        << "- labels_path: `" << options.labels_path.string() << "`\n"
        << "- evaluator: " << eval_selection_description(options.evaluator) << '\n'
        << "- timestamp: `" << (options.timestamp.empty() ? "unknown" : options.timestamp) << "`\n"
        << "- source_sha: `" << options.source_sha << "`\n"
        << "- command: `" << (options.command.empty() ? "unknown" : options.command) << "`\n"
        << "- top: " << options.top << '\n'
        << "- high_confidence_threshold: " << options.high_confidence_threshold
        << " heuristic units\n"
        << "- include_positions: " << (options.include_positions ? "true" : "false") << '\n'
        << "- move_rank_analysis: " << (options.move_rank_analysis ? "true" : "false")
        << "\n\n"
        << "## Summary\n\n"
        << "- records_read: " << summary.records_read << '\n'
        << "- analyzed: " << summary.analyzed << '\n'
        << "- skipped: " << summary.skipped
        << " (v1 fail-fast: malformed or invalid records abort instead of being skipped)\n"
        << "- exact_wins/losses/draws: " << summary.exact_wins << " / " << summary.exact_losses
        << " / " << summary.exact_draws << '\n'
        << "- eval_positive/negative/zero: " << summary.eval_positive << " / "
        << summary.eval_negative << " / " << summary.eval_zero << '\n'
        << "- sign_agreement_rate: " << percentage(summary.sign_agreements, summary.analyzed)
        << '\n'
        << "- sign_disagreement_count: " << summary.sign_disagreements << '\n'
        << "- wrong_direction_count: " << summary.wrong_direction << '\n'
        << "- high_confidence_wrong_direction_count: "
        << summary.high_confidence_wrong_direction << '\n'
        << "- exact_draw_handling_count: " << summary.exact_draw_handling << '\n'
        << "- mean_eval_score_for_exact_winning_positions: "
        << mean_text(exact_win_eval_sum, exact_win_eval_count) << '\n'
        << "- mean_eval_score_for_exact_losing_positions: "
        << mean_text(exact_loss_eval_sum, exact_loss_eval_count) << "\n\n"
        << "## Bucket Summary\n\n"
        << "### By Empties\n\n";
    write_bucket_table(out, empties_buckets);

    if (options.phase_breakdown) {
        out << "\n### By Evaluator Phase\n\n";
        write_bucket_table(out, phase_buckets);
    }

    write_move_rank_section(out, options, summary, analyses);

    out << "\n## Worst Wrong-Direction Positions\n\n";
    write_case_list(out, wrong_cases, options);

    out << "## High-Confidence Disagreements\n\n"
        << "Threshold: `abs(eval_score) >= " << options.high_confidence_threshold
        << "` heuristic units and opposite exact/eval signs.\n\n";
    write_case_list(out, high_confidence, options);

    out << "## Caveats\n\n"
        << "- Exact labels are final disc margins; evaluator scores are uncalibrated heuristic "
           "units.\n"
        << "- Sign agreement is a diagnostic, not an Elo, strength, or promotion claim.\n"
        << "- Move-rank analysis is diagnostic move-quality evidence, not an Elo, tuner, or "
           "promotion claim.\n"
        << "- Move-rank top-hit metrics use the evaluator top score group because evaluator "
           "scores can tie; the selected top move is deterministic report detail.\n"
        << "- Raw score differences are heuristic-vs-disc comparisons and should not be read as "
           "disc MAE.\n"
        << "- Move-rank analysis requires labels generated with `--include-move-scores` and is "
           "opt-in via `--move-rank-analysis`.\n"
        << "- Keep raw runs under `runs/`; keep durable summaries under "
           "`docs/perf/baselines/`.\n";

    return out.str();
}

} // namespace

std::string_view phase_name(EvaluationPhase phase) noexcept {
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

std::optional<AnalyzerReport> analyze_exact_label_jsonl(std::string_view text,
                                                        const AnalyzerOptions& options,
                                                        std::string& error) {
    error.clear();

    const EvaluationConfig config = resolve_evaluator_selection(options.evaluator);
    AnalyzerSummary summary;
    std::vector<RecordAnalysis> analyses;

    std::size_t line_begin = 0;
    int line_number = 0;
    while (line_begin <= text.size()) {
        ++line_number;
        const std::size_t newline = text.find('\n', line_begin);
        const std::size_t line_end = newline == std::string_view::npos ? text.size() : newline;
        std::string_view line = text.substr(line_begin, line_end - line_begin);
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }

        if (!is_empty_line(line)) {
            ++summary.records_read;
            std::optional<ExactLabelRecord> label = parse_line(line, line_number, error);
            if (!label.has_value()) {
                return std::nullopt;
            }

            RecordAnalysis analysis;
            if (!analyze_record(*label, config, options.move_rank_analysis, analysis, error)) {
                error = "line " + std::to_string(line_number) + ": " + error;
                return std::nullopt;
            }
            update_summary(summary, analysis);
            if (options.move_rank_analysis) {
                update_move_rank_summary(summary, analysis);
            }
            analyses.push_back(std::move(analysis));
        }

        if (newline == std::string_view::npos) {
            break;
        }
        line_begin = newline + 1;
    }

    summary.high_confidence_wrong_direction =
        high_confidence_cases(analyses, options.high_confidence_threshold).size();

    return AnalyzerReport{
        .summary = summary,
        .markdown = make_markdown_report(options, summary, analyses),
    };
}

} // namespace othello::tools::eval_vs_exact
