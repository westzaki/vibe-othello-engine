#include "../tools/protocols/nboard/game_codec.hpp"
#include "../tools/protocols/nboard/parser.hpp"

#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>
#include <string>
#include <vector>

namespace nboard = othello::tools::nboard;

TEST_CASE("NBoard move tokens parse coordinates and pass", "[nboard]") {
    const auto lower = nboard::parse_move_token("d3");
    const auto upper = nboard::parse_move_token("D3");
    const auto pass = nboard::parse_move_token("PASS");
    const auto nboard_pass = nboard::parse_move_token("PA");

    REQUIRE(lower.has_value());
    REQUIRE(lower->square.has_value());
    CHECK_FALSE(lower->pass);
    CHECK(lower->text == "d3");
    CHECK(othello::to_string(*lower->square) == "d3");

    REQUIRE(upper.has_value());
    CHECK(upper->text == "d3");

    REQUIRE(pass.has_value());
    CHECK(pass->pass);
    CHECK_FALSE(pass->square.has_value());
    CHECK(pass->text == "pass");

    REQUIRE(nboard_pass.has_value());
    CHECK(nboard_pass->pass);
}

TEST_CASE("NBoard move tokens reject invalid coordinates", "[nboard]") {
    CHECK_FALSE(nboard::parse_move_token("").has_value());
    CHECK_FALSE(nboard::parse_move_token("z9").has_value());
    CHECK_FALSE(nboard::parse_move_token("i1").has_value());
    CHECK_FALSE(nboard::parse_move_token("a9").has_value());
}

TEST_CASE("NBoard go output parses plain and labeled move lines", "[nboard]") {
    const auto plain = nboard::parse_go_move_line("=== d3");
    const auto labeled = nboard::parse_go_move_line("=== MOVE D3");
    const auto scored = nboard::parse_go_move_line("=== F5/-1.00/0.0");
    const auto pass = nboard::parse_go_move_line("=== MOVE pass");

    REQUIRE(plain.has_value());
    CHECK(plain->text == "d3");
    REQUIRE(labeled.has_value());
    CHECK(labeled->text == "d3");
    REQUIRE(scored.has_value());
    CHECK(scored->text == "f5");
    REQUIRE(pass.has_value());
    CHECK(pass->pass);

    CHECK_FALSE(nboard::parse_go_move_line("feature ping=1 done=1").has_value());
    CHECK_FALSE(nboard::parse_go_move_line("== d3").has_value());
}

TEST_CASE("NBoard pong lines match requested id", "[nboard]") {
    CHECK(nboard::is_pong_line("pong 1", "1"));
    CHECK(nboard::is_pong_line("  PONG 42  ", "42"));
    CHECK_FALSE(nboard::is_pong_line("pong 2", "1"));
    CHECK_FALSE(nboard::is_pong_line("feature ping=1", "1"));
}

TEST_CASE("NBoard move list applies legal history from initial board", "[nboard]") {
    const auto parsed = nboard::parse_move_list("d3 c3");

    REQUIRE(parsed.ok);
    REQUIRE(parsed.moves.size() == 2);
    CHECK(parsed.moves[0] == "d3");
    CHECK(parsed.moves[1] == "c3");
    CHECK(parsed.board.side_to_move == othello::Side::Black);
    CHECK(nboard::format_set_game_command(parsed.moves).starts_with("set game (;GM[Othello]"));
    CHECK(nboard::format_set_game_command(parsed.moves).find("B[D3]W[C3]") !=
          std::string::npos);
}

TEST_CASE("NBoard GGF game applies legal move history", "[nboard]") {
    const std::vector<std::string> moves{"d3", "c3"};
    const std::string ggf = nboard::format_ggf_game(moves);
    const auto parsed = nboard::parse_ggf_game(ggf);

    REQUIRE(parsed.ok);
    REQUIRE(parsed.moves.size() == 2);
    CHECK(parsed.moves[0] == "d3");
    CHECK(parsed.moves[1] == "c3");
    CHECK(parsed.board.side_to_move == othello::Side::Black);
}

TEST_CASE("NBoard move list rejects invalid and illegal history", "[nboard]") {
    CHECK_FALSE(nboard::parse_move_list("z9").ok);
    CHECK_FALSE(nboard::parse_move_list("a1").ok);
    CHECK_FALSE(nboard::parse_move_list("pass").ok);
}

TEST_CASE("NBoard response legality is checked against current board", "[nboard]") {
    const auto legal = nboard::parse_move_token("d3");
    const auto illegal = nboard::parse_move_token("a1");
    REQUIRE(legal.has_value());
    REQUIRE(illegal.has_value());

    const othello::Board initial = othello::Board::initial();
    CHECK(nboard::is_legal_response(initial, *legal));
    CHECK_FALSE(nboard::is_legal_response(initial, *illegal));
}
