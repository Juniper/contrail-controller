/*
 *  Copyright (c) 2014 Codilime
 */

#include "analytics/tcp_output_stream.h"

#include <boost/bind.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/streambuf.hpp>
#include <boost/asio/write.hpp>
#include <boost/asio/placeholders.hpp>

#include "io/event_manager.h"

using analytics::TCPOutputStream;

TCPOutputStream::TCPOutputStream(EventManager *evm,
  boost::asio::ip::tcp::endpoint endp)
: evm_(evm),
  endpoint_(endp),
  socket_(*evm_->io_service()) {
    setup_connection_async();
}

void TCPOutputStream::setup_connection_async() {
    socket_.async_connect(endpoint_,
	boost::bind(&TCPOutputStream::connect_handler, this, _1));
}

bool TCPOutputStream::appendMessage(const pugi::xml_node &msg) {
    if (!socket_.is_open()) {
        setup_connection_async();
    } else {
        boost::asio::streambuf buf;
        std::ostream ost(&buf);
        msg.print(ost, "",
            pugi::format_raw |
            pugi::format_no_declaration |
            pugi::format_no_escapes);
        boost::asio::async_write(socket_, buf,
            boost::bind(&TCPOutputStream::async_write_handler,
                shared_from_this(),
                boost::asio::placeholders::error,
                boost::asio::placeholders::bytes_transferred));
    }
    return true;
}

void TCPOutputStream::connect_handler(const boost::system::error_code &err) {
    if (err)
        setup_connection_async();
}

void TCPOutputStream::async_write_handler(const boost::system::error_code &err,
         size_t bytes_written) {
    // free the buffers, etc
}
