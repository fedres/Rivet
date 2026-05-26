// runtime/daemon/daemon.cpp — Compiler daemon implementation
//
// Phase 4: full Unix domain socket IPC with length-prefixed JSON wire format.
//
// Wire format (both directions):
//   [uint32_t little-endian byte count][UTF-8 JSON payload]
//
// Client auto-starts the daemon if the socket is absent/refused.
#include "daemon.hpp"
#include "../../platform/interface/process.hpp"
#include "../../platform/interface/env.hpp"
#include "../../platform/interface/terminal.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <format>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#  include <cerrno>
#  include <csignal>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <sys/un.h>
#  include <unistd.h>
#endif

namespace rivet::daemon {

// ─── socket_path() ───────────────────────────────────────────────────────────

std::string socket_path(const Path& rivet_home) {
#if defined(_WIN32)
    return R"(\\.\pipe\rivet-daemon)";
#else
    return (rivet_home / "daemon.sock").string();
#endif
}

// ─── Minimal hand-rolled JSON helpers ────────────────────────────────────────

namespace {

// Escape a string for embedding in JSON.
std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 4);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

// Serialize DaemonRequest → JSON string.
std::string serialize_request(const DaemonRequest& req) {
    // JSON fields:
    //   kind, request_id, task_id, cmd_exe, cmd_args (array), working_dir
    std::string j = "{";
    j += std::format("\"kind\":{},", static_cast<int>(req.kind));
    j += std::format("\"request_id\":{},", req.request_id);
    j += std::format("\"task_id\":{},", req.task.id);
    j += std::format("\"task_name\":\"{}\",", json_escape(req.task.name));
    j += std::format("\"task_kind\":{},", static_cast<int>(req.task.kind));

    // cmd_exe and cmd_args from CompileCommand (if present).
    if (req.task.command) {
        j += std::format("\"cmd_exe\":\"{}\",", json_escape(req.task.command->executable));
        j += "\"cmd_args\":[";
        bool first = true;
        for (const auto& arg : req.task.command->args) {
            if (!first) j += ',';
            j += '"';
            j += json_escape(arg);
            j += '"';
            first = false;
        }
        j += "],";
        j += std::format("\"working_dir\":\"{}\"",
                         json_escape(req.task.command->working_dir.string()));
    } else if (!req.task.raw_command.empty()) {
        j += std::format("\"cmd_exe\":\"{}\",",
                         json_escape(req.task.raw_command[0]));
        j += "\"cmd_args\":[";
        bool first = true;
        for (std::size_t i = 1; i < req.task.raw_command.size(); ++i) {
            if (!first) j += ',';
            j += '"';
            j += json_escape(req.task.raw_command[i]);
            j += '"';
            first = false;
        }
        j += "],";
        j += "\"working_dir\":\"\"";
    } else {
        j += "\"cmd_exe\":\"\",\"cmd_args\":[],\"working_dir\":\"\"";
    }

    j += '}';
    return j;
}

// Serialize DaemonResponse → JSON string.
std::string serialize_response(const DaemonResponse& resp) {
    std::string j = "{";
    j += std::format("\"request_id\":{},", resp.request_id);
    j += std::format("\"success\":{},", resp.success ? "true" : "false");
    j += std::format("\"exit_code\":{},", resp.exit_code);
    j += std::format("\"stdout\":\"{}\"," , json_escape(resp.stdout_out));
    j += std::format("\"stderr\":\"{}\"," , json_escape(resp.stderr_out));
    j += std::format("\"error_message\":\"{}\"," , json_escape(resp.error_message));
    j += std::format("\"jobs_completed\":{},", resp.metrics.jobs_completed);
    j += std::format("\"cache_hits\":{},", resp.metrics.cache_hits);
    j += std::format("\"queue_depth\":{},", resp.metrics.queue_depth);
    j += std::format("\"worker_count\":{},", resp.metrics.worker_count);
    j += std::format("\"uptime_sec\":{}", resp.metrics.uptime_sec);
    j += '}';
    return j;
}

// Minimal JSON value extractor — finds the first occurrence of "key": value
// and returns the raw value string.  Handles strings, numbers, booleans, and
// simple arrays of strings.  Not a full parser — sufficient for the fixed
// schema we control on both ends.

// Find the raw value for a JSON key (returns empty string if not found).
// Handles string values (returns without quotes/escapes), number values, bool.
std::string json_get_string(const std::string& json, const std::string& key) {
    // Look for "key":"...
    std::string search = "\"" + key + "\":\"";
    auto pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    std::string val;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            char c = json[pos + 1];
            switch (c) {
                case '"':  val += '"';  pos += 2; break;
                case '\\': val += '\\'; pos += 2; break;
                case 'n':  val += '\n'; pos += 2; break;
                case 'r':  val += '\r'; pos += 2; break;
                case 't':  val += '\t'; pos += 2; break;
                default:   val += c;    pos += 2; break;
            }
        } else {
            val += json[pos++];
        }
    }
    return val;
}

