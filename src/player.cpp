#include <othello/player.hpp>
#include <othello/rules.hpp>

namespace othello {

std::optional<Square> first_legal_move(const Board& board) noexcept {
    const Bitboard moves = legal_moves(board);

    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const auto square = Square::from_index(index);
        if (square.has_value() && (moves & square->bit()) != 0) {
            return square;
        }
    }

    return std::nullopt;
}

} // namespace othello
