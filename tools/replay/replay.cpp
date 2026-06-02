#include "replay/replay.hpp"

#include "common/jsonl.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <ostream>
#include <sstream>

namespace othello::replay {
namespace {

[[nodiscard]] std::string side_name(Side side) {
    return side == Side::Black ? "black" : "white";
}

[[nodiscard]] std::string side_token(Side side) {
    return side == Side::Black ? "B" : "W";
}

[[nodiscard]] std::string player_for_side(const GameRecord& record, Side side) {
    const bool side_is_player_a =
        side == Side::Black ? record.black_is_player_a : !record.black_is_player_a;
    return side_is_player_a ? "head" : "base";
}

[[nodiscard]] int first_different_move_index(std::span<const std::string> first,
                                             std::span<const std::string> second) noexcept {
    const auto limit = std::min(first.size(), second.size());
    for (std::size_t index = 0; index < limit; ++index) {
        if (first[index] != second[index]) {
            return static_cast<int>(index);
        }
    }
    if (first.size() != second.size()) {
        return static_cast<int>(limit);
    }
    return -1;
}

[[nodiscard]] bool same_opening(const GameRecord& first, const GameRecord& second) noexcept {
    return first.opening_index == second.opening_index &&
           first.opening_name == second.opening_name && first.start_board == second.start_board &&
           first.opening_moves == second.opening_moves;
}

class JsonLineParser {
public:
    explicit JsonLineParser(std::string_view text) noexcept : text_(text) {}

    [[nodiscard]] ParseRecordResult parse_record() {
        GameRecord record;
        bool saw_game_index = false;
        bool saw_opening_index = false;
        bool saw_opening_name = false;
        bool saw_opening_moves = false;
        bool saw_start_board = false;
        bool saw_black_is_player_a = false;
        bool saw_score_diff_from_player_a = false;
        bool saw_moves = false;

        skip_space();
        if (remaining().empty()) {
            return ParseRecordResult{.ok = true, .empty = true};
        }
        if (!consume('{')) {
            return fail("JSONL record must be an object");
        }
        skip_space();
        if (consume('}')) {
            return fail("JSONL record is missing match fields");
        }

        while (true) {
            auto key = parse_string();
            if (!key.has_value()) {
                return fail("object key must be a string");
            }
            skip_space();
            if (!consume(':')) {
                return fail("object field is missing ':'");
            }

            if (*key == "game_index") {
                auto value = parse_int();
                if (!value.has_value()) {
                    return fail("game_index must be an integer");
                }
                record.game_index = *value;
                saw_game_index = true;
            } else if (*key == "opening_index") {
                auto value = parse_int();
                if (!value.has_value()) {
                    return fail("opening_index must be an integer");
                }
                record.opening_index = *value;
                saw_opening_index = true;
            } else if (*key == "opening_name") {
                auto value = parse_string();
                if (!value.has_value()) {
                    return fail("opening_name must be a string");
                }
                record.opening_name = *value;
                saw_opening_name = true;
            } else if (*key == "opening_moves") {
                auto value = parse_string_array();
                if (!value.has_value()) {
                    return fail("opening_moves must be an array of strings");
                }
                record.opening_moves = *value;
                saw_opening_moves = true;
            } else if (*key == "start_board") {
                auto value = parse_string();
                if (!value.has_value()) {
                    return fail("start_board must be a string");
                }
                record.start_board = *value;
                saw_start_board = true;
            } else if (*key == "black_is_player_a") {
                auto value = parse_bool();
                if (!value.has_value()) {
                    return fail("black_is_player_a must be a boolean");
                }
                record.black_is_player_a = *value;
                saw_black_is_player_a = true;
            } else if (*key == "score_diff_from_player_a") {
                auto value = parse_int();
                if (!value.has_value()) {
                    return fail("score_diff_from_player_a must be an integer");
                }
                record.score_diff_from_player_a = *value;
                saw_score_diff_from_player_a = true;
            } else if (*key == "moves") {
                auto value = parse_string_array();
                if (!value.has_value()) {
                    return fail("moves must be an array of strings");
                }
                record.moves = *value;
                saw_moves = true;
            } else if (*key == "illegal_or_error") {
                auto value = parse_bool();
                if (!value.has_value()) {
                    return fail("illegal_or_error must be a boolean");
                }
                record.illegal_or_error = *value;
            } else if (!skip_value()) {
                return fail("failed to parse JSON value");
            }

            skip_space();
            if (consume('}')) {
                break;
            }
            if (!consume(',')) {
                return fail("object field is missing ','");
            }
            skip_space();
        }

        skip_space();
        if (!remaining().empty()) {
            return fail("unexpected text after JSON object");
        }

        if (!saw_game_index || !saw_opening_index || !saw_opening_name || !saw_opening_moves ||
            !saw_start_board || !saw_black_is_player_a || !saw_score_diff_from_player_a ||
            !saw_moves) {
            return fail("JSONL record is missing required match-runner fields");
        }

        return ParseRecordResult{.ok = true, .record = std::move(record)};
    }

private:
    [[nodiscard]] std::string_view remaining() const noexcept {
        return text_.substr(position_);
    }

