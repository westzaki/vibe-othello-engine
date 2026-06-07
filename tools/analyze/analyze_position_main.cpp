#include "analyze/analysis.hpp"
#include "common/board_io.hpp"
#include "common/cli.hpp"
#include "common/evaluator_cli.hpp"
#include "common/evaluator_selection.hpp"
#include "common/formatting.hpp"
#include "common/jsonl.hpp"
#include "common/search_cli_options.hpp"
#include "common/stats.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <othello/othello.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using AnalysisMode = othello::tools::analyze::AnalysisMode;
using AnalysisOptions = othello::tools::analyze::AnalysisOptions;

struct BatchRecord {
    std::string position_id;
    std::string board_text;
};

void print_usage(std::string_view program_name) {
    std::cout << "usage: " << program_name
              << " (--board-file PATH | --stdin) [--depth N] [--mode fixed|iterative]"
                 " [--tt on|off] [--tt-entries N] [--exact-tt-entries N]"
                 " [--tt-store-leaf on|off] [--tt-min-probe-depth N]"
                 " [--tt-min-store-depth N] [--lazy-first-move-ordering on|off]"
                 " [--shallow-tt-move-ordering-hint on|off]"
                 " [--pvs on|off]"
                 " [--aspiration on|off] [--aspiration-window N]"
                 " [--aspiration-max-researches N]"
                 " [--aspiration-profile fixed|score-delta-aware]"
                 " [--exact-endgame-threshold N]"
                 " "
              << othello::tools::evaluator_cli_usage() << " [--root-candidates] [--batch-jsonl]\n"
              << '\n'
              << "Options:\n"
              << "  --board-file PATH  read a board in board_from_string format\n"
              << "  --stdin            read a board in board_from_string format from stdin\n"
              << "  --batch-jsonl      with --stdin, read JSONL rows with position_id and "
                 "board or board_text and write JSONL analysis rows\n"
              << "  --depth N          non-negative search depth (default: 10)\n"
              << "  --mode MODE        fixed or iterative (default: fixed)\n"
              << "  --tt on|off        enable or disable transposition table (default: on)\n"
              << "  --tt-entries N     requested transposition table entry count\n"
              << "  --exact-tt-entries N\n"
              << "                    requested private exact-endgame TT entries; 0 disables "
                 "only exact TT\n"
              << "  --tt-store-leaf on|off\n"
              << "                    store depth-0 midgame heuristic leaves in TT (default: on)\n"
              << "  --tt-min-probe-depth N\n"
              << "                    skip midgame TT probes below remaining depth N\n"
              << "  --tt-min-store-depth N\n"
              << "                    skip midgame TT stores below remaining depth N\n"
              << "  --lazy-first-move-ordering on|off\n"
              << "                    try a legal PV/root/TT preferred move before full ordering\n"
              << "  --shallow-tt-move-ordering-hint on|off\n"
              << "                    allow shallower matching TT best moves as ordering-only hints\n"
              << "  --pvs on|off       enable or disable PVS (default: off)\n"
              << "  --aspiration on|off\n"
              << "                    enable iterative-search aspiration windows (default: off)\n"
              << "  --aspiration-window N\n"
              << "                    positive initial aspiration half-window\n"
              << "  --aspiration-max-researches N\n"
              << "                    non-negative aspiration widening retries before full-window "
                 "fallback\n"
              << "  --aspiration-profile PROFILE\n"
              << "                    iterative aspiration window policy: fixed or score-delta-aware "
                 "(default: fixed)\n"
              << "  --exact-endgame-threshold N\n"
              << "                    solve root positions with at most N empties exactly; N <= 0 "
                 "disables\n"
              << othello::tools::evaluator_cli_help()
              << "  --root-candidates  analyze each legal root move separately\n"
              << "  --help             show this help text\n";
}