uint64_t json_get_uint64(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return 0;
    pos += search.size();
    // skip whitespace
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos >= json.size()) return 0;
    uint64_t val = 0;
    while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
        val = val * 10 + static_cast<uint64_t>(json[pos] - '0');
        ++pos;
    }
    return val;
}

int json_get_int(const std::string& json, const std::string& key) {
    return static_cast<int>(json_get_uint64(json, key));
}

bool json_get_bool(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    auto pos = json.find(search);
    if (pos == std::string::npos) return false;
    pos += search.size();
    while (pos < json.size() && json[pos] == ' ') ++pos;
    return (pos + 4 <= json.size() && json.substr(pos, 4) == "true");
}

// Parse a JSON array of strings for key.
std::vector<std::string> json_get_string_array(const std::string& json,
                                                const std::string& key) {
    std::vector<std::string> result;
    std::string search = "\"" + key + "\":[";
    auto pos = json.find(search);
    if (pos == std::string::npos) return result;
    pos += search.size();
    // Walk through the array; a ']' that is NOT inside a quoted string ends it.
    bool in_string = false;
    std::string current;
    while (pos < json.size()) {
        char ch = json[pos];
        if (!in_string) {
            if (ch == ']') break;
            if (ch == '"') {
                in_string = true;
                current.clear();
                ++pos;
                continue;
            }
            ++pos;
        } else {
            // Inside a quoted string.
            if (ch == '\\' && pos + 1 < json.size()) {
                char esc = json[pos + 1];
                switch (esc) {
                    case '"':  current += '"';  pos += 2; break;
                    case '\\': current += '\\'; pos += 2; break;
                    case 'n':  current += '\n'; pos += 2; break;
                    case 'r':  current += '\r'; pos += 2; break;
                    case 't':  current += '\t'; pos += 2; break;
                    default:   current += esc;  pos += 2; break;
                }
            } else if (ch == '"') {
                // End of this string element.
                result.push_back(std::move(current));
                current.clear();
                in_string = false;
                ++pos;
            } else {
                current += ch;
                ++pos;
            }
        }
    }
    return result;
}

// Deserialize JSON → DaemonResponse.
DaemonResponse parse_response(const std::string& json) {
    DaemonResponse resp;
    resp.request_id       = json_get_uint64(json, "request_id");
    resp.success          = json_get_bool(json,   "success");
    resp.exit_code        = json_get_int(json,    "exit_code");
    resp.stdout_out       = json_get_string(json, "stdout");
    resp.stderr_out       = json_get_string(json, "stderr");
    resp.error_message    = json_get_string(json, "error_message");
    resp.metrics.jobs_completed = json_get_uint64(json, "jobs_completed");
    resp.metrics.cache_hits     = json_get_uint64(json, "cache_hits");
    resp.metrics.queue_depth    = static_cast<uint32_t>(json_get_uint64(json, "queue_depth"));
    resp.metrics.worker_count   = static_cast<uint32_t>(json_get_uint64(json, "worker_count"));
    resp.metrics.uptime_sec     = json_get_uint64(json, "uptime_sec");
    return resp;
}