    void skip_space() noexcept {
        while (position_ < text_.size()) {
            const char character = text_[position_];
            if (character != ' ' && character != '\t' && character != '\n' && character != '\r') {
                break;
            }
            ++position_;
        }
    }

    [[nodiscard]] bool consume(char expected) noexcept {
        skip_space();
        if (position_ >= text_.size() || text_[position_] != expected) {
            return false;
        }
        ++position_;
        return true;
    }

    [[nodiscard]] std::optional<std::string> parse_string() {
        skip_space();
        if (position_ >= text_.size() || text_[position_] != '"') {
            return std::nullopt;
        }
        ++position_;

        std::string value;
        while (position_ < text_.size()) {
            const char character = text_[position_++];
            if (character == '"') {
                return value;
            }
            if (character != '\\') {
                value += character;
                continue;
            }
            if (position_ >= text_.size()) {
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
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::vector<std::string>> parse_string_array() {
        if (!consume('[')) {
            return std::nullopt;
        }
        std::vector<std::string> values;
        skip_space();
        if (consume(']')) {
            return values;
        }
        while (true) {
            auto value = parse_string();
            if (!value.has_value()) {
                return std::nullopt;
            }
            values.push_back(*value);
            skip_space();
            if (consume(']')) {
                return values;
            }
            if (!consume(',')) {
                return std::nullopt;
            }
        }
    }

    [[nodiscard]] std::optional<int> parse_int() {
        skip_space();
        const std::size_t begin = position_;
        if (position_ < text_.size() && text_[position_] == '-') {
            ++position_;
        }
        while (position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9') {
            ++position_;
        }
        if (position_ == begin || (position_ == begin + 1 && text_[begin] == '-')) {
            return std::nullopt;
        }

        int value = 0;
        const auto* first = text_.data() + begin;
        const auto* last = text_.data() + position_;
        const auto result = std::from_chars(first, last, value);
        if (result.ec != std::errc{} || result.ptr != last) {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] std::optional<bool> parse_bool() {
        skip_space();
        if (remaining().starts_with("true")) {
            position_ += 4;
            return true;
        }
        if (remaining().starts_with("false")) {
            position_ += 5;
            return false;
        }
        return std::nullopt;
    }

    [[nodiscard]] bool skip_value() {
        skip_space();
        if (position_ >= text_.size()) {
            return false;
        }
        const char character = text_[position_];
        if (character == '"') {
            return parse_string().has_value();
        }
        if (character == '[') {
            ++position_;
            skip_space();
            if (consume(']')) {
                return true;
            }
            while (true) {
                if (!skip_value()) {
                    return false;
                }
                skip_space();
                if (consume(']')) {
                    return true;
                }
                if (!consume(',')) {
                    return false;
                }
            }
        }
        if (character == '{') {
            ++position_;
            skip_space();
            if (consume('}')) {
                return true;
            }
            while (true) {
                if (!parse_string().has_value()) {
                    return false;
                }
                if (!consume(':') || !skip_value()) {
                    return false;
                }
                skip_space();
                if (consume('}')) {
                    return true;
                }
                if (!consume(',')) {
                    return false;
                }
            }
        }

        while (position_ < text_.size()) {
            const char delimiter = text_[position_];
            if (delimiter == ',' || delimiter == ']' || delimiter == '}' || delimiter == ' ' ||
                delimiter == '\t' || delimiter == '\n' || delimiter == '\r') {
                break;
            }
            ++position_;
        }
        return true;
    }

    [[nodiscard]] ParseRecordResult fail(std::string error) const {
        return ParseRecordResult{.error = std::move(error)};
    }

    std::string_view text_;
    std::size_t position_ = 0;
};

[[nodiscard]] ExtractDivergencesResult
extract_pair_divergence(int pair_index, const GameRecord& first, const GameRecord& second) {
    if (!same_opening(first, second)) {
        return ExtractDivergencesResult{
            .error = "paired records do not share opening metadata and start board"};
    }

    const int divergence_index = first_different_move_index(first.moves, second.moves);
    if (divergence_index < 0) {
        return ExtractDivergencesResult{.ok = true};
    }

    const auto common_moves =
        std::span<const std::string>{first.moves}.first(static_cast<std::size_t>(divergence_index));
    ReplayResult replay = replay_moves(first.start_board, common_moves);
    if (!replay.ok) {
        return ExtractDivergencesResult{.error = replay.error};
    }

    const Side side = replay.board.side_to_move;
    const std::string first_player = player_for_side(first, side);
    const std::string second_player = player_for_side(second, side);
    if (!((first_player == "head" && second_player == "base") ||
          (first_player == "base" && second_player == "head"))) {
        return ExtractDivergencesResult{
            .error = "paired records do not compare head and base at divergence"};
    }

    const std::string first_move = static_cast<std::size_t>(divergence_index) < first.moves.size()
                                       ? first.moves[static_cast<std::size_t>(divergence_index)]
                                       : "none";
    const std::string second_move = static_cast<std::size_t>(divergence_index) < second.moves.size()
                                        ? second.moves[static_cast<std::size_t>(divergence_index)]
                                        : "none";
    const GameRecord& head_record = first_player == "head" ? first : second;
    const GameRecord& base_record = first_player == "base" ? first : second;

    Divergence divergence{
        .pair_index = pair_index,
        .opening_index = first.opening_index,
        .opening_name = first.opening_name,
        .head_game_index = head_record.game_index,
        .base_game_index = base_record.game_index,
        .ply = static_cast<int>(first.opening_moves.size()) + replay.turns,
        .side_to_move = side_name(side),
        .board_text = to_string(replay.board),
        .head_move = first_player == "head" ? first_move : second_move,
        .base_move = first_player == "base" ? first_move : second_move,
        .head_final_diff = head_record.score_diff_from_player_a,
        .base_game_head_final_diff = base_record.score_diff_from_player_a,
        .illegal_or_error = first.illegal_or_error || second.illegal_or_error,
        .preceding_moves = first.opening_moves,
    };
    divergence.preceding_moves.insert(divergence.preceding_moves.end(), common_moves.begin(),
                                      common_moves.end());

    return ExtractDivergencesResult{.ok = true, .divergences = {std::move(divergence)}};
}

void write_string_array(std::ostream& output, std::span<const std::string> values) {
    output << '[';
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index != 0) {
            output << ',';
        }
        tools::write_json_string(output, values[index]);
    }
    output << ']';
}

void write_divergence_json(std::ostream& output, const Divergence& divergence) {
    output << '{';
    output << "\"base_game_head_final_diff\":" << divergence.base_game_head_final_diff << ',';
    output << "\"base_game_index\":" << divergence.base_game_index << ',';
    output << "\"base_move\":";
    tools::write_json_string(output, divergence.base_move);
    output << ',';
    output << "\"board_text\":";
    tools::write_json_string(output, divergence.board_text);
    output << ',';
    output << "\"head_final_diff\":" << divergence.head_final_diff << ',';
    output << "\"head_game_index\":" << divergence.head_game_index << ',';
    output << "\"head_move\":";
    tools::write_json_string(output, divergence.head_move);
    output << ',';
    output << "\"illegal_or_error\":" << (divergence.illegal_or_error ? "true" : "false") << ',';
    output << "\"opening_index\":" << divergence.opening_index << ',';
    output << "\"opening_name\":";
    tools::write_json_string(output, divergence.opening_name);
    output << ',';
    output << "\"pair_index\":" << divergence.pair_index << ',';
    output << "\"ply\":" << divergence.ply << ',';
    output << "\"preceding_moves\":";
    write_string_array(output, divergence.preceding_moves);
    output << ',';
    output << "\"side_to_move\":";
    tools::write_json_string(output, divergence.side_to_move);
    output << '}';
}

} // namespace

ReplayResult replay_moves(const Board& start_board, std::span<const std::string> moves) {
    Board board = start_board;
    int turns = 0;
    int passes = 0;

    for (const std::string& move_text : moves) {
        if (move_text == "pass") {
            const auto next = pass_turn(board);
            if (!next.has_value()) {
                return ReplayResult{.board = board,
                                    .turns = turns,
                                    .passes = passes,
                                    .error = "illegal explicit pass"};
            }
            board = *next;
            ++turns;
            ++passes;
            continue;
        }

        while (legal_moves(board) == 0) {
            const auto next = pass_turn(board);
            if (!next.has_value()) {
                return ReplayResult{.board = board,
                                    .turns = turns,
                                    .passes = passes,
                                    .error = "move list continues after terminal board"};
            }
            board = *next;
            ++turns;
            ++passes;
        }

        const auto square = square_from_string(move_text);
        if (!square.has_value()) {
            return ReplayResult{.board = board,
                                .turns = turns,
                                .passes = passes,
                                .error = "invalid move token: " + move_text};
        }

        const auto next = apply_move(board, *square);
        if (!next.has_value()) {
            return ReplayResult{.board = board,
                                .turns = turns,
                                .passes = passes,
                                .error = "illegal move " + move_text + " for side " +
                                         side_token(board.side_to_move)};
        }
        board = *next;
        ++turns;
    }

    return ReplayResult{.ok = true, .board = board, .turns = turns, .passes = passes};
}

ReplayResult replay_moves(std::string_view start_board_text, std::span<const std::string> moves) {
    const auto board = board_from_string(start_board_text);
    if (!board.has_value()) {
        return ReplayResult{.error = "invalid start_board text"};
    }
    return replay_moves(*board, moves);
}

ParseRecordResult parse_match_jsonl_record(std::string_view line) {
    return JsonLineParser{line}.parse_record();
}

ReadRecordsResult read_match_jsonl_records(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) {
        return ReadRecordsResult{.error = "failed to open input file: " + path.string()};
    }