[[nodiscard]] bool append_utf8_escape(std::string& output, char escaped) {
    switch (escaped) {
    case '"':
        output += '"';
        return true;
    case '\\':
        output += '\\';
        return true;
    case '/':
        output += '/';
        return true;
    case 'b':
        output += '\b';
        return true;
    case 'f':
        output += '\f';
        return true;
    case 'n':
        output += '\n';
        return true;
    case 'r':
        output += '\r';
        return true;
    case 't':
        output += '\t';
        return true;
    default:
        return false;
    }
}

[[nodiscard]] bool parse_json_string(std::string_view text, std::size_t& index, std::string& output,
                                     std::string& error) {
    output.clear();
    if (index >= text.size() || text[index] != '"') {
        error = "expected JSON string";
        return false;
    }
    ++index;
    while (index < text.size()) {
        const char ch = text[index++];
        if (ch == '"') {
            return true;
        }
        if (ch == '\\') {
            if (index >= text.size()) {
                error = "unterminated JSON escape";
                return false;
            }
            const char escaped = text[index++];
            if (escaped == 'u') {
                error = "unicode escapes are not supported";
                return false;
            }
            if (!append_utf8_escape(output, escaped)) {
                error = "invalid JSON escape";
                return false;
            }
            continue;
        }
        output += ch;
    }
    error = "unterminated JSON string";
    return false;
}

void skip_json_space(std::string_view text, std::size_t& index) {
    while (index < text.size()) {
        const char ch = text[index];
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
            break;
        }
        ++index;
    }
}

[[nodiscard]] bool skip_json_value(std::string_view text, std::size_t& index, std::string& error) {
    skip_json_space(text, index);
    if (index >= text.size()) {
        error = "missing JSON value";
        return false;
    }
    if (text[index] == '"') {
        std::string ignored;
        return parse_json_string(text, index, ignored, error);
    }
    int nested = 0;
    while (index < text.size()) {
        const char ch = text[index];
        if (ch == '{' || ch == '[') {
            ++nested;
        } else if (ch == '}' || ch == ']') {
            if (nested == 0) {
                return true;
            }
            --nested;
        } else if (ch == ',' && nested == 0) {
            return true;
        }
        ++index;
    }
    return true;
}

[[nodiscard]] std::optional<BatchRecord> parse_batch_record(std::string_view line,
                                                            std::string& error) {
    std::map<std::string, std::string> string_fields;
    std::size_t index = 0;
    skip_json_space(line, index);
    if (index >= line.size() || line[index] != '{') {
        error = "expected JSON object";
        return std::nullopt;
    }
    ++index;
    while (true) {
        skip_json_space(line, index);
        if (index < line.size() && line[index] == '}') {
            ++index;
            break;
        }

        std::string key;
        if (!parse_json_string(line, index, key, error)) {
            return std::nullopt;
        }
        skip_json_space(line, index);
        if (index >= line.size() || line[index] != ':') {
            error = "expected ':' after JSON object key";
            return std::nullopt;
        }
        ++index;
        skip_json_space(line, index);
        if (index < line.size() && line[index] == '"') {
            std::string value;
            if (!parse_json_string(line, index, value, error)) {
                return std::nullopt;
            }
            string_fields.emplace(std::move(key), std::move(value));
        } else if (!skip_json_value(line, index, error)) {
            return std::nullopt;
        }
        skip_json_space(line, index);
        if (index < line.size() && line[index] == ',') {
            ++index;
            continue;
        }
        if (index < line.size() && line[index] == '}') {
            ++index;
            break;
        }
        error = "expected ',' or '}' after JSON object value";
        return std::nullopt;
    }
    skip_json_space(line, index);
    if (index != line.size()) {
        error = "unexpected trailing content after JSON object";
        return std::nullopt;
    }

    BatchRecord record;
    if (const auto position = string_fields.find("position_id"); position != string_fields.end()) {
        record.position_id = position->second;
    }
    if (const auto board = string_fields.find("board"); board != string_fields.end()) {
        record.board_text = board->second;
    } else if (const auto board_text = string_fields.find("board_text");
               board_text != string_fields.end()) {
        record.board_text = board_text->second;
    }
    if (record.position_id.empty()) {
        error = "missing required field: position_id";
        return std::nullopt;
    }
    if (record.board_text.empty()) {
        error = "missing required field: board or board_text";
        return std::nullopt;
    }
    return record;
}

