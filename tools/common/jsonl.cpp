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

} // namespace othello::tools
