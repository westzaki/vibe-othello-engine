#include "eval_vs_exact/eval_vs_exact.hpp"

#include "exact_labels/exact_label_dump.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <iomanip>
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

struct RecordAnalysis {
    ExactLabelRecord label;
    Board board;
    int eval_score = 0;
    EvaluationBreakdown breakdown;
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
                                  RecordAnalysis& analysis, std::string& error) {
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
        << "- include_positions: " << (options.include_positions ? "true" : "false") << "\n\n"
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
        << "- Raw score differences are heuristic-vs-disc comparisons and should not be read as "
           "disc MAE.\n"
        << "- Best-move agreement is intentionally out of scope for this v1 analyzer.\n"
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
            if (!analyze_record(*label, config, analysis, error)) {
                error = "line " + std::to_string(line_number) + ": " + error;
                return std::nullopt;
            }
            update_summary(summary, analysis);
            analyses.push_back(std::move(analysis));
        }

        if (newline == std::string_view::npos) {
            break;
        }
        line_begin = newline + 1;
    }

    return AnalyzerReport{
        .summary = summary,
        .markdown = make_markdown_report(options, summary, analyses),
    };
}

} // namespace othello::tools::eval_vs_exact
