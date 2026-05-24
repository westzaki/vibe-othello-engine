#include "core.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <optional>
#include <othello/othello.hpp>
#include <random>
#include <vector>

namespace othello::match_runner {
namespace {

[[nodiscard]] std::vector<Square> squares_from_bitboard(Bitboard bits) {
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

[[nodiscard]] std::optional<Square> best_eval_move(const Board& board) {
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

} // namespace

MoveSelection choose_move(const PlayerSpec& spec, const Board& board, std::mt19937_64& rng) {
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

std::pair<int, int> final_scores(const Board& board) noexcept {
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

GameRecord run_game(int game_index, const PlayerSpec& black_spec, const PlayerSpec& white_spec,
                    bool black_is_player_a, std::uint64_t seed, int opening_index,
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

GameRecord run_game(int game_index, const PlayerSpec& black_spec, const PlayerSpec& white_spec,
                    bool black_is_player_a, std::uint64_t seed) {
    return run_game(game_index, black_spec, white_spec, black_is_player_a, seed, 0,
                    default_opening());
}

std::size_t opening_index_for_game(int game_index, bool swap_sides,
                                   std::size_t opening_count) noexcept {
    if (opening_count == 0) {
        return 0;
    }

    const int opening_game_index = swap_sides ? game_index / 2 : game_index;
    return static_cast<std::size_t>(opening_game_index) % opening_count;
}

std::vector<GameRecord> run_match(const MatchConfig& config) {
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

MatchSummary summarize(std::span<const GameRecord> records) {
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