void write_root_scores_json(
    const std::vector<othello::tools::analyze::RootCandidateAnalysis>& candidates) {
    std::cout << '{';
    bool first = true;
    for (const auto& candidate : candidates) {
        if (!first) {
            std::cout << ',';
        }
        first = false;
        othello::tools::write_json_string(
            std::cout, candidate.pass ? "pass" : othello::tools::format_square(candidate.move));
        std::cout << ':' << candidate.score;
    }
    std::cout << '}';
}

[[nodiscard]] const othello::tools::analyze::RootCandidateAnalysis*
top_root_candidate(const std::vector<othello::tools::analyze::RootCandidateAnalysis>& candidates) {
    return candidates.empty() ? nullptr : &candidates.front();
}

[[nodiscard]] const othello::tools::analyze::RootCandidateAnalysis*
candidate_for_result_best_move(
    const othello::SearchResult& result,
    const std::vector<othello::tools::analyze::RootCandidateAnalysis>& candidates) {
    if (!result.best_move.has_value()) {
        return nullptr;
    }
    const auto candidate =
        std::find_if(candidates.begin(), candidates.end(),
                     [&result](const othello::tools::analyze::RootCandidateAnalysis& current) {
                         return current.move == result.best_move;
                     });
    return candidate == candidates.end() ? nullptr : &*candidate;
}

[[nodiscard]] bool root_candidate_matches_result(
    const othello::SearchResult& result,
    const std::vector<othello::tools::analyze::RootCandidateAnalysis>& candidates) {
    const auto* top = top_root_candidate(candidates);
    if (top == nullptr) {
        return !result.best_move.has_value();
    }
    return top->move == result.best_move && top->score == result.score;
}

void write_optional_candidate_move_field(
    othello::tools::JsonObjectWriter& writer, std::string_view field_name,
    const othello::tools::analyze::RootCandidateAnalysis* candidate) {
    if (candidate == nullptr) {
        writer.null_field(field_name);
        return;
    }
    writer.string_field(field_name,
                        candidate->pass ? "pass" : othello::tools::format_square(candidate->move));
}

void write_optional_candidate_score_field(
    othello::tools::JsonObjectWriter& writer, std::string_view field_name,
    const othello::tools::analyze::RootCandidateAnalysis* candidate) {
    if (candidate == nullptr) {
        writer.null_field(field_name);
        return;
    }
    writer.int_field(field_name, candidate->score);
}

void write_batch_error(std::string_view position_id, std::string_view error) {
    othello::tools::JsonObjectWriter writer{std::cout};
    writer.begin_object();
    writer.string_field("position_id", position_id);
    writer.string_field("status", "error");
    writer.string_field("error", error);
    writer.end_object();
    std::cout << '\n';
}

[[nodiscard]] std::string format_batch_best_move(
    const othello::SearchResult& result,
    const std::vector<othello::tools::analyze::RootCandidateAnalysis>& candidates) {
    if (result.best_move.has_value()) {
        return othello::tools::format_square(result.best_move);
    }
    const auto pass_candidate =
        std::find_if(candidates.begin(), candidates.end(),
                     [](const othello::tools::analyze::RootCandidateAnalysis& candidate) {
                         return candidate.pass;
                     });
    return pass_candidate != candidates.end() ? "pass" : "-";
}

