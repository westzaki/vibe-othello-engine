#pragma once

#include <algorithm>
#include <bit>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <othello/othello.hpp>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace othello::match_runner {

enum class PlayerKind {
    First,
    Random,
    Eval,
    Search,
};

struct SearchPlayerOptions {
    int max_depth = SearchOptions{}.max_depth;
    bool use_transposition_table = SearchOptions{}.use_transposition_table;
    std::size_t transposition_table_entries = SearchOptions{}.transposition_table_entries;
    int exact_endgame_empty_threshold = SearchOptions{}.exact_endgame_empty_threshold;
    bool use_pvs = SearchOptions{}.use_pvs;

    [[nodiscard]] friend bool operator==(const SearchPlayerOptions&,
                                         const SearchPlayerOptions&) = default;
};

struct PlayerSpec {
    PlayerKind kind = PlayerKind::First;
    int depth = 0;
    SearchPlayerOptions search_options;
    std::string text = "first";

    [[nodiscard]] friend bool operator==(const PlayerSpec&, const PlayerSpec&) = default;
};

struct MoveSelection {
    std::optional<Square> move;
    std::uint64_t nodes = 0;
    double elapsed_ms = 0.0;
    std::optional<SearchStats> search_stats;
};

struct Opening {
    std::string name = "initial";
    std::vector<std::string> moves;
    Board start_board = Board::initial();
};

struct MatchConfig {
    PlayerSpec player_a;
    PlayerSpec player_b;
    int games = 1;
    bool swap_sides = false;
    std::uint64_t seed = 1;
    std::vector<Opening> openings;
};

struct GameRecord {
    int game_index = 0;
    std::uint64_t seed = 0;
    int opening_index = 0;
    std::string opening_name = "initial";
    std::vector<std::string> opening_moves;
    std::string start_board;
    std::string black_spec;
    std::string white_spec;
    std::string player_a_spec;
    std::string player_b_spec;
    bool black_is_player_a = true;
    std::string winner = "draw";
    int black_score = 0;
    int white_score = 0;
    int score_diff_from_black = 0;
    int score_diff_from_player_a = 0;
    std::uint64_t nodes_black = 0;
    std::uint64_t nodes_white = 0;
    std::uint64_t nodes_player_a = 0;
    std::uint64_t nodes_player_b = 0;
    double time_ms_black = 0.0;
    double time_ms_white = 0.0;
    double time_ms_player_a = 0.0;
    double time_ms_player_b = 0.0;
    int plies = 0;
    int passes = 0;
    std::vector<std::string> moves;
    bool illegal_or_error = false;

    [[nodiscard]] friend bool operator==(const GameRecord&, const GameRecord&) = default;
};

struct OpeningParseResult {
    bool ok = false;
    bool has_opening = false;
    Opening opening;
    std::string error;
};

struct MatchSummary {
    int games = 0;
    int player_a_wins = 0;
    int player_b_wins = 0;
    int draws = 0;
    double average_disc_diff_from_player_a = 0.0;
};

[[nodiscard]] inline std::optional<int> parse_non_negative_int(std::string_view text) noexcept {
    int value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value < 0) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] inline std::optional<std::uint64_t> parse_u64(std::string_view text) noexcept {
    std::uint64_t value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] inline std::optional<bool> parse_on_off(std::string_view text) noexcept {
    if (text == "on") {
        return true;
    }
    if (text == "off") {
        return false;
    }
    return std::nullopt;
}

[[nodiscard]] inline bool is_ascii_space(char character) noexcept {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' ||
           character == '\f' || character == '\v';
}

