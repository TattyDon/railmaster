#include "railmaster/net/socket.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace railmaster::net {

namespace {

// Messages: a 4-byte little-endian length (of the type byte and the body),
// the type, the body. A length past this is a damaged or foreign stream.
constexpr std::uint32_t kMaxMessage = 512u << 20;

#ifdef _WIN32
using Native = SOCKET;
constexpr Native kNone = INVALID_SOCKET;
void close_native(Native s) { closesocket(s); }
int poll_native(pollfd* fds, int n, int timeout_ms) { return WSAPoll(fds, static_cast<ULONG>(n), timeout_ms); }
bool would_block() { return WSAGetLastError() == WSAEWOULDBLOCK; }
void startup() {
    static const bool started = [] {
        WSADATA d;
        return WSAStartup(MAKEWORD(2, 2), &d) == 0;
    }();
    (void)started;
}
void make_non_blocking(Native s) {
    u_long on = 1;
    ioctlsocket(s, FIONBIO, &on);
}
long send_native(Native s, const char* p, std::size_t n) { return ::send(s, p, static_cast<int>(n), 0); }
long recv_native(Native s, char* p, std::size_t n) { return ::recv(s, p, static_cast<int>(n), 0); }
bool connecting() { return WSAGetLastError() == WSAEWOULDBLOCK; }
#else
using Native = int;
constexpr Native kNone = -1;
void close_native(Native s) { ::close(s); }
int poll_native(pollfd* fds, int n, int timeout_ms) { return ::poll(fds, static_cast<nfds_t>(n), timeout_ms); }
bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }
void startup() {}
void make_non_blocking(Native s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
#ifdef MSG_NOSIGNAL
constexpr int kNoSignal = MSG_NOSIGNAL; // a peer gone does not kill the game (Linux)
#else
constexpr int kNoSignal = 0;
#endif
long send_native(Native s, const char* p, std::size_t n) { return static_cast<long>(::send(s, p, n, kNoSignal)); }
long recv_native(Native s, char* p, std::size_t n) { return static_cast<long>(::recv(s, p, n, 0)); }
bool connecting() { return errno == EINPROGRESS; }
#endif

Native native(std::intptr_t s) { return static_cast<Native>(s); }

void tune(Native s) {
    make_non_blocking(s);
    int on = 1;
    // Commands are small and wanted at once: no batching.
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&on), sizeof on);
#ifdef SO_NOSIGPIPE
    setsockopt(s, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof on); // as MSG_NOSIGNAL (macOS)
#endif
}

// Wait up to `timeout_ms` for `s` to be ready for `events`.
bool wait(Native s, short events, int timeout_ms) {
    pollfd p{};
    p.fd = s;
    p.events = events;
    return poll_native(&p, 1, timeout_ms) > 0;
}

int elapsed_ms(std::chrono::steady_clock::time_point since) {
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - since).count());
}

} // namespace

Connection::Connection(std::intptr_t socket) : socket_(socket) {}

Connection::~Connection() { close(); }

Connection::Connection(Connection&& o) noexcept
    : socket_(o.socket_), in_(std::move(o.in_)), out_(std::move(o.out_)), out_at_(o.out_at_) {
    o.socket_ = -1;
}

Connection& Connection::operator=(Connection&& o) noexcept {
    if (this != &o) {
        close();
        socket_ = o.socket_;
        in_ = std::move(o.in_);
        out_ = std::move(o.out_);
        out_at_ = o.out_at_;
        o.socket_ = -1;
    }
    return *this;
}

void Connection::close() {
    if (socket_ >= 0) close_native(native(socket_));
    socket_ = -1;
}

Connection Connection::connect(const std::string& host, std::uint16_t port, int timeout_ms, std::string& error) {
    startup();
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* found = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &found) != 0 || !found) {
        error = "cannot find " + host;
        return {};
    }
    error = "nobody is hosting at " + host + " port " + std::to_string(port);
    Connection out;
    for (addrinfo* a = found; a && !out.open(); a = a->ai_next) {
        const Native s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
        if (s == kNone) continue;
        tune(s);
        const int r = ::connect(s, a->ai_addr, static_cast<socklen_t>(a->ai_addrlen));
        bool ok = r == 0;
        if (!ok && connecting()) {
            // Under way: wait for it, then ask how it went.
            if (wait(s, POLLOUT, timeout_ms)) {
                int problem = 0;
                socklen_t len = sizeof problem;
                getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&problem), &len);
                ok = problem == 0;
            } else {
                error = "no answer from " + host;
            }
        }
        if (ok) out = Connection(static_cast<std::intptr_t>(s));
        else close_native(s);
    }
    freeaddrinfo(found);
    if (out.open()) error.clear();
    return out;
}

