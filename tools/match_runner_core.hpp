#pragma once

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdint>
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

struct PlayerSpec {
    PlayerKind kind = PlayerKind::First;
    int depth = 0;
    std::string text = "first";

    [[nodiscard]] friend bool operator==(const PlayerSpec&, const PlayerSpec&) = default;
};

struct MatchConfig {
    PlayerSpec player_a;
    PlayerSpec player_b;
    int games = 1;
    bool swap_sides = false;
    std::uint64_t seed = 1;
};

struct GameRecord {
    int game_index = 0;
    std::uint64_t seed = 0;
    std::string black_spec;
    std::string white_spec;
    bool black_is_player_a = true;
    std::string winner = "draw";
    int black_score = 0;
    int white_score = 0;
    int score_diff_from_black = 0;
    int plies = 0;
    int passes = 0;
    std::vector<std::string> moves;
    bool illegal_or_error = false;

    [[nodiscard]] friend bool operator==(const GameRecord&, const GameRecord&) = default;
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
        const std::string_view depth_text = text.substr(search_prefix.size());
        const std::optional<int> depth = parse_non_negative_int(depth_text);
        if (!depth.has_value()) {
            return std::nullopt;
        }
        return PlayerSpec{.kind = PlayerKind::Search, .depth = *depth, .text = std::string{text}};
    }

    return std::nullopt;
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

[[nodiscard]] inline std::optional<Square> choose_move(const PlayerSpec& spec, const Board& board,
                                                       std::mt19937_64& rng) {
    switch (spec.kind) {
    case PlayerKind::First:
        return first_legal_move(board);
    case PlayerKind::Random: {
        const std::vector<Square> moves = squares_from_bitboard(legal_moves(board));
        if (moves.empty()) {
            return std::nullopt;
        }

        std::uniform_int_distribution<std::size_t> distribution{0, moves.size() - 1};
        return moves[distribution(rng)];
    }
    case PlayerKind::Eval:
        return best_eval_move(board);
    case PlayerKind::Search:
        return search_fixed_depth(board, spec.depth).best_move;
    }

    return std::nullopt;
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
                                         std::uint64_t seed) {
    constexpr int max_turns = 200;

    GameRecord record{
        .game_index = game_index,
        .seed = seed,
        .black_spec = black_spec.text,
        .white_spec = white_spec.text,
        .black_is_player_a = black_is_player_a,
    };

    Board board = Board::initial();
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
        const std::optional<Square> move = choose_move(spec, board, rng);
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

    if (record.score_diff_from_black > 0) {
        record.winner = "black";
    } else if (record.score_diff_from_black < 0) {
        record.winner = "white";
    }

    return record;
}

[[nodiscard]] inline std::vector<GameRecord> run_match(const MatchConfig& config) {
    std::vector<GameRecord> records;
    records.reserve(static_cast<std::size_t>(std::max(config.games, 0)));

    for (int game_index = 0; game_index < config.games; ++game_index) {
        const bool swapped = config.swap_sides && (game_index % 2 == 1);
        const PlayerSpec& black_spec = swapped ? config.player_b : config.player_a;
        const PlayerSpec& white_spec = swapped ? config.player_a : config.player_b;
        records.push_back(run_game(game_index, black_spec, white_spec, !swapped,
                                   config.seed + static_cast<std::uint64_t>(game_index)));
    }

    return records;
}

[[nodiscard]] inline MatchSummary summarize(std::span<const GameRecord> records) {
    MatchSummary summary;
    summary.games = static_cast<int>(records.size());

    int total_diff_from_player_a = 0;
    for (const GameRecord& record : records) {
        const int diff_from_player_a =
            record.black_is_player_a ? record.score_diff_from_black
                                     : -record.score_diff_from_black;
        total_diff_from_player_a += diff_from_player_a;

        if (diff_from_player_a > 0) {
            ++summary.player_a_wins;
        } else if (diff_from_player_a < 0) {
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