[[nodiscard]] inline std::string_view trim_ascii_space(std::string_view text) noexcept {
    while (!text.empty() && is_ascii_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_ascii_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] inline std::optional<PlayerSpec> parse_player_spec(std::string_view text) {
    if (text == "first") {
        return PlayerSpec{.kind = PlayerKind::First, .depth = 0, .text = std::string{text}};
    }
    if (text == "random") {
        return PlayerSpec{.kind = PlayerKind::Random, .depth = 0, .text = std::string{text}};
    }
    if (text == "eval") {
        return PlayerSpec{.kind = PlayerKind::Eval, .depth = 0, .text = std::string{text}};
    }

    constexpr std::string_view search_prefix = "search:depth=";
    if (text.starts_with(search_prefix)) {
        std::string_view rest = text.substr(search_prefix.size());
        const std::size_t depth_end = rest.find(',');
        const std::string_view depth_text = rest.substr(0, depth_end);
        const std::optional<int> depth = parse_non_negative_int(depth_text);
        if (!depth.has_value() || *depth <= 0) {
            return std::nullopt;
        }

        SearchPlayerOptions search_options;
        search_options.max_depth = *depth;

        bool seen_tt = false;
        bool seen_pvs = false;
        bool seen_exact = false;
        bool seen_tt_entries = false;

        if (depth_end != std::string_view::npos) {
            rest.remove_prefix(depth_end + 1);
            while (true) {
                if (rest.empty()) {
                    return std::nullopt;
                }

                const std::size_t option_end = rest.find(',');
                const std::string_view option = rest.substr(0, option_end);
                if (option.empty()) {
                    return std::nullopt;
                }

                const std::size_t equals = option.find('=');
                if (equals == std::string_view::npos) {
                    return std::nullopt;
                }

                const std::string_view key = option.substr(0, equals);
                const std::string_view value = option.substr(equals + 1);

                if (key == "tt") {
                    if (seen_tt) {
                        return std::nullopt;
                    }
                    const std::optional<bool> parsed = parse_on_off(value);
                    if (!parsed.has_value()) {
                        return std::nullopt;
                    }
                    search_options.use_transposition_table = *parsed;
                    seen_tt = true;
                } else if (key == "pvs") {
                    if (seen_pvs) {
                        return std::nullopt;
                    }
                    const std::optional<bool> parsed = parse_on_off(value);
                    if (!parsed.has_value()) {
                        return std::nullopt;
                    }
                    search_options.use_pvs = *parsed;
                    seen_pvs = true;
                } else if (key == "exact") {
                    if (seen_exact) {
                        return std::nullopt;
                    }
                    if (value == "off") {
                        search_options.exact_endgame_empty_threshold = 0;
                    } else {
                        const std::optional<int> parsed = parse_non_negative_int(value);
                        if (!parsed.has_value()) {
                            return std::nullopt;
                        }
                        search_options.exact_endgame_empty_threshold = *parsed;
                    }
                    seen_exact = true;
                } else if (key == "tt_entries") {
                    if (seen_tt_entries) {
                        return std::nullopt;
                    }
                    const std::optional<std::uint64_t> parsed = parse_u64(value);
                    if (!parsed.has_value() ||
                        *parsed > static_cast<std::uint64_t>(
                                      std::numeric_limits<std::size_t>::max())) {
                        return std::nullopt;
                    }
                    search_options.transposition_table_entries =
                        static_cast<std::size_t>(*parsed);
                    seen_tt_entries = true;
                } else {
                    return std::nullopt;
                }

                if (option_end == std::string_view::npos) {
                    break;
                }
                rest.remove_prefix(option_end + 1);
            }
        }

        return PlayerSpec{.kind = PlayerKind::Search,
                          .depth = *depth,
                          .search_options = search_options,
                          .text = std::string{text}};
    }

    return std::nullopt;
}

[[nodiscard]] inline SearchOptions make_search_options(const PlayerSpec& spec) noexcept {
    SearchOptions options;
    options.max_depth = spec.search_options.max_depth;
    options.use_transposition_table = spec.search_options.use_transposition_table;
    options.transposition_table_entries = spec.search_options.transposition_table_entries;
    options.exact_endgame_empty_threshold = spec.search_options.exact_endgame_empty_threshold;
    options.use_pvs = spec.search_options.use_pvs;
    return options;
}

[[nodiscard]] inline Opening default_opening() {
    return Opening{.name = "initial", .moves = {}, .start_board = Board::initial()};
}

[[nodiscard]] inline std::vector<std::string> split_ascii_whitespace(std::string_view text) {
    std::vector<std::string> tokens;

    while (true) {
        text = trim_ascii_space(text);
        if (text.empty()) {
            return tokens;
        }

        std::size_t token_size = 0;
        while (token_size < text.size() && !is_ascii_space(text[token_size])) {
            ++token_size;
        }

        tokens.emplace_back(text.substr(0, token_size));
        text.remove_prefix(token_size);
    }
}

[[nodiscard]] inline OpeningParseResult parse_opening_line(std::string_view line) {
    line = trim_ascii_space(line);
    if (line.empty() || line.front() == '#') {
        return OpeningParseResult{.ok = true, .has_opening = false};
    }

    const std::size_t separator = line.find(':');
    if (separator == std::string_view::npos) {
        return OpeningParseResult{.error = "missing ':' separator"};
    }

    const std::string_view name = trim_ascii_space(line.substr(0, separator));
    if (name.empty()) {
        return OpeningParseResult{.error = "missing opening name"};
    }

    Opening opening{
        .name = std::string{name},
        .moves = split_ascii_whitespace(line.substr(separator + 1)),
        .start_board = Board::initial(),
    };

    Board board = Board::initial();
    for (const std::string& move_text : opening.moves) {
        if (is_game_over(board)) {
            return OpeningParseResult{.error = "opening has moves after game over"};
        }

        if (!has_legal_move(board)) {
            const std::optional<Board> after_pass = pass_turn(board);
            if (!after_pass.has_value()) {
                return OpeningParseResult{.error = "opening cannot pass from terminal board"};
            }
            board = *after_pass;
        }

        const std::optional<Square> move = square_from_string(move_text);
        if (!move.has_value()) {
            return OpeningParseResult{.error = "invalid move coordinate: " + move_text};
        }

        const std::optional<Board> next = apply_move(board, *move);
        if (!next.has_value()) {
            return OpeningParseResult{.error = "illegal opening move: " + move_text};
        }
        board = *next;
    }

    opening.start_board = board;
    return OpeningParseResult{.ok = true, .has_opening = true, .opening = std::move(opening)};
}

[[nodiscard]] inline std::vector<Square> squares_from_bitboard(Bitboard bits) {
    std::vector<Square> squares;
    squares.reserve(static_cast<std::size_t>(std::popcount(bits)));

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const std::optional<Square> square = Square::from_index(index);
        if (square.has_value() && (bits & square->bit()) != 0) {
            squares.push_back(*square);
        }
    }

    return squares;
}

