/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <ctime>

#include <boost/asio/buffer.hpp>
#include <boost/asio/detail/socket_option.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/placeholders.hpp>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/assign/list_of.hpp>
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

typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> SslStream;

/*
// Generate a new ssl private key :
$openssl genrsa -out privkey.pem 1024

// Create a certificate signing request using your new key
$openssl req -new -key privkey.pem -out certreq.csr

// Self-sign your CSR with your own private key:
$openssl x509 -req -days 3650 -in certreq.csr -signkey privkey.pem -out newcert.pem

// Install the signed certificate and private key for use by an ssl server
// This allows you to use a single file for certificate and private key
$( openssl x509 -in newcert.pem; cat privkey.pem ) > server.pem
*/

class SslServerSession {
public:
    SslServerSession(boost::asio::io_service& io_service,
                     boost::asio::ssl::context& context)
    : socket_(io_service, context), packets_sent_(0) {
    }

    SslStream::lowest_layer_type& socket() {
        return socket_.lowest_layer();
    }

    void Start() {
        std::srand(std::time(0));
        socket_.async_handshake(boost::asio::ssl::stream_base::server,
            boost::bind(&SslServerSession::HandleHandshake, this,
                        boost::asio::placeholders::error));
    }

    void HandleHandshake(const boost::system::error_code& error) {
        if (!error) {
            boost::asio::async_read_until(socket_, reply_, "\r\n\r\n",
                boost::bind(&SslServerSession::HandleRead, this,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred));
        } else {
            delete this;
        }
    }

    void HandleRead(const boost::system::error_code& error,
                    size_t bytes_transferred) {
        if (!error) {
            reply_ss_.str(std::string());
            reply_ss_.clear();
            reply_ss_ << &reply_;
            string reply_str = reply_ss_.str();

            ostringstream poll_msg;
            BuildPollResponse(poll_msg);
            boost::asio::async_write(socket_,
                boost::asio::buffer(poll_msg.str().c_str(),
                                    poll_msg.str().length()),
                boost::bind(&SslServerSession::HandleWrite, this,
                boost::asio::placeholders::error));
        } else {
            delete this;
        }
    }

    void HandleWrite(const boost::system::error_code& error) {
        if (!error) {
            boost::asio::async_read_until(socket_, reply_, "\r\n\r\n",
                boost::bind(&SslServerSession::HandleRead, this,
                            boost::asio::placeholders::error,
                            boost::asio::placeholders::bytes_transferred));
        } else {
            delete this;
        }
    }

    void BuildPollResponse(ostringstream& poll_msg) {
        ostringstream body;
        int num_times = GetRandomNumber();
        body << "numTimes " << num_times << "::";
        for (int i = 0; i < num_times; ++i) {
            body << "abcdefghijklmnopqrstuvwxyz:";
        }
        body << ":packetNumber " << ++packets_sent_;

        poll_msg << "POST / HTTP/1.1\r\n";
        poll_msg << "Content-Length: " <<  body.str().length() << "\r\n\r\n";
        poll_msg << body.str();
        cout << "SRV: Server sending pkt#" << packets_sent_ << " with "
             << body.str().length() << " bytes" << endl;
    }

    int GetRandomNumber() {
        return std::rand() % 125;
    }

private:
    SslStream socket_;
    boost::asio::streambuf reply_;
    ostringstream reply_ss_;
    int packets_sent_;
};

class BoostSslServer {
public:
    BoostSslServer(boost::asio::io_service &io_service, unsigned short port,
                   string password);
    string get_password() { return password_; }
    void Start();
    void HandleAccept(SslServerSession* new_session,
                      const boost::system::error_code& error);

private:
    boost::asio::io_service& io_service_;
    boost::asio::ip::tcp::acceptor acceptor_;
    boost::asio::ssl::context context_;
    string password_;
};