void write_batch_result(
    const BatchRecord& record, const AnalysisOptions& options, const othello::SearchResult& result,
    const std::vector<othello::tools::analyze::RootCandidateAnalysis>& candidates,
    std::chrono::nanoseconds elapsed) {
    othello::tools::JsonObjectWriter writer{std::cout};
    writer.begin_object();
    writer.string_field("position_id", record.position_id);
    writer.string_field("status", "ok");
    writer.string_field("best_move", format_batch_best_move(result, candidates));
    writer.int_field("score", result.score);
    writer.int_field("depth", options.depth);
    writer.uint_field("nodes", result.nodes);
    writer.double_field("elapsed_ms", othello::tools::elapsed_ms(elapsed));
    writer.string_field("root_score_semantics", "root_perspective_independent_child_search");
    const auto* top_candidate = top_root_candidate(candidates);
    const auto* result_best_candidate = candidate_for_result_best_move(result, candidates);
    write_optional_candidate_move_field(writer, "root_score_best_move", top_candidate);
    write_optional_candidate_score_field(writer, "root_score_best_score", top_candidate);
    write_optional_candidate_score_field(writer, "root_score_for_result_best_move",
                                         result_best_candidate);
    writer.bool_field("root_scores_match_result",
                      root_candidate_matches_result(result, candidates));
    writer.field_name("root_scores");
    write_root_scores_json(candidates);
    writer.end_object();
    std::cout << '\n';
}

