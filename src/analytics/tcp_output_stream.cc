/*
 *  Copyright (c) 2014 Codilime
 */

#include "analytics/tcp_output_stream.h"

#include <boost/asio/ip/tcp.hpp>

#include "io/event_manager.h"

using analytics::TCPOutputStream;

TCPOutputStream::TCPOutputStream(EventManager *evm,
  boost::asio:ip::tcp::endpoint endp)
: evm_(evm),
  endpoint_(endp),
  socket_(evm_->io_service()) {
    setup_connection_async();
}

TCPOutputStream::setup_connection_async() {
    socket_.async_connect(endpoint_, connect_handler);
}

TCPOutputStream::appendMessage(const pugi::xml_node &msg) {
    if (!socket_.open()) {
        setup_connection_async();
    } else {
        
    }
}

