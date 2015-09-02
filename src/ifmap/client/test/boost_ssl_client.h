/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_boost_ssl_client_h
#define ctrlplane_boost_ssl_client_h

#include <boost/asio/buffer.hpp>
#include <boost/asio/detail/socket_option.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include "boost/generator_iterator.hpp"
#include "boost/random.hpp"
#include <boost/system/error_code.hpp>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated"
#endif
#include <boost/asio/ssl.hpp>
#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <boost/asio/streambuf.hpp>
#include <boost/function.hpp>

using namespace std;
using boost::system::error_code;

typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> SslStream;

class BoostSslClient {
public:
    BoostSslClient(boost::asio::io_service &io_service);
    void Start(const std::string &user, const std::string& passwd,
               const std::string &host, const std::string &port);
    void DoResolve();
    void ReadResolveResponse(const boost::system::error_code& error,
        boost::asio::ip::tcp::resolver::iterator endpoint_iterator);
    void ProcConnectResponse(const boost::system::error_code& error);
    void ProcHandshakeResponse(const boost::system::error_code& error);
    void SendPollRequest();
    void ProcPollWrite(const boost::system::error_code& error,
                       size_t header_length);
    void ProcResponse(const boost::system::error_code& error,
                      size_t header_length);
    void ProcFullResponse(const boost::system::error_code& error,
                          size_t header_length);
    bool ShouldSleep(int *sleeptime);

private:
    void BuildPollRequest(std::ostringstream& poll_msg);

    boost::asio::ip::tcp::resolver resolver_;
    boost::asio::ssl::context context_;
    SslStream socket_;
    std::string username_;
    std::string password_;
    std::string host_;
    std::string port_;
    boost::asio::ip::tcp::endpoint endpoint_;
    boost::asio::streambuf reply_;
    std::ostringstream reply_ss_;
};

#endif // ctrlplane_boost_ssl_client_h
