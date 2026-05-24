#pragma once

#include <iosfwd>
#include <string>
#include <string_view>

namespace othello::tools {

[[nodiscard]] std::string json_escape(std::string_view text);
void write_json_string(std::ostream& output, std::string_view text);

} // namespace othello::tools
