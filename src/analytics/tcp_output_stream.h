#ifndef SRC_TCP_OUTPUT_STREAM_
#define SRC_TCP_OUTPUT_STREAM_
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/basic_socket.hpp>
#include "analytics/stream_handler.h"

class EventManager;

namespace analytics {

class TCPOutputStream : public OutputStreamHandler,
                        public boost::enable_shared_from_this<TCPOutputStream> {
 public:
    // EventManager for asio
    TCPOutputStream(EventManager *, boost::asio::ip::tcp::endpoint);
    bool appendMessage(const pugi::xml_node &);

 private:
    void setup_connection_async();
    void connect_handler(const boost::system::error_code &);
    void async_write_handler(const boost::system::error_code &, size_t);

    EventManager *evm_;

    boost::asio::ip::tcp::endpoint endpoint_;
    boost::asio::ip::tcp::socket socket_;
};

}

#endif
