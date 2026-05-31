#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <optional>
#include <othello/othello.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace othello::tools::exact_labels {

inline constexpr std::string_view exact_label_schema = "exact_label.v1";

struct InputPosition {
    std::string position_id;
    Board board;
    int source_line = 0;
};

struct MoveScoreLabel {
    std::string move;
    int exact_score_side_to_move = 0;
};

struct ExactLabel {
    std::string schema;
    std::string position_id;
    std::string board_text;
    std::string side_to_move;
    int occupied = 0;
    int empties = 0;
    std::vector<std::string> legal_moves;
    int exact_score_side_to_move = 0;
    std::vector<std::string> best_moves;
    std::optional<std::string> best_move;
    std::vector<MoveScoreLabel> move_scores;
    bool include_move_scores = false;
    double elapsed_ms = 0.0;
    std::uint64_t nodes = 0;
};

struct DumpOptions {
    std::optional<std::size_t> limit = std::nullopt;
    int max_empties = 14;
    bool include_move_scores = false;
};

struct DumpSummary {
    std::size_t input_positions = 0;
    std::size_t labeled = 0;
    std::size_t skipped_too_many_empties = 0;
};

[[nodiscard]] std::optional<std::vector<InputPosition>>
parse_position_text(std::string_view text, std::string& error);

[[nodiscard]] ExactLabel make_exact_label(const InputPosition& position,
                                          bool include_move_scores);

void write_jsonl_record(std::ostream& output, const ExactLabel& label);

[[nodiscard]] bool write_exact_label_jsonl(std::span<const InputPosition> positions,
                                           const DumpOptions& options, std::ostream& output,
                                           DumpSummary& summary, std::string& error);

} // namespace othello::tools::exact_labels
