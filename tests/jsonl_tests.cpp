#include "common/jsonl.hpp"

#include <catch2/catch_test_macros.hpp>

#include <sstream>
#include <string>

TEST_CASE("JSON object writer emits escaped fields with stable comma handling") {
    std::ostringstream output;
    othello::tools::JsonObjectWriter writer{output};

    writer.begin_object();
    writer.string_field("text", "a\nb\"c");
    writer.bool_field("ok", true);
    writer.int_field("neg", -3);
    writer.uint_field("count", 42);
    writer.double_field("ratio", 1.25);
    writer.null_field("missing");
    writer.end_object();

    CHECK(output.str() ==
          "{\"text\":\"a\\nb\\\"c\",\"ok\":true,\"neg\":-3,\"count\":42,"
          "\"ratio\":1.25,\"missing\":null}");
}