[[nodiscard]] inline std::optional<Square> best_eval_move(const Board& board) {
    const std::vector<Square> moves = squares_from_bitboard(legal_moves(board));
    if (moves.empty()) {
        return std::nullopt;
    }

    const Side side = board.side_to_move;
    std::optional<Square> best_move;
    int best_score = 0;

    for (Square move : moves) {
        const std::optional<Board> next = apply_move(board, move);
        if (!next.has_value()) {
            continue;
        }

        const int move_score = evaluate_basic(*next, side);
        if (!best_move.has_value() || move_score > best_score ||
            (move_score == best_score && move.index() < best_move->index())) {
            best_move = move;
            best_score = move_score;
        }
    }

    return best_move;
}

[[nodiscard]] inline MoveSelection choose_move(const PlayerSpec& spec, const Board& board,
                                               std::mt19937_64& rng) {
    switch (spec.kind) {
    case PlayerKind::First:
        return MoveSelection{.move = first_legal_move(board)};
    case PlayerKind::Random: {
        const std::vector<Square> moves = squares_from_bitboard(legal_moves(board));
        if (moves.empty()) {
            return MoveSelection{};
        }

        std::uniform_int_distribution<std::size_t> distribution{0, moves.size() - 1};
        return MoveSelection{.move = moves[distribution(rng)]};
    }
    case PlayerKind::Eval:
        return MoveSelection{.move = best_eval_move(board)};
    case PlayerKind::Search: {
        const auto started = std::chrono::steady_clock::now();
        const SearchResult result = search(board, make_search_options(spec));
        const auto finished = std::chrono::steady_clock::now();
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>{finished - started}.count();
        return MoveSelection{.move = result.best_move,
                             .nodes = result.nodes,
                             .elapsed_ms = elapsed_ms,
                             .search_stats = result.stats};
    }
    }

    return MoveSelection{};
}

[[nodiscard]] inline std::pair<int, int> final_scores(const Board& board) noexcept {
    int black_score = disc_count(board, Side::Black);
    int white_score = disc_count(board, Side::White);
    const int empty_count = 64 - black_score - white_score;

    if (black_score > white_score) {
        black_score += empty_count;
    } else if (white_score > black_score) {
        white_score += empty_count;
    } else {
        black_score += empty_count / 2;
        white_score += empty_count - (empty_count / 2);
    }

    return {black_score, white_score};
}

