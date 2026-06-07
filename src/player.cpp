#include <othello/player.hpp>
#include <othello/rules.hpp>

#include <bit>

namespace othello {

std::optional<Square> first_legal_move(const Board& board) noexcept {
    const Bitboard moves = legal_moves(board);
    if (moves == 0) {
        return std::nullopt;
    }

    return Square::from_index(std::countr_zero(moves));
}

} // namespace othello