// Deserialize JSON → DaemonRequest.
DaemonRequest parse_request(const std::string& json) {
    DaemonRequest req;
    req.kind                         = static_cast<RequestKind>(json_get_int(json, "kind"));
    req.request_id                   = json_get_uint64(json, "request_id");
    req.task.id                      = static_cast<build::TaskId>(json_get_uint64(json, "task_id"));
    req.task.name                    = json_get_string(json, "task_name");
    req.task.kind                    = static_cast<build::TaskKind>(json_get_int(json, "task_kind"));

    std::string exe = json_get_string(json, "cmd_exe");
    std::string wd  = json_get_string(json, "working_dir");
    auto args       = json_get_string_array(json, "cmd_args");

    if (!exe.empty()) {
        build::CompileCommand cmd;
        cmd.executable   = exe;
        cmd.args         = std::move(args);
        cmd.working_dir  = Path{wd};
        req.task.command = std::move(cmd);
    }

    return req;
}

#if !defined(_WIN32)

// ─── Low-level POSIX socket I/O ───────────────────────────────────────────────

// Write exactly `n` bytes to `fd`, returning false on error.
bool write_all(int fd, const void* buf, std::size_t n) {
    const char* p = static_cast<const char*>(buf);
    while (n > 0) {
        ssize_t written = ::write(fd, p, n);
        if (written <= 0) return false;
        p += written;
        n -= static_cast<std::size_t>(written);
    }
    return true;
}

// Read exactly `n` bytes from `fd`, returning false on error or EOF.
bool read_all(int fd, void* buf, std::size_t n) {
    char* p = static_cast<char*>(buf);
    while (n > 0) {
        ssize_t got = ::read(fd, p, n);
        if (got <= 0) return false;
        p += got;
        n -= static_cast<std::size_t>(got);
    }
    return true;
}

// Write a length-prefixed message to fd.
bool send_message(int fd, const std::string& payload) {
    uint32_t len = static_cast<uint32_t>(payload.size());
    // Little-endian length prefix.
    uint8_t prefix[4] = {
        static_cast<uint8_t>(len & 0xFF),
        static_cast<uint8_t>((len >> 8) & 0xFF),
        static_cast<uint8_t>((len >> 16) & 0xFF),
        static_cast<uint8_t>((len >> 24) & 0xFF),
    };
    if (!write_all(fd, prefix, 4)) return false;
    if (!write_all(fd, payload.data(), payload.size())) return false;
    return true;
}

// Read a length-prefixed message from fd.
// Returns empty string on error.
std::string recv_message(int fd) {
    uint8_t prefix[4];
    if (!read_all(fd, prefix, 4)) return {};
    uint32_t len = static_cast<uint32_t>(prefix[0])
                 | (static_cast<uint32_t>(prefix[1]) << 8)
                 | (static_cast<uint32_t>(prefix[2]) << 16)
                 | (static_cast<uint32_t>(prefix[3]) << 24);
    if (len == 0) return {};
    // Guard against runaway allocations (64 MiB max).
    if (len > (64u * 1024u * 1024u)) return {};
    std::string payload(len, '\0');
    if (!read_all(fd, payload.data(), len)) return {};
    return payload;
}

#endif // !_WIN32

} // anonymous namespace

// ─── DaemonClient ────────────────────────────────────────────────────────────

DaemonClient::DaemonClient(Path rivet_home) : rivet_home_(std::move(rivet_home)) {}

DaemonClient::~DaemonClient() {
    if (socket_fd_ >= 0) {
#if !defined(_WIN32)
        ::close(socket_fd_);
#endif
        socket_fd_ = -1;
    }
}

