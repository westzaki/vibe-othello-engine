#include "positions/fixtures.hpp"

#include "common/cli.hpp"

#include <bit>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

namespace othello::benchmarks {
namespace {

struct PendingPositionMetadata {
    std::string name;
    std::string phase;
    std::string tags;
    std::string note;
};

[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] bool starts_with(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
}

[[nodiscard]] std::string metadata_value(std::string_view comment,
                                         std::string_view key) {
    comment.remove_prefix(key.size());
    return std::string{trim(comment)};
}

void apply_metadata_line(std::string_view line, PendingPositionMetadata& metadata) {
    std::string_view comment = trim(line);
    if (comment.empty() || comment.front() != '#') {
        return;
    }
    comment.remove_prefix(1);
    comment = trim(comment);

    if (starts_with(comment, "name:")) {
        metadata.name = metadata_value(comment, "name:");
    } else if (starts_with(comment, "phase:")) {
        metadata.phase = metadata_value(comment, "phase:");
    } else if (starts_with(comment, "tags:")) {
        metadata.tags = metadata_value(comment, "tags:");
    } else if (starts_with(comment, "note:")) {
        metadata.note = metadata_value(comment, "note:");
    }
}

[[nodiscard]] bool is_ignored_line(std::string_view line) noexcept {
    line = trim(line);
    return line.empty() || line.front() == '#';
}

[[nodiscard]] std::string join_board_block(const std::vector<std::string>& lines) {
    std::string text;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index != 0) {
            text += '\n';
        }
        text += lines[index];
    }
    return text;
}

[[nodiscard]] std::string missing_metadata_error(std::string_view source_name,
                                                 int block_start_line,
                                                 std::string_view key) {
    std::ostringstream error;
    error << source_name << ": missing # " << key << ": before board block starting at line "
          << block_start_line;
    return error.str();
}

} // namespace

[[nodiscard]] std::uint64_t mix_checksum(std::uint64_t checksum,
                                                std::uint64_t value) noexcept {
    return std::rotl(checksum ^ value, 7) + 0x9E3779B97F4A7C15ULL;
}

[[nodiscard]] std::uint64_t side_checksum(Side side) noexcept {
    return side == Side::Black ? 0xB1A2C3D4E5F60718ULL : 0xF1E2D3C4B5A69788ULL;
}

[[nodiscard]] std::uint64_t board_checksum(const Board& board) noexcept {
    auto checksum = mix_checksum(0, board.black);
    checksum = mix_checksum(checksum, board.white);
    return mix_checksum(checksum, side_checksum(board.side_to_move));
}

[[nodiscard]] std::uint64_t search_result_checksum(const SearchResult& result) noexcept {
    auto checksum = mix_checksum(0, static_cast<std::uint64_t>(result.score));
    checksum = mix_checksum(checksum, static_cast<std::uint64_t>(result.depth));

    const auto move_value = result.best_move.has_value()
                                ? static_cast<std::uint64_t>(result.best_move->index() + 1)
                                : 0;
    return mix_checksum(checksum, move_value);
}

[[nodiscard]] std::optional<std::uint64_t>
parse_positive_count(std::string_view text) noexcept {
    return tools::parse_positive_count(text);
}

[[nodiscard]] std::vector<Square> squares_from_bitboard(Bitboard bits) {
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

[[nodiscard]] bool add_position(std::vector<Position>& positions, std::string_view name,
                                       std::string_view board_text,
                                       std::string_view phase, std::string_view tags,
                                       std::string_view notes) {
    auto board = board_from_string(board_text);
    if (!board.has_value()) {
        std::cerr << "failed to parse fixed benchmark position: " << name << '\n';
        return false;
    }

    positions.push_back(Position{.name = std::string{name},
                                 .phase = std::string{phase},
                                 .tags = std::string{tags},
                                 .board_text = std::string{board_text},
                                 .notes = std::string{notes},
                                 .board = *board});
    return true;
}

std::filesystem::path evaluation_diagnostic_suite_path() {
    return std::filesystem::path{OTHELLO_SOURCE_DIR} / "data" / "positions" / "evaluation" /
           "diagnostic_suite.txt";
}

std::optional<std::vector<Position>>
load_positions_from_text(std::string_view text, std::string_view source_name,
                         std::string& error) {
    std::istringstream input{std::string{text}};
    std::vector<Position> positions;
    std::vector<std::string> block;
    PendingPositionMetadata metadata;
    int block_start_line = 0;
    int line_number = 0;
    std::string line;

    while (std::getline(input, line)) {
        ++line_number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (is_ignored_line(line)) {
            if (block.empty()) {
                apply_metadata_line(line, metadata);
            }
            continue;
        }

        if (block.empty()) {
            block_start_line = line_number;
        }
        block.push_back(line);

        if (block.size() != 9) {
            continue;
        }

        if (metadata.name.empty()) {
            error = missing_metadata_error(source_name, block_start_line, "name");
            return std::nullopt;
        }
        if (metadata.phase.empty()) {
            error = missing_metadata_error(source_name, block_start_line, "phase");
            return std::nullopt;
        }

        const std::string board_text = join_board_block(block);
        if (!add_position(positions, metadata.name, board_text, metadata.phase, metadata.tags,
                          metadata.note)) {
            std::ostringstream parse_error;
            parse_error << source_name << ": invalid board block for " << metadata.name
                        << " starting at line " << block_start_line;
            error = parse_error.str();
            return std::nullopt;
        }

        block.clear();
        metadata = PendingPositionMetadata{};
    }

    if (!block.empty()) {
        std::ostringstream incomplete_error;
        incomplete_error << source_name << ": incomplete board block starting at line "
                         << block_start_line << ": expected 9 non-comment lines";
        error = incomplete_error.str();
        return std::nullopt;
    }
    if (!metadata.name.empty() || !metadata.phase.empty() || !metadata.tags.empty() ||
        !metadata.note.empty()) {
        error = std::string{source_name} + ": metadata block is not followed by a board position";
        return std::nullopt;
    }

    return positions;
}

std::optional<std::vector<Position>>
load_positions_from_file(const std::filesystem::path& path, std::string& error) {
    std::ifstream input{path};
    if (!input) {
        error = "failed to open position suite: " + path.string();
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        error = "failed to read position suite: " + path.string();
        return std::nullopt;
    }

    return load_positions_from_text(buffer.str(), path.string(), error);
}

[[nodiscard]] std::optional<std::vector<Position>> make_fixed_positions() {
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

std::optional<std::vector<Position>> make_evaluation_diagnostic_positions() {
    std::string error;
    auto positions = load_positions_from_file(evaluation_diagnostic_suite_path(), error);
    if (!positions.has_value()) {
        std::cerr << error << '\n';
    }
    return positions;
}

} // namespace othello::benchmarks
