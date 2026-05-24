#pragma once

#include "match_runner/types.hpp"

#include <filesystem>
#include <span>

namespace othello::match_runner {

[[nodiscard]] bool write_jsonl_file(const std::filesystem::path& output_path,
                                    std::span<const GameRecord> records);

} // namespace othello::match_runner
