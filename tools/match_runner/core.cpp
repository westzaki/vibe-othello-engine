#include "core.hpp"

#include "engine_config.hpp"
#include "external_player.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <optional>
#include <othello/othello.hpp>
#include <random>
#include <string>
#include <utility>
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

[[nodiscard]] std::string side_name(Side side) {
    return side == Side::Black ? "black" : "white";
}

[[nodiscard]] ExactRootTraceStats exact_root_trace_stats(const SearchStats& stats) noexcept {
    return ExactRootTraceStats{
        .nodes = stats.nodes,
        .tt_lookups = stats.tt_lookups,
        .tt_hits = stats.tt_hits,
        .tt_exact_hits = stats.tt_exact_hits,
        .tt_lower_hits = stats.tt_lower_hits,
        .tt_upper_hits = stats.tt_upper_hits,
        .tt_stores = stats.tt_stores,
        .tt_leaf_stores = stats.tt_leaf_stores,
        .tt_overwrites = stats.tt_overwrites,
        .tt_collisions = stats.tt_collisions,
        .tt_rejected_stores = stats.tt_rejected_stores,
        .tt_move_ordering_probes = stats.tt_move_ordering_probes,
        .tt_move_ordering_hits = stats.tt_move_ordering_hits,
        .tt_move_ordering_used = stats.tt_move_ordering_used,
    };
}

[[nodiscard]] ExactRootTrace exact_root_trace(const Board& board, const MoveSelection& selection,
                                              int ply, bool black_is_player_a) {
    const bool player_a =
        board.side_to_move == Side::Black ? black_is_player_a : !black_is_player_a;
    return ExactRootTrace{
        .ply = ply,
        .side = side_name(board.side_to_move),
        .player = player_a ? "A" : "B",
        .board = to_string(board),
        .empties = selection.exact_root_decision.empty_count,
        .legal_moves_current = selection.exact_root_decision.legal_moves_current,
        .legal_moves_opponent = selection.exact_root_decision.legal_moves_opponent,
        .best_move = selection.move,
        .score = selection.score,
        .depth = selection.depth,
        .nodes = selection.nodes,
        .elapsed_ms = selection.elapsed_ms,
        .stats = selection.search_stats.has_value()
                     ? exact_root_trace_stats(*selection.search_stats)
                     : ExactRootTraceStats{},
        .principal_variation = selection.principal_variation,
    };
}

} // namespace

InProcessPlayer::InProcessPlayer(PlayerSpec spec) : spec_{std::move(spec)} {}

void InProcessPlayer::reset_for_new_game() noexcept {
    search_session_.reset();
}

MoveSelection InProcessPlayer::choose_move(const Board& board, std::mt19937_64& rng) {
    switch (spec_.kind) {
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
        const SearchOptions search_options = make_search_options(spec_);
        const ExactEndgameRootDecision exact_root_decision =
            decide_exact_endgame_root(board, search_options);
        const auto started = std::chrono::steady_clock::now();
        const SearchResult result = spec_.search_options.use_iterative_search
                                        ? search_iterative(search_session_, board, search_options)
                                        : search(search_session_, board, search_options);
        const auto finished = std::chrono::steady_clock::now();
        const double elapsed_ms =
            std::chrono::duration<double, std::milli>{finished - started}.count();
        return MoveSelection{.move = result.best_move,
                             .nodes = result.nodes,
                             .elapsed_ms = elapsed_ms,
                             .exact_root_searches = exact_root_decision.solve_exact ? 1U : 0U,
                             .exact_root_decision = exact_root_decision,
                             .score = result.score,
                             .depth = result.depth,
                             .principal_variation = result.principal_variation,
                             .search_stats = result.stats};
    }
    case PlayerKind::ExternalNBoard:
        return MoveSelection{};
    }

    return MoveSelection{};
}

std::uint32_t InProcessPlayer::search_session_generation() const noexcept {
    return search_session_.generation();
}

