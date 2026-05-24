#include "protocols/nboard/process_client.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <poll.h>
#include <string>
#include <system_error>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace othello::tools::nboard {

namespace {

void close_fd(int& fd) noexcept {
    if (fd >= 0) {
        close(fd);
        fd = -1;
    }
}

} // namespace

ProcessClient::ProcessClient(ProcessClient&& other) noexcept
    : child_stdin_{other.child_stdin_},
      child_stdout_{other.child_stdout_},
      child_pid_{other.child_pid_},
      verbose_{other.verbose_},
      read_buffer_{std::move(other.read_buffer_)} {
    other.child_stdin_ = -1;
    other.child_stdout_ = -1;
    other.child_pid_ = -1;
}

ProcessClient& ProcessClient::operator=(ProcessClient&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    stop();
    child_stdin_ = other.child_stdin_;
    child_stdout_ = other.child_stdout_;
    child_pid_ = other.child_pid_;
    verbose_ = other.verbose_;
    read_buffer_ = std::move(other.read_buffer_);
    other.child_stdin_ = -1;
    other.child_stdout_ = -1;
    other.child_pid_ = -1;
    return *this;
}

ProcessClient::~ProcessClient() {
    stop();
}

ProcessStartResult ProcessClient::start(const std::vector<std::string>& command, bool verbose) {
    return start(ProcessStartOptions{.command = command, .cwd = std::nullopt, .verbose = verbose});
}

ProcessStartResult ProcessClient::start(const ProcessStartOptions& options) {
    if (options.command.empty()) {
        return {.ok = false, .error = "empty engine command"};
    }
    if (options.cwd.has_value()) {
        std::error_code error;
        const bool is_directory = std::filesystem::is_directory(*options.cwd, error);
        if (error) {
            return {.ok = false, .error = error.message()};
        }
        if (!is_directory) {
            return {.ok = false, .error = "cwd is not a directory"};
        }
    }
    stop();
    verbose_ = options.verbose;

    int stdin_pipe[2]{};
    int stdout_pipe[2]{};
    if (pipe(stdin_pipe) != 0 || pipe(stdout_pipe) != 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return {.ok = false, .error = std::strerror(errno)};
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);
        return {.ok = false, .error = std::strerror(errno)};
    }

    if (pid == 0) {
        if (options.cwd.has_value() && chdir(options.cwd->c_str()) != 0) {
            _exit(126);
        }
        dup2(stdin_pipe[0], STDIN_FILENO);
        dup2(stdout_pipe[1], STDOUT_FILENO);
        close(stdin_pipe[0]);
        close(stdin_pipe[1]);
        close(stdout_pipe[0]);
        close(stdout_pipe[1]);

        std::vector<char*> argv;
        argv.reserve(options.command.size() + 1);
        for (const std::string& part : options.command) {
            argv.push_back(const_cast<char*>(part.c_str()));
        }
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(stdin_pipe[0]);
    close(stdout_pipe[1]);
    child_stdin_ = stdin_pipe[1];
    child_stdout_ = stdout_pipe[0];
    child_pid_ = pid;
    return {.ok = true};
}

bool ProcessClient::running() const noexcept {
    return child_pid_ > 0;
}

bool ProcessClient::send_line(std::string_view line) {
    if (child_stdin_ < 0) {
        return false;
    }
    if (verbose_) {
        std::cerr << "send: " << line << '\n';
    }
    std::string payload{line};
    payload.push_back('\n');
    const char* data = payload.data();
    std::size_t remaining = payload.size();
    while (remaining > 0) {
        const ssize_t written = write(child_stdin_, data, remaining);
        if (written <= 0) {
            return false;
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
    return true;
}

std::optional<std::string> ProcessClient::read_line(std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        const std::size_t newline = read_buffer_.find('\n');
        if (newline != std::string::npos) {
            std::string line = read_buffer_.substr(0, newline);
            read_buffer_.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (verbose_) {
                std::cerr << "recv: " << line << '\n';
            }
            return line;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return std::nullopt;
        }
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        pollfd descriptor{.fd = child_stdout_, .events = POLLIN, .revents = 0};
        const int ready = poll(&descriptor, 1, static_cast<int>(remaining.count()));
        if (ready <= 0) {
            return std::nullopt;
        }
        char buffer[256];
        const ssize_t read_count = read(child_stdout_, buffer, sizeof(buffer));
        if (read_count <= 0) {
            return std::nullopt;
        }
        read_buffer_.append(buffer, static_cast<std::size_t>(read_count));
    }
}

void ProcessClient::stop() noexcept {
    close_fd(child_stdin_);
    close_fd(child_stdout_);
    if (child_pid_ > 0) {
        int status = 0;
        if (waitpid(child_pid_, &status, WNOHANG) == 0) {
            kill(child_pid_, SIGTERM);
            waitpid(child_pid_, &status, 0);
        }
        child_pid_ = -1;
    }
}

} // namespace othello::tools::nboard
