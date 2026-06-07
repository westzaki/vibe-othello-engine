#pragma once

#include "bitboard_rules.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <optional>
#include <othello/board.hpp>
#include <othello/square.hpp>
#include <vector>

namespace othello::search_detail {

struct SearchPosition {
    Bitboard player = 0;
    Bitboard opponent_discs = 0;
    Side side_to_move = Side::Black;

    [[nodiscard]] static constexpr SearchPosition from_board(const Board& board) noexcept {
        return SearchPosition{
            .player = board.discs(board.side_to_move),
            .opponent_discs = board.discs(opponent(board.side_to_move)),
            .side_to_move = board.side_to_move,
        };
    }

    [[nodiscard]] constexpr Board to_board() const noexcept {
        if (side_to_move == Side::Black) {
            return Board{
                .black = player,
                .white = opponent_discs,
                .side_to_move = side_to_move,
            };
        }
        return Board{
            .black = opponent_discs,
            .white = player,
            .side_to_move = side_to_move,
        };
    }

    [[nodiscard]] constexpr Bitboard occupied() const noexcept {
        return player | opponent_discs;
    }

    [[nodiscard]] constexpr Bitboard empty() const noexcept {
        return ~occupied();
    }
};

struct PrincipalVariation {
    std::array<int, 64> indexes{};
    std::size_t size = 0;
};

struct NodeResult {
    std::optional<Square> best_move;
    int score = 0;
    PrincipalVariation principal_variation;
};

struct SearchNodeResult {
    std::optional<Square> best_move;
    int score = 0;
};

struct MidgamePvTable {
    std::array<PrincipalVariation, 65> lines{};

    void clear(std::size_t ply) noexcept {
        if (ply < lines.size()) {
            lines[ply] = PrincipalVariation{};
        }
    }

    void copy_from_child(std::size_t ply) noexcept {
        if (ply + 1 < lines.size()) {
            lines[ply] = lines[ply + 1];
            return;
        }
        clear(ply);
    }

    void set_single_move(std::size_t ply, Square move) noexcept {
        set_single_move_index(ply, move.index());
    }

    void set_single_move_index(std::size_t ply, int move_index) noexcept {
        if (ply >= lines.size()) {
            return;
        }
        lines[ply] = PrincipalVariation{};
        lines[ply].indexes[0] = move_index;
        lines[ply].size = 1;
    }

    void update_with_move(std::size_t ply, Square move) noexcept {
        update_with_move_index(ply, move.index());
    }

