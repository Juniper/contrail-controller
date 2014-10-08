#ifndef SRC_TCP_OUTPUT_STREAM_
#define SRC_TCP_OUTPUT_STREAM_
#include <boost/asio/ip/tcp.hpp>
#include "analytics/stream_handler.h"

class EventManager;

namespace analytics {

class TCPOutputStream : public OutputStreamManager::OutputStreamHandler {
 public:
    // EventManager for asio
    TCPOutputStream(EventManager *, boost::asio::ip::tcp::endpoint);
    bool appendMessage(const pugi::xml_node &);

 private:
    void setup_connection_async();
    void connect_handler(const boost::system::error_code&);

    boost::asio::ip::tcp::endpoint endpoint_;
    boost::asio::ip::tcp::socket socket_;

    EventManager *evm_;
};

}

#endif
