/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "boost_ssl_client.h"
#include <ctime>

using namespace std;
using boost::system::error_code;

BoostSslClient::BoostSslClient(boost::asio::io_service &io_service)
    : io_service_(io_service), resolver_(io_service),
      context_(io_service, boost::asio::ssl::context::sslv3_client),
      socket_(io_service, context_) {
    error_code ec;
    context_.set_verify_mode(boost::asio::ssl::context::verify_none, ec);
}

void BoostSslClient::Start(const std::string &user, const std::string& passwd,
        const std::string &host, const std::string &port) {
    username_ = user;
    password_ = passwd;
    host_ = host;
    port_ = port;
    std::srand(std::time(0));
    DoResolve();
}

void BoostSslClient::DoResolve() {
    boost::asio::ip::tcp::resolver::query conn_query = 
        boost::asio::ip::tcp::resolver::query(host_, port_);
    resolver_.async_resolve(conn_query,
                            boost::bind(&BoostSslClient::ReadResolveResponse,
                                        this, boost::asio::placeholders::error,
                                        boost::asio::placeholders::iterator));
}

void BoostSslClient::ReadResolveResponse(const boost::system::error_code& error,
          boost::asio::ip::tcp::resolver::iterator endpoint_iterator) {
    if (!error) {
        endpoint_ = *endpoint_iterator;

        socket_.lowest_layer().async_connect(endpoint_,
            boost::bind(&BoostSslClient::ProcConnectResponse, this,
                        boost::asio::placeholders::error));
    } else {
        assert(0);
    }
}

void BoostSslClient::ProcConnectResponse(
        const boost::system::error_code& error) {
    if (!error) {
        socket_.async_handshake(boost::asio::ssl::stream_base::client,
            boost::bind(&BoostSslClient::ProcHandshakeResponse, this,
                        boost::asio::placeholders::error));
    } else {
        assert(0);
    }
}

void BoostSslClient::ProcHandshakeResponse(
        const boost::system::error_code& error) {
    if (!error) {
        SendPollRequest();
    } else {
        assert(0);
    }
}

void BoostSslClient::SendPollRequest() {
    std::ostringstream poll_msg;
    BuildPollRequest(poll_msg);
    boost::asio::async_write(socket_,
        boost::asio::buffer(poll_msg.str().c_str(), poll_msg.str().length()),
        boost::bind(&BoostSslClient::ProcPollWrite, this,
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
}

void BoostSslClient::ProcPollWrite(const boost::system::error_code& error,
                                   size_t header_length) {
    if (!error) {
        boost::asio::async_read_until(socket_, reply_, "\r\n\r\n",
            boost::bind(&BoostSslClient::ProcResponse, this,
                        boost::asio::placeholders::error, 
                        boost::asio::placeholders::bytes_transferred));
    } else {
        assert(0);
    }
}

void BoostSslClient::BuildPollRequest(std::ostringstream& poll_msg) {
    std::ostringstream body;
    body << "abcdefghijklmnopqrstuvwxyz";

    poll_msg << "POST / HTTP/1.1\r\n";
    poll_msg << "Content-length: " <<  body.str().length() << "\r\n\r\n";
    poll_msg << body.str();
}

void BoostSslClient::ProcResponse(const boost::system::error_code& error,
                                  size_t header_length) {
    if (!error) {
        reply_ss_ << &reply_;
        std::string reply_str = reply_ss_.str();
        if (reply_str.find("401 Unauthorized") != string::npos) {
            assert(0);
        }
        string srch1("Content-Length: ");
        size_t pos1 = reply_str.find(srch1);
        if (pos1 == string::npos) {
            assert(0);
        }
        string srch2("\r\n");
        size_t pos2 = reply_str.find(srch2, pos1 + srch1.length());
        if (pos2 == string::npos) {
            assert(0);
        }
        size_t start_pos = (pos1 + srch1.length());
        size_t lenlen = pos2 - pos1 - srch1.length();
        string content_len_str = reply_str.substr(start_pos, lenlen);
        int content_len = atoi(content_len_str.c_str());
        if ((header_length + content_len) == reply_str.length()) {
            ProcFullResponse(error, header_length);
        } else {
            size_t bytes_to_read = content_len - 
                                   (reply_str.length() - header_length);
            boost::asio::async_read(socket_, reply_,
                boost::asio::transfer_exactly(bytes_to_read),
                boost::bind(&BoostSslClient::ProcFullResponse, this,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred));
        }

    } else {
        assert(0);
    }
}

void BoostSslClient::ProcFullResponse(const boost::system::error_code& error,
                                      size_t header_length) {
    reply_ss_ << &reply_;
    std::string reply_str = reply_ss_.str();
    cout << "CLI: Client received full message" << endl;
    SendPollRequest();
    int sleeptime;
    if (ShouldSleep(&sleeptime)) {
        usleep(sleeptime);
    }
    reply_ss_.str(std::string());
    reply_ss_.clear();
}

bool BoostSslClient::ShouldSleep(int *sleeptime) {
    *sleeptime = std::rand() % 200;
    return true;
}

