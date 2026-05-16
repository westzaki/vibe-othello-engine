#include <array>
#include <bit>
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

constexpr auto piece_hashes = make_piece_hashes();
constexpr auto side_hashes = make_side_hashes();

[[nodiscard]] othello::ZobristHash piece_hash(othello::Bitboard bits,
                                              std::size_t color_index) noexcept {
    othello::ZobristHash hash = 0;

    while (bits != 0) {
        const auto square_index = static_cast<std::size_t>(std::countr_zero(bits));
        hash ^= piece_hashes[color_index][square_index];
        bits &= bits - 1;
    }

    return hash;
}

} // namespace

namespace othello {

ZobristHash zobrist_hash(const Board& board) noexcept {
    ZobristHash hash = piece_hash(board.black, black_index);
    hash ^= piece_hash(board.white, white_index);
    hash ^= side_hashes[side_index(board.side_to_move)];
    return hash;
}

} // namespace othello

namespace othello::detail {

// NOLINTNEXTLINE(misc-use-internal-linkage)
ZobristHash zobrist_piece_hash(Side side, int square_index) noexcept {
    return piece_hashes[side_index(side)][static_cast<std::size_t>(square_index)];
}

// NOLINTNEXTLINE(misc-use-internal-linkage)
ZobristHash zobrist_side_hash(Side side) noexcept {
    return side_hashes[side_index(side)];
}

} // namespace othello::detail