[[nodiscard]] std::optional<AnalysisMode> parse_mode(std::string_view text) noexcept {
    if (text == "fixed") {
        return AnalysisMode::Fixed;
    }
    if (text == "iterative") {
        return AnalysisMode::Iterative;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<AnalysisOptions> parse_options(std::span<char* const> args,
                                                           bool& help_requested) {
    AnalysisOptions options;
    othello::tools::EvaluatorCliParseState evaluator_cli;

    for (std::size_t index = 1; index < args.size(); ++index) {
        const std::string_view arg{args[index]};

        if (arg == "--help") {
            help_requested = true;
            return options;
        }

        if (arg == "--board-file") {
            const auto value = othello::tools::next_argument(args, index, arg);
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.board_file = std::string{*value};
        } else if (arg == "--stdin") {
            options.read_stdin = true;
        } else if (arg == "--depth") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto depth =
                value.has_value() ? othello::tools::parse_non_negative_int(*value) : std::nullopt;
            if (!depth.has_value()) {
                std::cerr << "invalid --depth value\n";
                return std::nullopt;
            }
            options.depth = *depth;
        } else if (arg == "--mode") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto mode = value.has_value() ? parse_mode(*value) : std::nullopt;
            if (!mode.has_value()) {
                std::cerr << "invalid --mode value\n";
                return std::nullopt;
            }
            options.mode = *mode;
        } else if (arg == "--tt") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto tt = value.has_value() ? othello::tools::parse_on_off(*value) : std::nullopt;
            if (!tt.has_value()) {
                std::cerr << "invalid --tt value\n";
                return std::nullopt;
            }
            options.use_transposition_table = *tt;
        } else if (arg == "--tt-entries") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto entries =
                value.has_value() ? othello::tools::parse_entry_count(*value) : std::nullopt;
            if (!entries.has_value()) {
                std::cerr << "invalid --tt-entries value\n";
                return std::nullopt;
            }
            options.transposition_table_entries = *entries;
        } else if (arg == "--tt-store-leaf") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto store_leaf =
                value.has_value() ? othello::tools::parse_on_off(*value) : std::nullopt;
            if (!store_leaf.has_value()) {
                std::cerr << "invalid --tt-store-leaf value\n";
                return std::nullopt;
            }
            options.store_leaf_tt_entries = *store_leaf;
        } else if (arg == "--tt-min-probe-depth") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto depth =
                value.has_value() ? othello::tools::parse_non_negative_int(*value) : std::nullopt;
            if (!depth.has_value()) {
                std::cerr << "invalid --tt-min-probe-depth value\n";
                return std::nullopt;
            }
            options.tt_min_probe_depth = *depth;
        } else if (arg == "--tt-min-store-depth") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto depth =
                value.has_value() ? othello::tools::parse_non_negative_int(*value) : std::nullopt;
            if (!depth.has_value()) {
                std::cerr << "invalid --tt-min-store-depth value\n";
                return std::nullopt;
            }
            options.tt_min_store_depth = *depth;
        } else if (arg == "--lazy-first-move-ordering") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto lazy =
                value.has_value() ? othello::tools::parse_on_off(*value) : std::nullopt;
            if (!lazy.has_value()) {
                std::cerr << "invalid --lazy-first-move-ordering value\n";
                return std::nullopt;
            }
            options.use_lazy_first_move_ordering = *lazy;
        } else if (arg == "--shallow-tt-move-ordering-hint") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto shallow_hint =
                value.has_value() ? othello::tools::parse_on_off(*value) : std::nullopt;
            if (!shallow_hint.has_value()) {
                std::cerr << "invalid --shallow-tt-move-ordering-hint value\n";
                return std::nullopt;
            }
            options.use_shallow_tt_move_ordering_hint = *shallow_hint;
        } else if (arg == "--pvs") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto pvs =
                value.has_value() ? othello::tools::parse_on_off(*value) : std::nullopt;
            if (!pvs.has_value()) {
                std::cerr << "invalid --pvs value\n";
                return std::nullopt;
            }
            options.use_pvs = *pvs;
        } else if (arg == "--aspiration") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto aspiration =
                value.has_value() ? othello::tools::parse_on_off(*value) : std::nullopt;
            if (!aspiration.has_value()) {
                std::cerr << "invalid --aspiration value\n";
                return std::nullopt;
            }
            options.use_aspiration_window = *aspiration;
        } else if (arg == "--aspiration-window") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto window =
                value.has_value() ? othello::tools::parse_positive_int(*value) : std::nullopt;
            if (!window.has_value()) {
                std::cerr << "invalid --aspiration-window value\n";
                return std::nullopt;
            }
            options.aspiration_window = *window;
        } else if (arg == "--aspiration-max-researches") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto researches =
                value.has_value() ? othello::tools::parse_non_negative_int(*value) : std::nullopt;
            if (!researches.has_value()) {
                std::cerr << "invalid --aspiration-max-researches value\n";
                return std::nullopt;
            }
            options.aspiration_max_researches = *researches;
        } else if (arg == "--aspiration-profile") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto profile =
                value.has_value() ? othello::tools::parse_aspiration_profile(*value) : std::nullopt;
            if (!profile.has_value()) {
                std::cerr << "invalid --aspiration-profile value\n";
                return std::nullopt;
            }
            options.aspiration_profile = *profile;
        } else if (arg == "--exact-endgame-threshold") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto threshold =
                value.has_value() ? othello::tools::parse_int(*value) : std::nullopt;
            if (!threshold.has_value()) {
                std::cerr << "invalid --exact-endgame-threshold value\n";
                return std::nullopt;
            }
            options.exact_endgame_empty_threshold = *threshold;
        } else if (arg == "--eval-config") {
            std::string evaluator_cli_error;
            if (othello::tools::parse_evaluator_cli_option(args, index, evaluator_cli,
                                                           evaluator_cli_error) ==
                othello::tools::EvaluatorCliParseResult::Error) {
                std::cerr << evaluator_cli_error << '\n';
                return std::nullopt;
            }
        } else if (arg == "--root-candidates" || arg == "--root-breakdown") {
            options.root_candidates = true;
        } else if (arg == "--batch-jsonl") {
            options.batch_jsonl = true;
        } else {
            std::cerr << "unknown option: " << arg << '\n';
            return std::nullopt;
        }
    }

    const int input_count = (options.board_file.has_value() ? 1 : 0) + (options.read_stdin ? 1 : 0);
    if (input_count != 1) {
        std::cerr << "choose exactly one input source: --board-file PATH or --stdin\n";
        return std::nullopt;
    }
    if (options.batch_jsonl && !options.read_stdin) {
        std::cerr << "--batch-jsonl requires --stdin\n";
        return std::nullopt;
    }

    std::string evaluator_error;
    const std::optional<othello::tools::EvaluatorSelection> evaluator =
        othello::tools::parse_evaluator_selection(evaluator_cli.input, evaluator_error);
    if (!evaluator.has_value()) {
        std::cerr << evaluator_error << '\n';
        return std::nullopt;
    }
    options.evaluator = *evaluator;

    return options;
}

