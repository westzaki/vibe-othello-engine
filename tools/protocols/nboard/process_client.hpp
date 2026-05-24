#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace othello::tools::nboard {

struct ProcessStartResult {
    bool ok = false;
    std::string error;
};

struct ProcessStartOptions {
    std::vector<std::string> command;
    std::optional<std::string> cwd;
    bool verbose = false;
};

class ProcessClient {
public:
    ProcessClient() = default;
    ProcessClient(const ProcessClient&) = delete;
    ProcessClient& operator=(const ProcessClient&) = delete;
    ProcessClient(ProcessClient&& other) noexcept;
    ProcessClient& operator=(ProcessClient&& other) noexcept;
    ~ProcessClient();

    [[nodiscard]] ProcessStartResult start(const std::vector<std::string>& command,
                                           bool verbose);
    [[nodiscard]] ProcessStartResult start(const ProcessStartOptions& options);
    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] bool send_line(std::string_view line);
    [[nodiscard]] std::optional<std::string> read_line(std::chrono::milliseconds timeout);
    void stop() noexcept;

private:
    int child_stdin_ = -1;
    int child_stdout_ = -1;
    int child_pid_ = -1;
    bool verbose_ = false;
    std::string read_buffer_;
};

} // namespace othello::tools::nboard