Result<void> DaemonClient::connect() {
#if defined(_WIN32)
    return make_error<void>("daemon IPC not yet implemented on Windows");
#else
    std::string path = socket_path(rivet_home_);

    auto try_connect = [&]() -> bool {
        int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return false;

        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

        if (::connect(fd, reinterpret_cast<struct sockaddr*>(&addr),
                      sizeof(addr)) == 0) {
            socket_fd_ = fd;
            connected_ = true;
            return true;
        }
        ::close(fd);
        return false;
    };

    // First attempt.
    if (try_connect()) return {};

    // Connection failed — daemon may not be running.
    if (errno == ECONNREFUSED || errno == ENOENT) {
        auto start_r = start_daemon_process();
        if (!start_r) return propagate<void>(start_r);
    }

    // Retry up to 3 times with 200 ms delay.
    for (int attempt = 0; attempt < 3; ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (try_connect()) return {};
    }

    return make_error<void>(std::format(
        "connect: could not reach daemon at {} after retries", path));
#endif
}

Result<void> DaemonClient::send(const DaemonRequest& req) {
#if defined(_WIN32)
    return make_error<void>("daemon IPC not yet implemented on Windows");
#else
    if (socket_fd_ < 0)
        return make_error<void>("send: not connected");

    std::string payload = serialize_request(req);
    if (!send_message(socket_fd_, payload))
        return make_error<void>(
            std::string("send: write failed: ") + std::strerror(errno), errno);
    return {};
#endif
}

Result<DaemonResponse> DaemonClient::recv() {
#if defined(_WIN32)
    return make_error<DaemonResponse>("daemon IPC not yet implemented on Windows");
#else
    if (socket_fd_ < 0)
        return make_error<DaemonResponse>("recv: not connected");

    std::string payload = recv_message(socket_fd_);
    if (payload.empty())
        return make_error<DaemonResponse>(
            std::string("recv: read failed or connection closed"));

    return parse_response(payload);
#endif
}

Result<DaemonResponse> DaemonClient::compile(const build::TaskNode& task) {
#if defined(_WIN32)
    return make_error<DaemonResponse>("daemon IPC not yet implemented on Windows");
#else
    if (!connected_) {
        auto c = connect();
        if (!c) return propagate<DaemonResponse>(c);
    }

    static uint64_t next_id = 1;
    DaemonRequest req;
    req.kind       = RequestKind::Compile;
    req.request_id = next_id++;
    req.task       = task;

    auto s = send(req);
    if (!s) return propagate<DaemonResponse>(s);
    return recv();
#endif
}

Result<DaemonMetrics> DaemonClient::status() {
#if defined(_WIN32)
    return make_error<DaemonMetrics>("daemon IPC not yet implemented on Windows");
#else
    if (!connected_) {
        auto c = connect();
        if (!c) return propagate<DaemonMetrics>(c);
    }

    static uint64_t next_id = 1;
    DaemonRequest req;
    req.kind       = RequestKind::Status;
    req.request_id = next_id++;

    auto s = send(req);
    if (!s) return propagate<DaemonMetrics>(s);

    auto r = recv();
    if (!r) return propagate<DaemonMetrics>(r);
    return r->metrics;
#endif
}

Result<void> DaemonClient::shutdown() {
#if defined(_WIN32)
    return make_error<void>("daemon IPC not yet implemented on Windows");
#else
    if (!connected_) {
        auto c = connect();
        if (!c) return propagate<void>(c);
    }

    static uint64_t next_id = 1;
    DaemonRequest req;
    req.kind       = RequestKind::Shutdown;
    req.request_id = next_id++;

    auto s = send(req);
    if (!s) return propagate<void>(s);

    // Best-effort receive (daemon may close before reply).
    recv();

    if (socket_fd_ >= 0) {
        ::close(socket_fd_);
        socket_fd_ = -1;
    }
    connected_ = false;
    return {};
#endif
}