int run_batch_analysis(const AnalysisOptions& options, std::string_view input_text) {
    int failures = 0;
    std::size_t line_number = 0;
    std::size_t line_begin = 0;
    while (line_begin <= input_text.size()) {
        ++line_number;
        const std::size_t newline = input_text.find('\n', line_begin);
        const std::size_t line_end =
            newline == std::string_view::npos ? input_text.size() : newline;
        const std::string_view line = input_text.substr(line_begin, line_end - line_begin);
        line_begin = newline == std::string_view::npos ? input_text.size() + 1 : newline + 1;
        if (line.empty()) {
            continue;
        }

        std::string error;
        const std::optional<BatchRecord> record = parse_batch_record(line, error);
        if (!record.has_value()) {
            ++failures;
            write_batch_error("line-" + std::to_string(line_number), error);
            continue;
        }
        const std::optional<othello::Board> board = othello::board_from_string(record->board_text);
        if (!board.has_value()) {
            ++failures;
            write_batch_error(
                record->position_id,
                "invalid board input: expected 8 board rows followed by side=B or side=W");
            continue;
        }

        const auto start = Clock::now();
        const othello::SearchResult result = othello::tools::analyze::run_search(*board, options);
        const std::vector<othello::tools::analyze::RootCandidateAnalysis> candidates =
            options.root_candidates
                ? othello::tools::analyze::analyze_root_candidates(*board, options)
                : std::vector<othello::tools::analyze::RootCandidateAnalysis>{};
        const auto end = Clock::now();
        write_batch_result(*record, options, result, candidates, end - start);
        std::cout.flush();
    }
    return failures == 0 ? 0 : 1;
}

int run_analysis(std::span<char* const> args) {
    bool help_requested = false;
    const std::optional<AnalysisOptions> options = parse_options(args, help_requested);

    if (help_requested) {
        print_usage(args.empty() ? "othello_analyze_position" : args.front());
        return 0;
    }

    if (!options.has_value()) {
        print_usage(args.empty() ? "othello_analyze_position" : args.front());
        return 1;
    }

    const std::optional<std::string> board_text =
        options->read_stdin ? othello::tools::read_stdin_text()
                            : othello::tools::read_text_file(*options->board_file);
    if (!board_text.has_value()) {
        return 1;
    }

    if (options->batch_jsonl) {
        return run_batch_analysis(*options, *board_text);
    }

    const std::optional<othello::Board> board = othello::board_from_string(*board_text);
    if (!board.has_value()) {
        std::cerr << "invalid board input: expected 8 board rows followed by side=B or side=W\n";
        return 1;
    }

    const auto start = Clock::now();
    const othello::SearchResult result = othello::tools::analyze::run_search(*board, *options);
    const auto end = Clock::now();

    othello::tools::analyze::print_report(*board, *options, result, end - start);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run_analysis(std::span<char* const>{argv, static_cast<std::size_t>(argc)});
    } catch (const std::exception& exception) {
        std::cerr << "analysis failed: " << exception.what() << '\n';
    } catch (...) {
        std::cerr << "analysis failed with an unknown exception\n";
    }

    return 1;
}
