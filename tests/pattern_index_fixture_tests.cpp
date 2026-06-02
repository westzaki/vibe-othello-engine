#include <catch2/catch_test_macros.hpp>
#include <othello/board.hpp>
#include <othello/evaluation_patterns.hpp>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using othello::Bitboard;
using othello::Board;
using othello::Side;

namespace {

struct ExpectedPatternIndex {
    std::string instance;
    int value = 0;
};

struct FamilyExpectation {
    std::string side_name;
    Side side = Side::Black;
    std::string family;
    std::vector<ExpectedPatternIndex> indexes;
};

struct PatternIndexFixture {
    std::string name;
    Side side_to_move = Side::Black;
    std::vector<std::string> rows;
    std::vector<FamilyExpectation> expectations;
};

[[nodiscard]] Side parse_side(std::string_view text) {
    if (text == "B") {
        return Side::Black;
    }
    if (text == "W") {
        return Side::White;
    }
    FAIL("unknown side token: " << text);
    return Side::Black;
}

[[nodiscard]] othello::Corner2x3PatternCorner
parse_corner_2x3(std::string_view text) {
    using Corner = othello::Corner2x3PatternCorner;
    if (text == "A1") {
        return Corner::A1;
    }
    if (text == "H1") {
        return Corner::H1;
    }
    if (text == "A8") {
        return Corner::A8;
    }
    if (text == "H8") {
        return Corner::H8;
    }
    FAIL("unknown corner_2x3 instance: " << text);
    return Corner::A1;
}

[[nodiscard]] othello::Corner3x3PatternCorner
parse_corner_3x3(std::string_view text) {
    using Corner = othello::Corner3x3PatternCorner;
    if (text == "A1") {
        return Corner::A1;
    }
    if (text == "H1") {
        return Corner::H1;
    }
    if (text == "A8") {
        return Corner::A8;
    }
    if (text == "H8") {
        return Corner::H8;
    }
    FAIL("unknown corner_3x3 instance: " << text);
    return Corner::A1;
}

[[nodiscard]] othello::Edge8PatternEdge parse_edge_8(std::string_view text) {
    using Edge = othello::Edge8PatternEdge;
    if (text == "Top") {
        return Edge::Top;
    }
    if (text == "Bottom") {
        return Edge::Bottom;
    }
    if (text == "Left") {
        return Edge::Left;
    }
    if (text == "Right") {
        return Edge::Right;
    }
    FAIL("unknown edge_8 instance: " << text);
    return Edge::Top;
}

[[nodiscard]] othello::EdgeX10PatternEdge parse_edge_x_10(std::string_view text) {
    using Edge = othello::EdgeX10PatternEdge;
    if (text == "Top") {
        return Edge::Top;
    }
    if (text == "Bottom") {
        return Edge::Bottom;
    }
    if (text == "Left") {
        return Edge::Left;
    }
    if (text == "Right") {
        return Edge::Right;
    }
    FAIL("unknown edge_x_10 instance: " << text);
    return Edge::Top;
}

[[nodiscard]] othello::Diagonal8PatternDiagonal
parse_diagonal_8(std::string_view text) {
    using Diagonal = othello::Diagonal8PatternDiagonal;
    if (text == "A1H8") {
        return Diagonal::A1H8;
    }
    if (text == "H1A8") {
        return Diagonal::H1A8;
    }
    FAIL("unknown diagonal_8 instance: " << text);
    return Diagonal::A1H8;
}

[[nodiscard]] othello::InnerRow8PatternLine
parse_inner_row_8(std::string_view text) {
    using Line = othello::InnerRow8PatternLine;
    if (text == "Top") {
        return Line::Top;
    }
    if (text == "Bottom") {
        return Line::Bottom;
    }
    if (text == "Left") {
        return Line::Left;
    }
    if (text == "Right") {
        return Line::Right;
    }
    FAIL("unknown inner_row_8 instance: " << text);
    return Line::Top;
}

[[nodiscard]] int pattern_index_for_instance(const Board& board, Side side,
                                             std::string_view family,
                                             std::string_view instance) {
    if (family == "corner_2x3") {
        return othello::corner_2x3_pattern_index(board, side,
                                                 parse_corner_2x3(instance));
    }
    if (family == "corner_3x3") {
        return othello::corner_3x3_pattern_index(board, side,
                                                 parse_corner_3x3(instance));
    }
    if (family == "edge_8") {
        return othello::edge_8_pattern_index(board, side, parse_edge_8(instance));
    }
    if (family == "edge_x_10") {
        return othello::edge_x_10_pattern_index(board, side,
                                                parse_edge_x_10(instance));
    }
    if (family == "diagonal_8") {
        return othello::diagonal_8_pattern_index(board, side,
                                                 parse_diagonal_8(instance));
    }
    if (family == "inner_row_8") {
        return othello::inner_row_8_pattern_index(board, side,
                                                  parse_inner_row_8(instance));
    }

    FAIL("unknown pattern family: " << family);
    return -1;
}

[[nodiscard]] Board board_from_index_order_rows(const std::vector<std::string>& rows,
                                                Side side_to_move) {
    REQUIRE(rows.size() == 8);

    Board board{.side_to_move = side_to_move};
    for (std::size_t row = 0; row < rows.size(); ++row) {
        REQUIRE(rows[row].size() == 8);
        for (std::size_t file = 0; file < rows[row].size(); ++file) {
            const Bitboard bit = Bitboard{1} << ((row * 8) + file);
            switch (rows[row][file]) {
            case 'B':
                board.black |= bit;
                break;
            case 'W':
                board.white |= bit;
                break;
            case '.':
                break;
            default:
                FAIL("unknown board cell: " << rows[row][file]);
            }
        }
    }

    REQUIRE((board.black & board.white) == 0);
    return board;
}

[[nodiscard]] std::vector<PatternIndexFixture> read_pattern_index_fixtures() {
    const std::filesystem::path path =
        std::filesystem::path{OTHELLO_SOURCE_DIR} / "tests" / "fixtures" /
        "pattern_index_drift.txt";
    std::ifstream file{path};
    REQUIRE(file.is_open());

    std::vector<PatternIndexFixture> fixtures;
    PatternIndexFixture current;
    bool has_current = false;

    const auto push_current = [&]() {
        if (has_current) {
            fixtures.push_back(std::move(current));
            current = PatternIndexFixture{};
            has_current = false;
        }
    };

    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream line_input{line};
        std::string keyword;
        line_input >> keyword;
        if (keyword == "fixture") {
            push_current();
            line_input >> current.name;
            REQUIRE_FALSE(current.name.empty());
            has_current = true;
        } else if (keyword == "side_to_move") {
            REQUIRE(has_current);
            std::string side;
            line_input >> side;
            current.side_to_move = parse_side(side);
        } else if (keyword == "row") {
            REQUIRE(has_current);
            std::string row;
            line_input >> row;
            current.rows.push_back(row);
        } else if (keyword == "expect") {
            REQUIRE(has_current);
            FamilyExpectation expectation;
            line_input >> expectation.side_name >> expectation.family;
            expectation.side = parse_side(expectation.side_name);

            std::string token;
            while (line_input >> token) {
                const std::size_t separator = token.find('=');
                REQUIRE(separator != std::string::npos);
                expectation.indexes.push_back(ExpectedPatternIndex{
                    .instance = token.substr(0, separator),
                    .value = std::stoi(token.substr(separator + 1)),
                });
            }
            current.expectations.push_back(std::move(expectation));
        } else {
            FAIL("unknown fixture keyword: " << keyword);
        }
    }
    push_current();

    return fixtures;
}

} // namespace

TEST_CASE("Pattern index fixture matches C++ pattern definitions", "[evaluation]") {
    const std::vector<PatternIndexFixture> fixtures = read_pattern_index_fixtures();
    REQUIRE_FALSE(fixtures.empty());

    for (const PatternIndexFixture& fixture : fixtures) {
        CAPTURE(fixture.name);
        REQUIRE(fixture.expectations.size() == 12);
        const Board board =
            board_from_index_order_rows(fixture.rows, fixture.side_to_move);

        for (const FamilyExpectation& expectation : fixture.expectations) {
            CAPTURE(expectation.side_name);
            CAPTURE(expectation.family);
            REQUIRE_FALSE(expectation.indexes.empty());

            for (const ExpectedPatternIndex& expected : expectation.indexes) {
                CAPTURE(expected.instance);
                CHECK(pattern_index_for_instance(board, expectation.side,
                                                 expectation.family,
                                                 expected.instance) == expected.value);
            }
        }
    }
}
