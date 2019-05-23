#pragma once

#include "fsm.hpp"

#include <asio.hpp>

#include <optional>
#include <variant>
#include <system_error>
#include <functional>


class resolving : public state<asio::ip::tcp::endpoint> {
public:
    resolving(asio::io_service& io, const std::string& addr, const std::string& service, completion_handler cb) :
        state(io, std::move(cb)),
        resolver(io)
    {
        resolver.async_resolve(addr, service, track([this](const std::error_code& ec, asio::ip::tcp::resolver::iterator it) {
            ec ? complete(ec) : complete(*it);
        }));
    }

    virtual void cancel() override {
        resolver.cancel();
    }
private:
    asio::ip::tcp::resolver resolver;
};

class connecting : public state<std::reference_wrapper<asio::ip::tcp::socket>> {
public:
    connecting(asio::io_service& io, const asio::ip::tcp::endpoint& ep, completion_handler cb) :
        state(io, std::move(cb)),
        sock(io)
    {
        sock.async_connect(ep, track([this](const std::error_code& ec) {
            ec ? complete(ec) : complete(std::ref(sock));
        }));
    }


    virtual void cancel() override {
        sock.cancel();
    }
private:
    asio::ip::tcp::socket sock;
};

class online : public state<std::monostate> {
public:
    online(asio::io_service& io, asio::ip::tcp::socket& sock, completion_handler cb) :
        state(io, std::move(cb)),
        sock(std::move(sock)),
        timer(io),
        extend(false)
    {
        start_read_socket();
        start_wait_timer();
    }

    virtual void cancel() override {
        sock.cancel();
        timer.cancel();
    }

private:
    void start_read_socket() {
        using namespace std::literals;
        asio::async_read_until(sock, asio::dynamic_buffer(rx_buffer), "\n"sv, track([this] (const std::error_code& ec, size_t bytes_transferred) {
            if (ec) {
                return complete(ec);
            }

            log("got %s", rx_buffer);
            rx_buffer.clear();
            start_read_socket();
            extend = true;
            start_wait_timer();
        }));
    }

    void start_wait_timer() {
        timer.expires_from_now(std::chrono::seconds(10));
        timer.async_wait(track([this](const std::error_code& ec) {
            if (ec) {
                if (ec == asio::error::operation_aborted && extend) {
                    extend = false;
                    return;
                }

                return complete(ec);
            }

            return complete(make_error_code(std::errc::timed_out));
        }));
    }
private:
    asio::ip::tcp::socket   sock;
    asio::steady_timer      timer;
    bool                    extend;
    std::string             rx_buffer;

};

class client {
public:
    client(asio::io_service& io) : io(io) {}
    using completion_handler = std::function<void(const std::error_code& r)>;

    void async_wait(const std::string& addr, const std::string& service, completion_handler cb) {
        sess.emplace(std::make_unique<resolving>(io, addr, service, std::bind(&client::on_event<resolving>, this, std::placeholders::_1)), std::move(cb), io);
    }

private:
    struct context {
        context(completion_handler cb, asio::io_service& io) : io(io), cb(std::move(cb)) {}

        completion_handler  cb;
        asio::io_service&   io;
    };

    using transition_table = transitions<
        transition<resolving, std::error_code, completed>,
        transition<resolving, asio::ip::tcp::endpoint, connecting>,
        transition<connecting, std::error_code, completed>,
        transition<connecting, std::reference_wrapper<asio::ip::tcp::socket>, online>,
        transition<online, std::error_code, completed>,
        transition<online, std::monostate, completed>>;

    template<typename State>
    void on_event(const typename State::result& r) {
        std::visit([this](auto v) {
            transition_table t;
            t.make_transition<State>(v, sess->ctx);
        }, r);
    }

private:

    struct session {
        template<typename ...Args>
        session(std::unique_ptr<state_base> st, completion_handler cb, Args&& ...args) :
            st(std::move(st)),
            ctx(std::move(cb), std::forward<Args>(args)...)
        {}

        std::unique_ptr<state_base> st;
        context                     ctx;
    };

    std::optional<session>      sess;
    asio::io_service&           io;
};