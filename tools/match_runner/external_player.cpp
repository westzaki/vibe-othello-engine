#include "external_player.hpp"

#include "protocols/nboard/game_codec.hpp"
#include "protocols/nboard/parser.hpp"
#include "protocols/nboard/protocol.hpp"

#include <chrono>
#include <string_view>

namespace othello::match_runner {
namespace nboard = tools::nboard;
namespace {

[[nodiscard]] bool send_or_error(nboard::ProcessClient& client, std::string_view line,
                                 std::string& error) {
    if (client.send_line(line)) {
        return true;
    }
    error = "failed to send NBoard command";
    return false;
}

[[nodiscard]] bool wait_for_pong(nboard::ProcessClient& client, std::chrono::milliseconds timeout,
                                 std::string& error) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto line = client.read_line(std::chrono::milliseconds{200});
        if (!line.has_value()) {
            continue;
        }
        if (nboard::is_pong_line(*line, "1")) {
            return true;
        }
    }
    error = "NBoard engine did not respond to ping";
    return false;
}

[[nodiscard]] std::optional<nboard::NBoardMove>
wait_for_move(nboard::ProcessClient& client, std::chrono::milliseconds timeout,
              std::string& error) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto line = client.read_line(std::chrono::milliseconds{200});
        if (!line.has_value()) {
            continue;
        }
        const auto move = nboard::parse_go_move_line(*line);
        if (move.has_value()) {
            return move;
        }
    }
    error = "NBoard engine did not return a move";
    return std::nullopt;
}

} // namespace

bool ExternalNBoardPlayer::start(const ExternalEngineConfig& config, bool verbose) {
    stop();
    config_ = &config;
    error_.clear();

    const nboard::ProcessStartResult started = client_.start(nboard::ProcessStartOptions{
        .command = config.command,
        .cwd = config.cwd,
        .verbose = verbose,
    });
    if (!started.ok) {
        error_ = "failed to start NBoard engine: " + started.error;
        return false;
    }
    if (!send_or_error(client_, nboard::nboard_command(2), error_)) {
        return false;
    }
    return true;
}

ExternalMoveResult ExternalNBoardPlayer::choose_move(const Board& board,
                                                     const std::vector<std::string>& moves,
                                                     std::chrono::milliseconds timeout) {
    const auto started = std::chrono::steady_clock::now();
    if (config_ == nullptr) {
        return ExternalMoveResult{.error = "NBoard engine is not started"};
    }

    std::string error;
    if (!send_or_error(client_, nboard::set_game_command(moves), error) ||
        !send_or_error(client_, nboard::set_depth_command(config_->depth), error) ||
        !send_or_error(client_, nboard::ping_command(1), error) ||
        !wait_for_pong(client_, timeout, error) ||
        !send_or_error(client_, nboard::go_command(), error)) {
        const auto finished = std::chrono::steady_clock::now();
        return ExternalMoveResult{
            .elapsed_ms = std::chrono::duration<double, std::milli>{finished - started}.count(),
            .error = error,
        };
    }

    const std::optional<nboard::NBoardMove> response = wait_for_move(client_, timeout, error);
    const auto finished = std::chrono::steady_clock::now();
    const double elapsed_ms =
        std::chrono::duration<double, std::milli>{finished - started}.count();
    if (!response.has_value()) {
        return ExternalMoveResult{.elapsed_ms = elapsed_ms, .error = error};
    }
    if (!nboard::is_legal_response(board, *response)) {
        return ExternalMoveResult{.elapsed_ms = elapsed_ms,
                                  .error = "NBoard engine returned an illegal move"};
    }
    if (response->pass) {
        return ExternalMoveResult{.elapsed_ms = elapsed_ms,
                                  .error = "NBoard engine returned pass on a legal-move turn"};
    }
    return ExternalMoveResult{.move = response->square, .elapsed_ms = elapsed_ms};
}

void ExternalNBoardPlayer::stop() noexcept {
    if (client_.running()) {
        (void)client_.send_line(nboard::quit_command());
    }
    client_.stop();
    config_ = nullptr;
}

} // namespace othello::match_runner
