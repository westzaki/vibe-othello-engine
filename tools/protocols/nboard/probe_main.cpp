#include "common/cli.hpp"
#include "protocols/nboard/game_codec.hpp"
#include "protocols/nboard/protocol.hpp"
#include "protocols/nboard/process_client.hpp"

#include <chrono>
#include <iostream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    int depth = 4;
    int timeout_ms = 5000;
    std::string moves;
    std::optional<std::string> cwd;
    bool verbose = false;
    bool help = false;
    std::vector<std::string> engine_command;
};

void print_usage(std::string_view program) {
    std::cout << "usage: " << program
              << " [--moves \"d3 c3\"] [--depth N] [--timeout-ms N] [--cwd PATH] [--verbose]"
                 " --engine-cmd -- COMMAND [ARGS...]\n";
}

[[nodiscard]] std::optional<Options> parse_options(std::span<char* const> args) {
    Options options;
    std::size_t engine_boundary = args.size();
    for (std::size_t index = 1; index < args.size(); ++index) {
        if (std::string_view{args[index]} == "--engine-cmd") {
            engine_boundary = index;
            break;
        }
    }

    for (std::size_t index = 1; index < engine_boundary; ++index) {
        const std::string_view arg{args[index]};
        if (arg == "--help") {
            options.help = true;
            return options;
        }
        if (arg == "--verbose") {
            options.verbose = true;
            continue;
        }
        if (arg == "--moves") {
            const auto value = othello::tools::next_argument(args, index, arg);
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.moves = std::string{*value};
            continue;
        }
        if (arg == "--cwd") {
            const auto value = othello::tools::next_argument(args, index, arg);
            if (!value.has_value()) {
                return std::nullopt;
            }
            options.cwd = std::string{*value};
            continue;
        }
        if (arg == "--depth") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto depth =
                value.has_value() ? othello::tools::parse_positive_int(*value) : std::nullopt;
            if (!depth.has_value()) {
                std::cerr << "invalid --depth value\n";
                return std::nullopt;
            }
            options.depth = *depth;
            continue;
        }
        if (arg == "--timeout-ms") {
            const auto value = othello::tools::next_argument(args, index, arg);
            const auto timeout =
                value.has_value() ? othello::tools::parse_positive_int(*value) : std::nullopt;
            if (!timeout.has_value()) {
                std::cerr << "invalid --timeout-ms value\n";
                return std::nullopt;
            }
            options.timeout_ms = *timeout;
            continue;
        }
        std::cerr << "unknown option: " << arg << '\n';
        return std::nullopt;
    }

    if (engine_boundary == args.size()) {
        if (!options.help) {
            std::cerr << "missing --engine-cmd -- COMMAND\n";
        }
        return options.help ? std::optional{options} : std::nullopt;
    }
    std::size_t first_engine_arg = engine_boundary + 1;
    if (first_engine_arg < args.size() && std::string_view{args[first_engine_arg]} == "--") {
        ++first_engine_arg;
    }
    for (std::size_t index = first_engine_arg; index < args.size(); ++index) {
        options.engine_command.emplace_back(args[index]);
    }
    if (options.engine_command.empty()) {
        std::cerr << "missing engine command after --engine-cmd\n";
        return std::nullopt;
    }
    return options;
}

[[nodiscard]] bool wait_for_pong(othello::tools::nboard::ProcessClient& client,
                                 std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto line = client.read_line(std::chrono::milliseconds{200});
        if (!line.has_value()) {
            continue;
        }
        if (othello::tools::nboard::is_pong_line(*line, "1")) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool send_or_report(othello::tools::nboard::ProcessClient& client,
                                  std::string_view line) {
    if (client.send_line(line)) {
        return true;
    }
    std::cerr << "failed to send command: " << line << '\n';
    return false;
}

[[nodiscard]] std::optional<othello::tools::nboard::NBoardMove>
wait_for_move(othello::tools::nboard::ProcessClient& client, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto line = client.read_line(std::chrono::milliseconds{200});
        if (!line.has_value()) {
            continue;
        }
        const auto move = othello::tools::nboard::parse_go_move_line(*line);
        if (move.has_value()) {
            return move;
        }
    }
    return std::nullopt;
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char* const> args{argv, static_cast<std::size_t>(argc)};
    const auto options = parse_options(args);
    if (!options.has_value()) {
        print_usage(args.empty() ? "othello_probe_nboard" : args.front());
        return 2;
    }
    if (options->help) {
        print_usage(args.empty() ? "othello_probe_nboard" : args.front());
        return 0;
    }

    const auto parsed_moves = othello::tools::nboard::parse_move_list(options->moves);
    if (!parsed_moves.ok) {
        std::cerr << "invalid --moves: " << parsed_moves.error << '\n';
        return 2;
    }

    othello::tools::nboard::ProcessClient client;
    const auto started = client.start(othello::tools::nboard::ProcessStartOptions{
        .command = options->engine_command,
        .cwd = options->cwd,
        .verbose = options->verbose,
    });
    if (!started.ok) {
        std::cerr << "failed to start engine: " << started.error << '\n';
        return 1;
    }

    const auto timeout = std::chrono::milliseconds{options->timeout_ms};
    if (!send_or_report(client, othello::tools::nboard::nboard_command(2)) ||
        !send_or_report(client, othello::tools::nboard::set_game_command(parsed_moves.moves)) ||
        !send_or_report(client, othello::tools::nboard::set_depth_command(options->depth)) ||
        !send_or_report(client, othello::tools::nboard::ping_command(1))) {
        return 1;
    }
    if (!wait_for_pong(client, timeout)) {
        std::cerr << "engine did not respond to ping\n";
        return 1;
    }

    if (!send_or_report(client, othello::tools::nboard::go_command())) {
        return 1;
    }

    const auto move = wait_for_move(client, timeout);
    if (!move.has_value()) {
        std::cerr << "engine did not return a move\n";
        return 1;
    }

    const bool legal = othello::tools::nboard::is_legal_response(parsed_moves.board, *move);
    std::cout << "move: " << move->text << '\n';
    std::cout << "legal: " << (legal ? "yes" : "no") << '\n';
    std::cout << "board:\n" << othello::to_string(parsed_moves.board) << '\n';
    (void)client.send_line(othello::tools::nboard::quit_command());
    return legal ? 0 : 1;
}