Result<void> DaemonClient::start_daemon_process() {
    auto self = rivet::process::self_exe();
    if (!self) return propagate<void>(self);

    rivet::process::SpawnOptions opts;
    // argv[0] must be the executable path; subsequent entries are arguments.
    opts.args        = {self->string(), "daemon", "start", "--background"};
    opts.inherit_env = true;
    // Don't capture output — this is a detached background process.
    opts.capture_stdout = false;
    opts.capture_stderr = false;

    auto child = rivet::process::spawn(std::move(opts));
    if (!child) return propagate<void>(child);

    // Do not wait — daemon runs independently.
    return {};
}

// ─── daemon_main() ───────────────────────────────────────────────────────────

#if !defined(_WIN32)

namespace {

// Global flag set by SIGTERM / SIGINT handler.
volatile sig_atomic_t g_shutdown_requested = 0;

extern "C" void daemon_signal_handler(int /*sig*/) {
    g_shutdown_requested = 1;
}

// Execute a compile task and return a populated DaemonResponse.
DaemonResponse execute_compile_task(const build::TaskNode& task,
                                    uint64_t& jobs_completed,
                                    uint64_t  uptime_sec) {
    DaemonResponse resp;
    resp.request_id = 0; // will be overwritten by caller

    if (task.kind == build::TaskKind::Phony ||
        (!task.command && task.raw_command.empty())) {
        resp.success  = true;
        resp.exit_code = 0;
        ++jobs_completed;
        resp.metrics.jobs_completed = jobs_completed;
        resp.metrics.uptime_sec     = uptime_sec;
        return resp;
    }

    std::vector<std::string> argv;
    if (task.command) {
        argv.push_back(task.command->executable);
        for (const auto& a : task.command->args) argv.push_back(a);
    } else {
        argv = task.raw_command;
    }

    if (argv.empty()) {
        resp.success = true;
        ++jobs_completed;
        resp.metrics.jobs_completed = jobs_completed;
        resp.metrics.uptime_sec     = uptime_sec;
        return resp;
    }

    rivet::process::SpawnOptions opts;
    opts.args            = argv;
    opts.capture_stdout  = true;
    opts.capture_stderr  = true;
    opts.inherit_env     = false;
    if (task.command && !task.command->working_dir.empty())
        opts.working_dir = task.command->working_dir;

    auto child_r = rivet::process::spawn(std::move(opts));
    if (!child_r) {
        resp.success       = false;
        resp.exit_code     = -1;
        resp.error_message = std::format("spawn failed: {}", child_r.error().message);
        resp.metrics.jobs_completed = jobs_completed;
        resp.metrics.uptime_sec     = uptime_sec;
        return resp;
    }

    auto& child  = *child_r;
    auto wait_r  = child.wait();
    resp.exit_code  = wait_r ? *wait_r : -1;
    resp.success    = (resp.exit_code == 0);
    resp.stdout_out = child.stdout_output();
    resp.stderr_out = child.stderr_output();

    ++jobs_completed;
    resp.metrics.jobs_completed = jobs_completed;
    resp.metrics.uptime_sec     = uptime_sec;
    return resp;
}

// Handle a single client connection; returns true to continue, false to stop.
bool handle_connection(int conn_fd,
                       uint64_t& jobs_completed,
                       std::chrono::steady_clock::time_point start_time) {
    std::string payload = recv_message(conn_fd);
    if (payload.empty()) return true; // client disconnected — keep running

    DaemonRequest req = parse_request(payload);

    auto uptime_sec = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time).count());

    DaemonResponse resp;
    resp.request_id = req.request_id;
    bool keep_running = true;

    switch (req.kind) {
        case RequestKind::Ping:
            resp.success              = true;
            resp.metrics.uptime_sec   = uptime_sec;
            resp.metrics.jobs_completed = jobs_completed;
            break;

        case RequestKind::Status:
            resp.success              = true;
            resp.metrics.uptime_sec   = uptime_sec;
            resp.metrics.jobs_completed = jobs_completed;
            resp.metrics.queue_depth  = 0;
            resp.metrics.worker_count = 1;
            break;

        case RequestKind::Shutdown:
            resp.success = true;
            keep_running = false;
            break;

        case RequestKind::Compile:
            resp = execute_compile_task(req.task, jobs_completed, uptime_sec);
            resp.request_id = req.request_id;
            break;
    }

    std::string out = serialize_response(resp);
    send_message(conn_fd, out);
    return keep_running;
}

} // anonymous namespace

