#include <array>
#include <catch2/catch_test_macros.hpp>
#include <othello/othello.hpp>
#include <string>
#include <string_view>

TEST_CASE("Coordinates convert to and from square indexes", "[square]") {
    struct Example {
        std::string_view coordinate;
        int index;
    };

    constexpr std::array examples{
        Example{.coordinate = "a1", .index = 0},  Example{.coordinate = "h1", .index = 7},
        Example{.coordinate = "a8", .index = 56}, Example{.coordinate = "h8", .index = 63},
        Example{.coordinate = "d3", .index = 19}, Example{.coordinate = "c4", .index = 26},
    };

    for (const Example example : examples) {
        CAPTURE(std::string{example.coordinate});

        const auto square = othello::square_from_string(example.coordinate);

        REQUIRE(square.has_value());
        CHECK(square->index() == example.index);
        CHECK(othello::to_string(*square) == std::string{example.coordinate});
    }
}

TEST_CASE("Invalid coordinates are rejected", "[square]") {
    CHECK_FALSE(othello::square_from_string("").has_value());
    CHECK_FALSE(othello::square_from_string("i1").has_value());
    CHECK_FALSE(othello::square_from_string("a9").has_value());
    CHECK_FALSE(othello::square_from_string("D3").has_value());
}