    void update_with_move_index(std::size_t ply, int move_index) noexcept {
        if (ply >= lines.size()) {
            return;
        }

        PrincipalVariation& line = lines[ply];
        line.indexes[0] = move_index;
        line.size = 1;

        if (ply + 1 >= lines.size()) {
            return;
        }

        const PrincipalVariation& child_line = lines[ply + 1];
        const std::size_t child_size = std::min(child_line.size, line.indexes.size() - 1);
        for (std::size_t index = 0; index < child_size; ++index) {
            line.indexes[index + 1] = child_line.indexes[index];
        }
        line.size += child_size;
    }
};

[[nodiscard]] inline int empty_count(const Board& board) noexcept {
    return std::popcount(board.empty());
}

[[nodiscard]] inline int empty_count(const SearchPosition& position) noexcept {
    return std::popcount(position.empty());
}

[[nodiscard]] inline Bitboard legal_moves(const SearchPosition& position) noexcept {
    return rules_detail::legal_moves_for(position.player, position.opponent_discs);
}

[[nodiscard]] inline Bitboard flips_for_move(const SearchPosition& position,
                                             Square square) noexcept {
    return rules_detail::flips_for_move_for(position.player, position.opponent_discs,
                                            square);
}

[[nodiscard]] inline Bitboard flips_for_move_bit(const SearchPosition& position,
                                                 Bitboard move_bit) noexcept {
    return rules_detail::flips_for_move_bit(position.player, position.opponent_discs,
                                            move_bit);
}

[[nodiscard]] inline Bitboard flips_for_known_empty_move(const SearchPosition& position,
                                                        Bitboard move_bit) noexcept {
    return rules_detail::flips_for_known_empty_move(position.player, position.opponent_discs,
                                                    move_bit);
}

[[nodiscard]] inline SearchPosition position_after_move_bit(const SearchPosition& position,
                                                            Bitboard move_bit,
                                                            Bitboard flips) noexcept {
    const Bitboard next_opponent = position.player | move_bit | flips;
    const Bitboard next_player = position.opponent_discs & ~flips;
    return SearchPosition{
        .player = next_player,
        .opponent_discs = next_opponent,
        .side_to_move = opponent(position.side_to_move),
    };
}

[[nodiscard]] inline SearchPosition position_after_move(const SearchPosition& position,
                                                        Square square,
                                                        Bitboard flips) noexcept {
    return position_after_move_bit(position, square.bit(), flips);
}

[[nodiscard]] inline SearchPosition
position_after_pass(const SearchPosition& position) noexcept {
    return SearchPosition{
        .player = position.opponent_discs,
        .opponent_discs = position.player,
        .side_to_move = opponent(position.side_to_move),
    };
}

[[nodiscard]] inline int score_for_player(const SearchPosition& position) noexcept {
    return std::popcount(position.player) - std::popcount(position.opponent_discs);
}

[[nodiscard]] inline bool is_better_best_move(int candidate_score, Square candidate,
                                              const std::optional<int>& best_score,
                                              const std::optional<Square>& best_move) noexcept {
    if (!best_score.has_value()) {
        return true;
    }
    if (candidate_score != *best_score) {
        return candidate_score > *best_score;
    }
    return !best_move.has_value() || candidate.index() < best_move->index();
}

[[nodiscard]] inline bool is_better_best_move_index(
    int candidate_score, int candidate_index, const std::optional<int>& best_score,
    const std::optional<int>& best_move_index) noexcept {
    if (!best_score.has_value()) {
        return true;
    }
    if (candidate_score != *best_score) {
        return candidate_score > *best_score;
    }
    return !best_move_index.has_value() || candidate_index < *best_move_index;
}

[[nodiscard]] inline PrincipalVariation
principal_variation_with_move(Square move, const PrincipalVariation& child_variation) noexcept {
    PrincipalVariation principal_variation;
    principal_variation.indexes[0] = move.index();
    principal_variation.size = 1;

    const std::size_t child_size =
        std::min(child_variation.size, principal_variation.indexes.size() - 1);
    for (std::size_t index = 0; index < child_size; ++index) {
        principal_variation.indexes[index + 1] = child_variation.indexes[index];
    }
    principal_variation.size += child_size;

    return principal_variation;
}

[[nodiscard]] inline std::vector<Square>
principal_variation_to_vector(const PrincipalVariation& principal_variation) noexcept {
    std::vector<Square> squares;
    try {
        squares.reserve(principal_variation.size);
        for (std::size_t index = 0; index < principal_variation.size; ++index) {
            const std::optional<Square> square =
                Square::from_index(principal_variation.indexes[index]);
            if (square.has_value()) {
                squares.push_back(*square);
            }
        }
    } catch (...) {
        return {};
    }
    return squares;
}

[[nodiscard]] inline PrincipalVariation
principal_variation_from_vector(const std::vector<Square>& principal_variation) noexcept {
    PrincipalVariation result;
    const std::size_t size = std::min(principal_variation.size(), result.indexes.size());
    for (std::size_t index = 0; index < size; ++index) {
        result.indexes[index] = principal_variation[index].index();
    }
    result.size = size;
    return result;
}

[[nodiscard]] inline std::optional<Square> square_from_transposition_index(int index) noexcept {
    if (index < Square::min_index || index > Square::max_index) {
        return std::nullopt;
    }
    return Square::from_index(index);
}

[[nodiscard]] inline NodeResult node_result_from_transposition_entry(int score,
                                                                     int best_move_index) noexcept {
    const std::optional<Square> best_move = square_from_transposition_index(best_move_index);
    NodeResult result{
        .best_move = best_move,
        .score = score,
    };

    // TT entries intentionally store only a score and best move, not a full line.
    // A cached result therefore reports the conservative PV fragment we know.
    if (best_move.has_value()) {
        result.principal_variation.indexes[0] = best_move->index();
        result.principal_variation.size = 1;
    }
    return result;
}

[[nodiscard]] inline SearchNodeResult
search_node_result_from_transposition_entry(int score, int best_move_index) noexcept {
    const std::optional<Square> best_move = square_from_transposition_index(best_move_index);
    return SearchNodeResult{
        .best_move = best_move,
        .score = score,
    };
}

} // namespace othello::search_detail
