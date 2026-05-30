#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <optional>
#include <othello/board.hpp>
#include <othello/square.hpp>
#include <vector>

namespace othello::search_detail {

struct PrincipalVariation {
    std::array<int, 64> indexes{};
    std::size_t size = 0;
};

struct NodeResult {
    std::optional<Square> best_move;
    int score = 0;
    PrincipalVariation principal_variation;
};

[[nodiscard]] inline int empty_count(const Board& board) noexcept {
    return std::popcount(board.empty());
}

[[nodiscard]] inline Board board_after_move(const Board& board, Square square,
                                            Bitboard flips) noexcept {
    const Bitboard move_bit = square.bit();

    Board next = board;
    if (board.side_to_move == Side::Black) {
        next.black = board.black | move_bit | flips;
        next.white = board.white & ~flips;
    } else {
        next.white = board.white | move_bit | flips;
        next.black = board.black & ~flips;
    }
    next.side_to_move = opponent(board.side_to_move);

    return next;
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

} // namespace othello::search_detail