#endif // !_WIN32

int daemon_main(const Path& rivet_home) {
#if defined(_WIN32)
    std::cerr << "[rivet daemon] Windows named-pipe support not yet implemented\n";
    return 1;
#else
    std::string path = socket_path(rivet_home);
    std::cerr << "[rivet daemon] starting — socket: " << path << "\n";

    // Set up signal handlers.
    struct sigaction sa{};
    sa.sa_handler = daemon_signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    ::sigaction(SIGTERM, &sa, nullptr);
    ::sigaction(SIGINT,  &sa, nullptr);
    // Ignore SIGPIPE so a crashed client doesn't kill the daemon.
    ::signal(SIGPIPE, SIG_IGN);

    // Remove stale socket file if present.
    ::unlink(path.c_str());

    // Create listening socket.
    int listen_fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        std::cerr << "[rivet daemon] socket: " << std::strerror(errno) << "\n";
        return 1;
    }

    int reuse = 1;
    ::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(listen_fd, reinterpret_cast<struct sockaddr*>(&addr),
               sizeof(addr)) != 0) {
        std::cerr << "[rivet daemon] bind: " << std::strerror(errno) << "\n";
        ::close(listen_fd);
        return 1;
    }

    if (::listen(listen_fd, 10) != 0) {
        std::cerr << "[rivet daemon] listen: " << std::strerror(errno) << "\n";
        ::close(listen_fd);
        ::unlink(path.c_str());
        return 1;
    }

    std::cerr << "[rivet daemon] listening\n";

    auto start_time         = std::chrono::steady_clock::now();
    auto last_activity      = start_time;
    uint64_t jobs_completed = 0;

    // Accept loop.
    while (!g_shutdown_requested) {
        // Check idle timeout.
        auto now = std::chrono::steady_clock::now();
        auto idle = std::chrono::duration_cast<std::chrono::minutes>(
            now - last_activity);
        if (idle >= kDefaultIdleTimeout) {
            std::cerr << "[rivet daemon] idle timeout — shutting down\n";
            break;
        }

        // Use select() with a 1-second timeout so we can check signals and
        // the idle clock without blocking forever in accept().
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(listen_fd, &read_fds);
        struct timeval tv{1, 0};  // 1 second
        int sel = ::select(listen_fd + 1, &read_fds, nullptr, nullptr, &tv);
        if (sel < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[rivet daemon] select: " << std::strerror(errno) << "\n";
            break;
        }
        if (sel == 0) continue;  // timeout — loop to check idle/signals

        int conn_fd = ::accept(listen_fd, nullptr, nullptr);
        if (conn_fd < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[rivet daemon] accept: " << std::strerror(errno) << "\n";
            continue;
        }

        last_activity = std::chrono::steady_clock::now();

        bool keep_running = handle_connection(conn_fd, jobs_completed, start_time);
        ::close(conn_fd);

        if (!keep_running) {
            std::cerr << "[rivet daemon] shutdown requested by client\n";
            break;
        }
    }

    ::close(listen_fd);
    ::unlink(path.c_str());
    std::cerr << "[rivet daemon] stopped\n";
    return 0;
#endif
}

} // namespace rivet::daemon
