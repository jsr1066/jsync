#include "peer.h"
#include "wire.h"

#include <cerrno>
#include <cstdlib>
#include <chrono>
#include <csignal>
#include <cstring>
#include <mutex>
#include <thread>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

extern char** environ;

namespace jsync_ns
{
    namespace
    {
        // Each side sends this first and skips anything before it from the
        // other side (e.g. shell startup noise). Also serves as a version check.
        constexpr char MAGIC[] = "JSYNC1\n";
        constexpr size_t MAGIC_LEN = sizeof(MAGIC) - 1;
        constexpr size_t MAX_NOISE = 64 * 1024;

        // ~/.local/state/jsync/jsync.log; spdlog creates missing directories.
        std::string server_log_path()
        {
            const char* home = std::getenv("HOME");
            if (home == nullptr || *home == '\0')
            {
                return {};
            }
            return std::string(home) + "/.local/state/jsync/jsync.log";
        }

        // Logs the error for the named command and wraps it for return.
        std::unexpected<Error> fail(const char* command, std::string message, int code)
        {
            spdlog::error("{}: {} (code {})", command, message, code);
            return std::unexpected(Error{std::move(message), code});
        }

        std::unexpected<Error> fail_errno(const char* command, const char* what, int code)
        {
            return fail(command, std::string(what) + ": " + std::strerror(code), code);
        }

        // A dead remote should surface as EPIPE from write(), not kill us.
        void ignore_sigpipe()
        {
            static std::once_flag once;
            std::call_once(once, [] { std::signal(SIGPIPE, SIG_IGN); });
        }

        bool make_pipe(int fds[2])
        {
            if (::pipe(fds) != 0) return false;
            for (int i = 0; i < 2; ++i)
            {
                int flags = ::fcntl(fds[i], F_GETFD);
                ::fcntl(fds[i], F_SETFD, flags | FD_CLOEXEC);
            }
            return true;
        }

        void close_fd(int& fd)
        {
            if (fd >= 0)
            {
                ::close(fd);
                fd = -1;
            }
        }

        std::string describe_status(int status)
        {
            if (WIFEXITED(status)) return "exited with status " + std::to_string(WEXITSTATUS(status));
            if (WIFSIGNALED(status)) return "killed by signal " + std::to_string(WTERMSIG(status));
            return "ended";
        }
    }

    Peer::Peer()
    {
        ignore_sigpipe();
    }

    Peer::~Peer()
    {
        close();
    }

    ExpectBool Peer::connect(const std::string& host)
    {
        return spawn({"ssh", "-T", host, "jsync", "--server"});
    }

    ExpectBool Peer::accept()
    {
        // stdout is the protocol stream, so log to a file instead, falling
        // back to stderr if the file can't be opened (e.g. no write access).
        const std::string log_path = server_log_path();
        std::string log_error;
        try
        {
            if (log_path.empty())
            {
                throw spdlog::spdlog_ex("HOME is not set");
            }
            spdlog::set_default_logger(spdlog::basic_logger_mt("jsync", log_path));
        }
        catch (const spdlog::spdlog_ex& ex)
        {
            log_error = ex.what();
            spdlog::set_default_logger(spdlog::stderr_color_mt("jsync"));
        }
        if (!log_error.empty())
        {
            spdlog::warn("cannot log to {}, using stderr: {}", log_path, log_error);
        }
        return attach(STDIN_FILENO, STDOUT_FILENO, false);
    }

