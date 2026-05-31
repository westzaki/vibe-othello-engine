#include "common/jsonl.hpp"

#include <ostream>

namespace othello::tools {

std::string json_escape(std::string_view text) {
    std::string escaped;
    escaped.reserve(text.size() + 2);

    for (const char character : text) {
        switch (character) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\b':
            escaped += "\\b";
            break;
        case '\f':
            escaped += "\\f";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped += character;
            break;
        }
    }

    return escaped;
}

void write_json_string(std::ostream& output, std::string_view text) {
    output << '"' << json_escape(text) << '"';
}

JsonObjectWriter::JsonObjectWriter(std::ostream& output) noexcept : output_(&output) {}

void JsonObjectWriter::begin_object() {
    first_ = true;
    *output_ << '{';
}

void JsonObjectWriter::end_object() {
    *output_ << '}';
}

void JsonObjectWriter::field_name(std::string_view name) {
    if (!first_) {
        *output_ << ',';
    }
    first_ = false;
    write_json_string(*output_, name);
    *output_ << ':';
}

void JsonObjectWriter::string_field(std::string_view name, std::string_view value) {
    field_name(name);
    write_json_string(*output_, value);
}

void JsonObjectWriter::bool_field(std::string_view name, bool value) {
    field_name(name);
    *output_ << (value ? "true" : "false");
}

void JsonObjectWriter::int_field(std::string_view name, long long value) {
    field_name(name);
    *output_ << value;
}

void JsonObjectWriter::uint_field(std::string_view name, std::uint64_t value) {
    field_name(name);
    *output_ << value;
}

void JsonObjectWriter::double_field(std::string_view name, double value) {
    field_name(name);
    *output_ << value;
}

void JsonObjectWriter::null_field(std::string_view name) {
    field_name(name);
    *output_ << "null";
}

} // namespace othello::tools
