#pragma once

// TCP connections carrying whole messages (a length, a type byte, the
// bytes), for multiplayer (session.hpp). Sockets are non-blocking: sending
// queues what cannot go at once, and receiving returns only whole messages.
// POSIX sockets, and Winsock on Windows.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace railmaster::net {

struct Message {
    std::uint8_t type = 0;
    std::string body;
};

class Connection {
public:
    Connection() = default;
    explicit Connection(std::intptr_t socket);
    ~Connection();
    Connection(Connection&& o) noexcept;
    Connection& operator=(Connection&& o) noexcept;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Connect to `host` (a name or address) on `port`, waiting up to
    // `timeout_ms`. A closed connection, with `error` set, if it cannot.
    static Connection connect(const std::string& host, std::uint16_t port, int timeout_ms, std::string& error);

    bool open() const { return socket_ >= 0; }
    // Queue a message and send what can go now.
    void send(std::uint8_t type, std::string_view body);
    // Send what is queued, waiting up to `timeout_ms` for it all to go.
    // False once the connection has closed.
    bool flush(int timeout_ms = 0);
    // The next whole message, waiting up to `timeout_ms` for one. Nothing
    // when none has come (or the connection has closed: see open()).
    std::optional<Message> receive(int timeout_ms = 0);
    // Bytes queued and not yet sent.
    std::size_t backlog() const { return out_.size() - out_at_; }
    void close();

private:
    bool read_some(int timeout_ms);
    std::optional<Message> take();
    std::intptr_t socket_ = -1;
    std::string in_, out_;
    std::size_t out_at_ = 0;
};

class Listener {
public:
    Listener() = default;
    ~Listener();
    Listener(Listener&& o) noexcept;
    Listener& operator=(Listener&& o) noexcept;
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    // Listen on `port` (0: any free one) on every address of this computer.
    // A closed listener, with `error` set, if it cannot.
    static Listener listen(std::uint16_t port, std::string& error);
    bool open() const { return socket_ >= 0; }
    std::uint16_t port() const { return port_; }
    // A connection waiting to be let in, if any.
    std::optional<Connection> accept();

private:
    std::intptr_t socket_ = -1;
    std::uint16_t port_ = 0;
};

// This computer's network addresses (IPv4, not the loopback), to tell
// other players where to join.
std::vector<std::string> local_addresses();

} // namespace railmaster::net