    ExpectBool Peer::spawn(const std::vector<std::string>& argv)
    {
        if (is_connected())
        {
            return fail("spawn", "already connected", EISCONN);
        }
        if (argv.empty())
        {
            return fail("spawn", "empty command", EINVAL);
        }

        int to_child[2];
        int from_child[2];
        if (!make_pipe(to_child))
        {
            return fail_errno("spawn", "pipe", errno);
        }
        if (!make_pipe(from_child))
        {
            int err = errno;
            ::close(to_child[0]);
            ::close(to_child[1]);
            return fail_errno("spawn", "pipe", err);
        }

        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        // The pipe ends are close-on-exec; dup2 onto 0/1 clears that for the child's copies.
        posix_spawn_file_actions_adddup2(&actions, to_child[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, from_child[1], STDOUT_FILENO);

        std::vector<char*> args;
        for (const auto& a : argv)
        {
            args.push_back(const_cast<char*>(a.c_str()));
        }
        args.push_back(nullptr);

        pid_t pid;
        int rc = posix_spawnp(&pid, args[0], &actions, nullptr, args.data(), environ);
        posix_spawn_file_actions_destroy(&actions);

        ::close(to_child[0]);
        ::close(from_child[1]);
        if (rc != 0)
        {
            ::close(to_child[1]);
            ::close(from_child[0]);
            return fail_errno("spawn", argv[0].c_str(), rc);
        }

        spdlog::info("spawned {} (pid {})", argv[0], static_cast<int>(pid));
        child_ = pid;
        read_fd_ = from_child[0];
        write_fd_ = to_child[1];
        owned_ = true;

        auto result = handshake();
        if (!result)
        {
            close();
        }
        return result;
    }

    ExpectBool Peer::attach(int read_fd, int write_fd, bool owned)
    {
        if (is_connected())
        {
            return fail("attach", "already connected", EISCONN);
        }
        read_fd_ = read_fd;
        write_fd_ = write_fd;
        owned_ = owned;

        auto result = handshake();
        if (!result)
        {
            close();
        }
        return result;
    }

    ExpectBool Peer::handshake()
    {
        // If the remote is already gone this write fails; the read below then
        // reports the cause (EOF plus the child's exit status).
        send(MAGIC, MAGIC_LEN);

        size_t matched = 0;
        size_t noise = 0;
        while (matched < MAGIC_LEN)
        {
            char c;
            ssize_t n = ::read(read_fd_, &c, 1);
            if (n < 0 && errno == EINTR) continue;
            if (n < 0)
            {
                return fail_errno("handshake", "read", errno);
            }
            if (n == 0)
            {
                // Close our end so a child waiting on stdin can finish.
                close_fd(write_fd_);
                std::string how = reap();
                return fail("handshake",
                            "remote closed the connection" + (how.empty() ? "" : " (" + how + ")"),
                            ECONNRESET);
            }
            if (c == MAGIC[matched])
            {
                ++matched;
            }
            else
            {
                noise += matched + 1;
                // MAGIC has no repeated prefix, so only 'J' can restart a match.
                matched = (c == MAGIC[0]) ? 1 : 0;
                if (noise > MAX_NOISE)
                {
                    return fail("handshake", "no jsync handshake from remote", EPROTO);
                }
            }
        }
        if (noise > 0)
        {
            spdlog::warn("skipped {} bytes of noise before handshake", noise);
        }
        spdlog::info("handshake complete");
        return true;
    }

    ExpectBool Peer::send(const void* buf, size_t n)
    {
        const char* p = static_cast<const char*>(buf);
        while (n > 0)
        {
            ssize_t w = ::write(write_fd_, p, n);
            if (w < 0)
            {
                if (errno == EINTR) continue;
                return fail_errno("send", "write", errno);
            }
            p += w;
            n -= static_cast<size_t>(w);
        }
        return true;
    }

    ExpectBool Peer::receive(void* buf, size_t n)
    {
        char* p = static_cast<char*>(buf);
        while (n > 0)
        {
            ssize_t r = ::read(read_fd_, p, n);
            if (r < 0)
            {
                if (errno == EINTR) continue;
                return fail_errno("receive", "read", errno);
            }
            if (r == 0)
            {
                return fail("receive", "connection closed by remote", ECONNRESET);
            }
            p += r;
            n -= static_cast<size_t>(r);
        }
        return true;
    }

    bool Peer::is_connected() const
    {
        return read_fd_ >= 0 && write_fd_ >= 0;
    }

    std::string Peer::reap()
    {
        if (child_ <= 0) return {};

        int status = 0;
        bool killed = false;
        for (int waited_ms = 0;; waited_ms += 10)
        {
            pid_t r = ::waitpid(child_, &status, WNOHANG);
            if (r == child_) break;
            if (r < 0 && errno != EINTR)
            {
                child_ = -1;
                return {};
            }
            if (waited_ms >= 2000 && !killed)
            {
                spdlog::warn("child {} did not exit, killing it", static_cast<int>(child_));
                ::kill(child_, SIGKILL);
                killed = true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        child_ = -1;
        return describe_status(status);
    }

    void Peer::close()
    {
        if (owned_)
        {
            close_fd(write_fd_);   // gives the remote EOF
            close_fd(read_fd_);
        }
        else
        {
            write_fd_ = -1;
            read_fd_ = -1;
        }
        owned_ = false;

        std::string how = reap();
        if (!how.empty())
        {
            spdlog::info("child {}", how);
        }
    }

    namespace
    {
        constexpr size_t HEADER_LEN = 15;
    }

    ExpectBool read_command(Peer& peer, std::string& command, std::map<std::string, std::string>& kv, std::vector<uint8_t>& chunk)
    {
        static constexpr char NAME[] = "read_command";
        command.clear();
        kv.clear();
        chunk.clear();

        char hdr[HEADER_LEN];
        if (auto r = peer.receive(hdr, HEADER_LEN); !r)
        {
            return std::unexpected(r.error());
        }
        if (std::memcmp(hdr, "JS1.0", 5) != 0 || hdr[5] != FIELD_SEP || hdr[14] != FIELD_SEP)
        {
            return fail(NAME, "bad header", EPROTO);
        }

        size_t len = 0;
        for (int i = 6; i < 14; ++i)
        {
            if (hdr[i] < '0' || hdr[i] > '9')
            {
                return fail(NAME, "bad length in header", EPROTO);
            }
            len = len * 10 + static_cast<size_t>(hdr[i] - '0');
        }
        if (len > MAX_COMMAND_LEN)
        {
            return fail(NAME, "command too long", EMSGSIZE);
        }

        std::string data(len, '\0');
        if (len > 0)
        {
            if (auto r = peer.receive(data.data(), len); !r)
            {
                return std::unexpected(r.error());
            }
        }

        size_t pos = 0;
        while (pos < data.size())
        {
            size_t end = data.find(FIELD_SEP, pos);
            if (end == std::string::npos)
            {
                end = data.size();
            }
            std::string token = data.substr(pos, end - pos);
            pos = end + 1;
            if (token.empty() || token == "\n")
            {
                continue;
            }
            size_t eq = token.find('=');
            if (eq == std::string::npos || eq == 0)
            {
                return fail(NAME, "malformed key=value: " + token, EPROTO);
            }
            kv[token.substr(0, eq)] = token.substr(eq + 1);
        }

        auto cmd = kv.find("cmd");
        if (cmd == kv.end())
        {
            return fail(NAME, "missing cmd", EPROTO);
        }
        command = cmd->second;

        auto cs = kv.find("chunk_size");
        if (cs != kv.end())
        {
            const std::string& v = cs->second;
            char* endp = nullptr;
            errno = 0;
            unsigned long long n = std::strtoull(v.c_str(), &endp, 10);
            if (v.empty() || v[0] == '-' || *endp != '\0' || errno != 0)
            {
                return fail(NAME, "bad chunk_size: " + v, EPROTO);
            }
            if (n > MAX_CHUNK)
            {
                return fail(NAME, "chunk_size too large: " + v, EMSGSIZE);
            }
            chunk.resize(n);
            if (n > 0)
            {
                if (auto r = peer.receive(chunk.data(), n); !r)
                {
                    return std::unexpected(r.error());
                }
            }
        }
        return true;
    }
}
