#pragma once

#include <bit>
#include <charconv>
#include <cstdint>
#include <iostream>
#include <optional>
#include <othello/othello.hpp>
#include <string_view>
#include <system_error>
#include <vector>

namespace othello::benchmarks {

struct Position {
    std::string_view name;
    std::string_view phase;
    std::string_view tags;
    std::string_view board_text;
    std::string_view notes;
    Board board;
};

[[nodiscard]] inline std::uint64_t mix_checksum(std::uint64_t checksum,
                                                std::uint64_t value) noexcept {
    return std::rotl(checksum ^ value, 7) + 0x9E3779B97F4A7C15ULL;
}

[[nodiscard]] inline std::uint64_t side_checksum(Side side) noexcept {
    return side == Side::Black ? 0xB1A2C3D4E5F60718ULL : 0xF1E2D3C4B5A69788ULL;
}

[[nodiscard]] inline std::uint64_t board_checksum(const Board& board) noexcept {
    auto checksum = mix_checksum(0, board.black);
    checksum = mix_checksum(checksum, board.white);
    return mix_checksum(checksum, side_checksum(board.side_to_move));
}

[[nodiscard]] inline std::uint64_t search_result_checksum(const SearchResult& result) noexcept {
    auto checksum = mix_checksum(0, static_cast<std::uint64_t>(result.score));
    checksum = mix_checksum(checksum, static_cast<std::uint64_t>(result.depth));

    const auto move_value = result.best_move.has_value()
                                ? static_cast<std::uint64_t>(result.best_move->index() + 1)
                                : 0;
    return mix_checksum(checksum, move_value);
}

[[nodiscard]] inline std::optional<std::uint64_t>
parse_positive_count(std::string_view text) noexcept {
    std::uint64_t value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end || value == 0) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] inline std::vector<Square> squares_from_bitboard(Bitboard bits) {
    std::vector<Square> squares;
    squares.reserve(64);
    for (int index = Square::min_index; index <= Square::max_index; ++index) {
        const auto mask = Bitboard{1} << index;
        if ((bits & mask) != 0) {
            const auto square = Square::from_index(index);
            if (square.has_value()) {
                squares.push_back(*square);
            }
        }
    }
    return squares;
}

[[nodiscard]] inline bool add_position(std::vector<Position>& positions, std::string_view name,
                                       std::string_view board_text,
                                       std::string_view phase = "smoke", std::string_view tags = {},
                                       std::string_view notes = {}) {
    auto board = board_from_string(board_text);
    if (!board.has_value()) {
        std::cerr << "failed to parse fixed benchmark position: " << name << '\n';
        return false;
    }

    positions.push_back(Position{.name = name,
                                 .phase = phase,
                                 .tags = tags,
                                 .board_text = board_text,
                                 .notes = notes,
                                 .board = *board});
    return true;
}

[[nodiscard]] inline std::optional<std::vector<Position>> make_fixed_positions() {
    std::vector<Position> positions;
    positions.reserve(8);

    if (!add_position(positions, "initial",
                      "........\n"
                      "........\n"
                      "........\n"
                      "...BW...\n"
                      "...WB...\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "after d3",
                      "........\n"
                      "........\n"
                      "........\n"
                      "...BW...\n"
                      "...BB...\n"
                      "...B....\n"
                      "........\n"
                      "........\n"
                      "side=W\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "multi-direction",
                      "........\n"
                      "........\n"
                      "...B.B..\n"
                      "...WW...\n"
                      ".BW.WB..\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "edge horizontal",
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      ".WWWWWWB\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "edge vertical",
                      "B.......\n"
                      "W.......\n"
                      "W.......\n"
                      "W.......\n"
                      "W.......\n"
                      "W.......\n"
                      "W.......\n"
                      "........\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "corner flip",
                      "........\n"
                      "......W.\n"
                      ".....B..\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "pass",
                      "........\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "...BWWWW\n"
                      "........\n"
                      "........\n"
                      "........\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    if (!add_position(positions, "dense late-game-like",
                      "BBBBBBBB\n"
                      "BWWWWWWB\n"
                      "BWBBBBWB\n"
                      "BWB..BWB\n"
                      "BWBBBBWB\n"
                      "BWWWWWWB\n"
                      "BBBBBBBB\n"
                      "........\n"
                      "side=B\n")) {
        return std::nullopt;
    }

    return positions;
}

} // namespace othello::benchmarks