[[nodiscard]] inline GameRecord run_game(int game_index, const PlayerSpec& black_spec,
                                         const PlayerSpec& white_spec, bool black_is_player_a,
                                         std::uint64_t seed, int opening_index,
                                         const Opening& opening) {
    constexpr int max_turns = 200;

    GameRecord record{
        .game_index = game_index,
        .seed = seed,
        .opening_index = opening_index,
        .opening_name = opening.name,
        .opening_moves = opening.moves,
        .start_board = to_string(opening.start_board),
        .black_spec = black_spec.text,
        .white_spec = white_spec.text,
        .player_a_spec = black_is_player_a ? black_spec.text : white_spec.text,
        .player_b_spec = black_is_player_a ? white_spec.text : black_spec.text,
        .black_is_player_a = black_is_player_a,
    };

    Board board = opening.start_board;
    std::mt19937_64 rng{seed};

    for (int turn = 0; !is_game_over(board); ++turn) {
        if (turn >= max_turns) {
            record.illegal_or_error = true;
            break;
        }

        const Bitboard moves = legal_moves(board);
        if (moves == 0) {
            const std::optional<Board> next = pass_turn(board);
            if (!next.has_value()) {
                record.illegal_or_error = true;
                break;
            }

            board = *next;
            ++record.passes;
            continue;
        }

        const PlayerSpec& spec = board.side_to_move == Side::Black ? black_spec : white_spec;
        const MoveSelection selection = choose_move(spec, board, rng);
        if (board.side_to_move == Side::Black) {
            record.nodes_black += selection.nodes;
            record.time_ms_black += selection.elapsed_ms;
        } else {
            record.nodes_white += selection.nodes;
            record.time_ms_white += selection.elapsed_ms;
        }

        const std::optional<Square> move = selection.move;
        if (!move.has_value() || (moves & move->bit()) == 0) {
            record.illegal_or_error = true;
            break;
        }

        const std::optional<Board> next = apply_move(board, *move);
        if (!next.has_value()) {
            record.illegal_or_error = true;
            break;
        }

        record.moves.push_back(to_string(*move));
        board = *next;
        ++record.plies;
    }

    const auto [black_score, white_score] = final_scores(board);
    record.black_score = black_score;
    record.white_score = white_score;
    record.score_diff_from_black = black_score - white_score;
    record.score_diff_from_player_a =
        record.black_is_player_a ? record.score_diff_from_black : -record.score_diff_from_black;
    record.nodes_player_a = record.black_is_player_a ? record.nodes_black : record.nodes_white;
    record.nodes_player_b = record.black_is_player_a ? record.nodes_white : record.nodes_black;
    record.time_ms_player_a =
        record.black_is_player_a ? record.time_ms_black : record.time_ms_white;
    record.time_ms_player_b =
        record.black_is_player_a ? record.time_ms_white : record.time_ms_black;

    if (record.score_diff_from_black > 0) {
        record.winner = "black";
    } else if (record.score_diff_from_black < 0) {
        record.winner = "white";
    }

    return record;
}

[[nodiscard]] inline GameRecord run_game(int game_index, const PlayerSpec& black_spec,
                                         const PlayerSpec& white_spec, bool black_is_player_a,
                                         std::uint64_t seed) {
    return run_game(game_index, black_spec, white_spec, black_is_player_a, seed, 0,
                    default_opening());
}

[[nodiscard]] inline std::size_t opening_index_for_game(int game_index, bool swap_sides,
                                                        std::size_t opening_count) noexcept {
    if (opening_count == 0) {
        return 0;
    }

    const int opening_game_index = swap_sides ? game_index / 2 : game_index;
    return static_cast<std::size_t>(opening_game_index) % opening_count;
}

[[nodiscard]] inline std::vector<GameRecord> run_match(const MatchConfig& config) {
    std::vector<GameRecord> records;
    records.reserve(static_cast<std::size_t>(std::max(config.games, 0)));
    const std::vector<Opening> fallback_openings{default_opening()};
    const std::span<const Opening> openings =
        config.openings.empty() ? std::span<const Opening>{fallback_openings}
                                : std::span<const Opening>{config.openings};

    for (int game_index = 0; game_index < config.games; ++game_index) {
        const bool swapped = config.swap_sides && (game_index % 2 == 1);
        const PlayerSpec& black_spec = swapped ? config.player_b : config.player_a;
        const PlayerSpec& white_spec = swapped ? config.player_a : config.player_b;
        const std::size_t opening_index =
            opening_index_for_game(game_index, config.swap_sides, openings.size());
        records.push_back(run_game(game_index, black_spec, white_spec, !swapped,
                                   config.seed + static_cast<std::uint64_t>(game_index),
                                   static_cast<int>(opening_index), openings[opening_index]));
    }

    return records;
}

[[nodiscard]] inline MatchSummary summarize(std::span<const GameRecord> records) {
    MatchSummary summary;
    summary.games = static_cast<int>(records.size());

    int total_diff_from_player_a = 0;
    for (const GameRecord& record : records) {
        total_diff_from_player_a += record.score_diff_from_player_a;

        if (record.score_diff_from_player_a > 0) {
            ++summary.player_a_wins;
        } else if (record.score_diff_from_player_a < 0) {
            ++summary.player_b_wins;
        } else {
            ++summary.draws;
        }
    }

    if (!records.empty()) {
        summary.average_disc_diff_from_player_a =
            static_cast<double>(total_diff_from_player_a) / static_cast<double>(records.size());
    }

    return summary;
}

} // namespace othello::match_runner