void Connection::send(std::uint8_t type, std::string_view body) {
    if (!open()) return;
    const auto n = static_cast<std::uint32_t>(body.size() + 1);
    for (int i = 0; i < 4; ++i) out_.push_back(static_cast<char>((n >> (8 * i)) & 0xFF));
    out_.push_back(static_cast<char>(type));
    out_.append(body);
    flush(0);
}

bool Connection::flush(int timeout_ms) {
    const auto start = std::chrono::steady_clock::now();
    while (open() && out_at_ < out_.size()) {
        const long sent = send_native(native(socket_), out_.data() + out_at_, out_.size() - out_at_);
        if (sent > 0) {
            out_at_ += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent < 0 && would_block()) {
            const int left = timeout_ms - elapsed_ms(start);
            if (left <= 0 || !wait(native(socket_), POLLOUT, left)) break;
            continue;
        }
        close(); // the other end has gone
    }
    if (out_at_ == out_.size()) {
        out_.clear();
        out_at_ = 0;
    } else if (out_at_ > (1u << 20)) {
        out_.erase(0, out_at_); // keep the queue from growing without end
        out_at_ = 0;
    }
    return open();
}

bool Connection::read_some(int timeout_ms) {
    if (!open()) return false;
    if (timeout_ms > 0 && !wait(native(socket_), POLLIN, timeout_ms)) return false;
    char buf[65536];
    bool got = false;
    for (;;) {
        const long n = recv_native(native(socket_), buf, sizeof buf);
        if (n > 0) {
            in_.append(buf, static_cast<std::size_t>(n));
            got = true;
            continue;
        }
        if (n < 0 && would_block()) break;
        close(); // closed by the other end, or failed
        break;
    }
    return got;
}

std::optional<Message> Connection::take() {
    if (in_.size() < 4) return std::nullopt;
    std::uint32_t n = 0;
    for (int i = 0; i < 4; ++i) n |= std::uint32_t{static_cast<unsigned char>(in_[static_cast<std::size_t>(i)])} << (8 * i);
    if (n == 0 || n > kMaxMessage) {
        close(); // not our game talking
        in_.clear();
        return std::nullopt;
    }
    if (in_.size() < 4 + std::size_t{n}) return std::nullopt;
    Message m;
    m.type = static_cast<std::uint8_t>(in_[4]);
    m.body = in_.substr(5, n - 1);
    in_.erase(0, 4 + std::size_t{n});
    return m;
}

std::optional<Message> Connection::receive(int timeout_ms) {
    if (auto m = take()) return m;
    const auto start = std::chrono::steady_clock::now();
    read_some(0);
    while (true) {
        if (auto m = take()) return m;
        const int left = timeout_ms - elapsed_ms(start);
        if (!open() || left <= 0) return std::nullopt;
        read_some(left);
    }
}

Listener::~Listener() {
    if (socket_ >= 0) close_native(native(socket_));
}

Listener::Listener(Listener&& o) noexcept : socket_(o.socket_), port_(o.port_) { o.socket_ = -1; }

Listener& Listener::operator=(Listener&& o) noexcept {
    if (this != &o) {
        if (socket_ >= 0) close_native(native(socket_));
        socket_ = o.socket_;
        port_ = o.port_;
        o.socket_ = -1;
    }
    return *this;
}

Listener Listener::listen(std::uint16_t port, std::string& error) {
    startup();
    Listener out;
    const Native s = ::socket(AF_INET, SOCK_STREAM, 0);
    if (s == kNone) {
        error = "cannot open a network socket";
        return out;
    }
    int on = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&on), sizeof on);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) != 0 || ::listen(s, 8) != 0) {
        close_native(s);
        error = "cannot host on port " + std::to_string(port) + " (is another game hosting there?)";
        return out;
    }
    socklen_t len = sizeof addr;
    getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len);
    make_non_blocking(s);
    out.socket_ = static_cast<std::intptr_t>(s);
    out.port_ = ntohs(addr.sin_port);
    return out;
}

std::optional<Connection> Listener::accept() {
    if (!open()) return std::nullopt;
    const Native s = ::accept(native(socket_), nullptr, nullptr);
    if (s == kNone) return std::nullopt;
    tune(s);
    return Connection(static_cast<std::intptr_t>(s));
}

std::vector<std::string> local_addresses() {
    std::vector<std::string> out;
#ifndef _WIN32
    ifaddrs* list = nullptr;
    if (getifaddrs(&list) != 0) return out;
    for (ifaddrs* a = list; a; a = a->ifa_next) {
        if (!a->ifa_addr || a->ifa_addr->sa_family != AF_INET) continue;
        char text[INET_ADDRSTRLEN] = {};
        const auto* in = reinterpret_cast<const sockaddr_in*>(a->ifa_addr);
        if (!inet_ntop(AF_INET, &in->sin_addr, text, sizeof text)) continue;
        const std::string s = text;
        if (s.rfind("127.", 0) == 0) continue;
        out.push_back(s);
    }
    freeifaddrs(list);
#endif
    return out;
}

} // namespace railmaster::net
