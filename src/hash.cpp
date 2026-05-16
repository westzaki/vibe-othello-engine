#include <array>
#include <cstddef>
#include <othello/hash.hpp>

namespace {

constexpr othello::ZobristHash zobrist_seed = 0x4F4F5448454C4C4FULL;
constexpr std::size_t black_index = 0;
constexpr std::size_t white_index = 1;
constexpr int square_count = 64;

using PieceHashTable = std::array<std::array<othello::ZobristHash, square_count>, 2>;

[[nodiscard]] constexpr othello::ZobristHash splitmix64_next(othello::ZobristHash& state) noexcept {
    state += 0x9E3779B97F4A7C15ULL;
    auto value = state;
    value = (value ^ (value >> 30)) * 0xBF58476D1CE4E5B9ULL;
    value = (value ^ (value >> 27)) * 0x94D049BB133111EBULL;
    return value ^ (value >> 31);
}

[[nodiscard]] constexpr PieceHashTable make_piece_hashes() noexcept {
    PieceHashTable table{};
    auto state = zobrist_seed;

    for (auto& side_hashes : table) {
        for (auto& square_hash : side_hashes) {
            square_hash = splitmix64_next(state);
        }
    }

    return table;
}

[[nodiscard]] constexpr std::array<othello::ZobristHash, 2> make_side_hashes() noexcept {
    auto state = zobrist_seed;

    for (int index = 0; index < square_count * 2; ++index) {
        static_cast<void>(splitmix64_next(state));
    }

    return {splitmix64_next(state), splitmix64_next(state)};
}

[[nodiscard]] constexpr std::size_t side_index(othello::Side side) noexcept {
    return side == othello::Side::Black ? black_index : white_index;
}

[[nodiscard]] constexpr othello::Bitboard bit_at(int square_index) noexcept {
    return othello::Bitboard{1} << square_index;
}

constexpr auto piece_hashes = make_piece_hashes();
constexpr auto side_hashes = make_side_hashes();

} // namespace

namespace othello {

ZobristHash zobrist_hash(const Board& board) noexcept {
    ZobristHash hash = 0;

    for (int square_index = 0; square_index < square_count; ++square_index) {
        const Bitboard square_bit = bit_at(square_index);
        if ((board.black & square_bit) != 0) {
            hash ^= piece_hashes[black_index][static_cast<std::size_t>(square_index)];
        }
        if ((board.white & square_bit) != 0) {
            hash ^= piece_hashes[white_index][static_cast<std::size_t>(square_index)];
        }
    }

    hash ^= side_hashes[side_index(board.side_to_move)];
    return hash;
}

} // namespace othello
