#pragma once

#include "types.hpp"

#include "protocols/nboard/process_client.hpp"

#include <chrono>
#include <optional>
#include <othello/othello.hpp>
#include <string>
#include <vector>

namespace othello::match_runner {

struct ExternalMoveResult {
    std::optional<Square> move;
    double elapsed_ms = 0.0;
    std::string error;
};

class ExternalNBoardPlayer {
public:
    ExternalNBoardPlayer() = default;
    [[nodiscard]] bool start(const ExternalEngineConfig& config, bool verbose = false);
    [[nodiscard]] ExternalMoveResult choose_move(const Board& board,
                                                 const std::vector<std::string>& moves,
                                                 std::chrono::milliseconds timeout);
    void stop() noexcept;

    [[nodiscard]] const std::string& error() const noexcept { return error_; }

private:
    tools::nboard::ProcessClient client_;
    const ExternalEngineConfig* config_ = nullptr;
    std::string error_;
};

} // namespace othello::match_runner