    std::vector<GameRecord> records;
    std::string line;
    int line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        ParseRecordResult parsed = parse_match_jsonl_record(line);
        if (!parsed.ok) {
            return ReadRecordsResult{.error = "line " + std::to_string(line_number) + ": " +
                                              parsed.error};
        }
        if (!parsed.empty) {
            records.push_back(std::move(parsed.record));
        }
    }

    if (records.empty()) {
        return ReadRecordsResult{.error = "input file contains no records: " + path.string()};
    }
    return ReadRecordsResult{.ok = true, .records = std::move(records)};
}

ExtractDivergencesResult extract_divergences(std::span<const GameRecord> records) {
    if (records.size() % 2 != 0) {
        return ExtractDivergencesResult{
            .error = "expected an even number of records from a swap-side matrix"};
    }

    std::vector<Divergence> divergences;
    for (std::size_t index = 0; index < records.size(); index += 2) {
        ExtractDivergencesResult pair = extract_pair_divergence(static_cast<int>(index / 2),
                                                                records[index], records[index + 1]);
        if (!pair.ok) {
            return pair;
        }
        divergences.insert(divergences.end(), pair.divergences.begin(), pair.divergences.end());
    }

    return ExtractDivergencesResult{.ok = true, .divergences = std::move(divergences)};
}

std::string render_divergences(std::span<const Divergence> divergences, OutputFormat format) {
    std::ostringstream output;
    if (format == OutputFormat::Jsonl) {
        for (const Divergence& divergence : divergences) {
            write_divergence_json(output, divergence);
            output << '\n';
        }
        return output.str();
    }

    output << "| pair | opening | head game | base game | ply | side | head move | base move | "
              "head diff | paired head diff | error | preceding moves |\n";
    output << "| ---: | :--- | ---: | ---: | ---: | :--- | :--- | :--- | ---: | ---: | :--- | "
              ":--- |\n";
    for (const Divergence& divergence : divergences) {
        output << "| " << divergence.pair_index << " | " << divergence.opening_name << " | "
               << divergence.head_game_index << " | " << divergence.base_game_index << " | "
               << divergence.ply << " | " << divergence.side_to_move << " | "
               << divergence.head_move << " | " << divergence.base_move << " | "
               << divergence.head_final_diff << " | " << divergence.base_game_head_final_diff
               << " | " << (divergence.illegal_or_error ? "yes" : "no") << " | ";
        if (divergence.preceding_moves.empty()) {
            output << "(none)";
        } else {
            for (std::size_t index = 0; index < divergence.preceding_moves.size(); ++index) {
                if (index != 0) {
                    output << ' ';
                }
                output << divergence.preceding_moves[index];
            }
        }
        output << " |\n";
    }

    if (!divergences.empty()) {
        output << "\n## Boards\n\n";
        for (const Divergence& divergence : divergences) {
            output << "### Pair " << divergence.pair_index << ", ply " << divergence.ply << "\n\n";
            output << "```text\n" << divergence.board_text << "\n```\n\n";
        }
    }
    return output.str();
}

} // namespace othello::replay
