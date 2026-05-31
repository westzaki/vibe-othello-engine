#include "common/jsonl.hpp"
#include "common/output_format.hpp"

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

TEST_CASE("Tool output format parser accepts text and jsonl only") {
    CHECK(othello::tools::parse_output_format("text") ==
          othello::tools::OutputFormat::Text);
    CHECK(othello::tools::parse_output_format("jsonl") ==
          othello::tools::OutputFormat::Jsonl);
    CHECK_FALSE(othello::tools::parse_output_format("yaml").has_value());
    CHECK_FALSE(othello::tools::parse_output_format("").has_value());
}
