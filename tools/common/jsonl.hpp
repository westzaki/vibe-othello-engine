#pragma once

#include <cstdint>
#include <iosfwd>
#include <string>
#include <string_view>

namespace othello::tools {

[[nodiscard]] std::string json_escape(std::string_view text);
void write_json_string(std::ostream& output, std::string_view text);

class JsonObjectWriter {
public:
    explicit JsonObjectWriter(std::ostream& output) noexcept;

    void begin_object();
    void end_object();

    void field_name(std::string_view name);
    void string_field(std::string_view name, std::string_view value);
    void bool_field(std::string_view name, bool value);
    void int_field(std::string_view name, long long value);
    void uint_field(std::string_view name, std::uint64_t value);
    void double_field(std::string_view name, double value);
    void null_field(std::string_view name);

private:
    std::ostream* output_;
    bool first_ = true;
};

} // namespace othello::tools