MoveSelection choose_move(const PlayerSpec& spec, const Board& board, std::mt19937_64& rng) {
    InProcessPlayer player{spec};
    player.reset_for_new_game();
    return player.choose_move(board, rng);
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
                    const Opening& opening, std::span<const ExternalEngineConfig> external_engines,
                    int external_timeout_ms) {
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
    std::vector<std::string> full_moves = opening.moves;
    std::mt19937_64 rng{seed};
    InProcessPlayer black_player{black_spec};
    InProcessPlayer white_player{white_spec};
    black_player.reset_for_new_game();
    white_player.reset_for_new_game();
    ExternalNBoardPlayer black_external;
    ExternalNBoardPlayer white_external;

    auto set_error = [&record](std::string reason) {
        record.illegal_or_error = true;
        if (!record.error_reason.has_value()) {
            record.error_reason = std::move(reason);
        }
    };
    auto finish_record = [&record](const Board& final_board) {
        const auto [black_score, white_score] = final_scores(final_board);
        record.black_score = black_score;
        record.white_score = white_score;
        record.score_diff_from_black = black_score - white_score;
        record.score_diff_from_player_a =
            record.black_is_player_a ? record.score_diff_from_black : -record.score_diff_from_black;
        record.nodes_player_a = record.black_is_player_a ? record.nodes_black : record.nodes_white;
        record.nodes_player_b = record.black_is_player_a ? record.nodes_white : record.nodes_black;
        record.exact_roots_player_a =
            record.black_is_player_a ? record.exact_roots_black : record.exact_roots_white;
        record.exact_roots_player_b =
            record.black_is_player_a ? record.exact_roots_white : record.exact_roots_black;
        record.time_ms_player_a =
            record.black_is_player_a ? record.time_ms_black : record.time_ms_white;
        record.time_ms_player_b =
            record.black_is_player_a ? record.time_ms_white : record.time_ms_black;

        if (record.score_diff_from_black > 0) {
            record.winner = "black";
        } else if (record.score_diff_from_black < 0) {
            record.winner = "white";
        }
    };

    auto start_external = [&](const PlayerSpec& spec, ExternalNBoardPlayer& player) -> bool {
        if (spec.kind != PlayerKind::ExternalNBoard) {
            return true;
        }
        const auto config = find_engine_config(spec.external_engine_name, external_engines);
        if (!config.has_value()) {
            set_error("missing external engine config: " + spec.external_engine_name);
            return false;
        }
        if (!player.start(**config)) {
            set_error(player.error());
            return false;
        }
        return true;
    };

    if (!start_external(black_spec, black_external) ||
        !start_external(white_spec, white_external)) {
        finish_record(board);
        return record;
    }

    for (int turn = 0; !is_game_over(board); ++turn) {
        if (turn >= max_turns) {
            set_error("maximum turn count exceeded");
            break;
        }

        const Bitboard moves = legal_moves(board);
        if (moves == 0) {
            const std::optional<Board> next = pass_turn(board);
            if (!next.has_value()) {
                set_error("pass failed");
                break;
            }

            board = *next;
            full_moves.push_back("pass");
            ++record.passes;
            continue;
        }

        const PlayerSpec& spec = board.side_to_move == Side::Black ? black_spec : white_spec;
        MoveSelection selection;
        if (spec.kind == PlayerKind::ExternalNBoard) {
            ExternalNBoardPlayer& player =
                board.side_to_move == Side::Black ? black_external : white_external;
            const ExternalMoveResult external_result = player.choose_move(
                board, full_moves, std::chrono::milliseconds{external_timeout_ms});
            selection.move = external_result.move;
            selection.elapsed_ms = external_result.elapsed_ms;
            if (!external_result.error.empty()) {
                set_error(external_result.error);
            }
        } else {
            InProcessPlayer& player =
                board.side_to_move == Side::Black ? black_player : white_player;
            selection = player.choose_move(board, rng);
        }
        if (board.side_to_move == Side::Black) {
            record.nodes_black += selection.nodes;
            record.time_ms_black += selection.elapsed_ms;
            record.exact_roots_black += selection.exact_root_searches;
        } else {
            record.nodes_white += selection.nodes;
            record.time_ms_white += selection.elapsed_ms;
            record.exact_roots_white += selection.exact_root_searches;
        }
        if (selection.exact_root_searches > 0) {
            record.exact_root_events.push_back(exact_root_trace(
                board, selection, static_cast<int>(full_moves.size()), record.black_is_player_a));
        }

        const std::optional<Square> move = selection.move;
        if (!move.has_value() || (moves & move->bit()) == 0) {
            if (!record.error_reason.has_value()) {
                set_error("player returned no legal move");
            }
            break;
        }

        const std::optional<Board> next = apply_move(board, *move);
        if (!next.has_value()) {
            set_error("apply_move failed");
            break;
        }

        record.moves.push_back(to_string(*move));
        full_moves.push_back(to_string(*move));
        board = *next;
        ++record.plies;
    }

    finish_record(board);

    return record;
}

GameRecord run_game(int game_index, const PlayerSpec& black_spec, const PlayerSpec& white_spec,
                    bool black_is_player_a, std::uint64_t seed, int opening_index,
                    const Opening& opening) {
    return run_game(game_index, black_spec, white_spec, black_is_player_a, seed, opening_index,
                    opening, {}, 10000);
}

GameRecord run_game(int game_index, const PlayerSpec& black_spec, const PlayerSpec& white_spec,
                    bool black_is_player_a, std::uint64_t seed) {
    return run_game(game_index, black_spec, white_spec, black_is_player_a, seed, 0,
                    default_opening(), {}, 10000);
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
    const std::span<const Opening> openings = config.openings.empty()
                                                  ? std::span<const Opening>{fallback_openings}
                                                  : std::span<const Opening>{config.openings};

    for (int game_index = 0; game_index < config.games; ++game_index) {
        const bool swapped = config.swap_sides && (game_index % 2 == 1);
        const PlayerSpec& black_spec = swapped ? config.player_b : config.player_a;
        const PlayerSpec& white_spec = swapped ? config.player_a : config.player_b;
        const std::size_t opening_index =
            opening_index_for_game(game_index, config.swap_sides, openings.size());
        records.push_back(run_game(game_index, black_spec, white_spec, !swapped,
                                   config.seed + static_cast<std::uint64_t>(game_index),
                                   static_cast<int>(opening_index), openings[opening_index],
                                   config.external_engines, config.external_timeout_ms));
    }

    return records;
}

MatchSummary summarize(std::span<const GameRecord> records) {
    MatchSummary summary;
    summary.games = static_cast<int>(records.size());

    int total_diff_from_player_a = 0;
    for (const GameRecord& record : records) {
        if (record.illegal_or_error) {
            ++summary.error_games;
            continue;
        }

        ++summary.valid_games;
        total_diff_from_player_a += record.score_diff_from_player_a;

        if (record.score_diff_from_player_a > 0) {
            ++summary.player_a_wins;
        } else if (record.score_diff_from_player_a < 0) {
            ++summary.player_b_wins;
        } else {
            ++summary.draws;
        }
    }

    if (summary.valid_games > 0) {
        summary.average_disc_diff_from_player_a = static_cast<double>(total_diff_from_player_a) /
                                                  static_cast<double>(summary.valid_games);
    }

    return summary;
}

} // namespace othello::match_runner
