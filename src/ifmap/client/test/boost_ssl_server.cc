/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "boost_ssl_server.h"

BoostSslServer::BoostSslServer(boost::asio::io_service& io_service,
                               unsigned short port, string password)
    : io_service_(io_service),
      acceptor_(io_service, boost::asio::ip::tcp::endpoint(
                                        boost::asio::ip::tcp::v4(), port)),
      context_(io_service, boost::asio::ssl::context::sslv3_server),
      password_(password) {

    boost::system::error_code ec;
    context_.set_verify_mode(boost::asio::ssl::context::verify_none, ec);
    context_.use_certificate_chain_file(
        "controller/src/ifmap/client/test/server.pem");
    context_.use_private_key_file("controller/src/ifmap/client/test/server.pem",
                                  boost::asio::ssl::context::pem);
    context_.set_options(boost::asio::ssl::context::default_workarounds);
    context_.set_password_callback(boost::bind(&BoostSslServer::get_password,
                                               this));
}

void BoostSslServer::Start() {
    SslServerSession* new_session = new SslServerSession(io_service_, context_);
    acceptor_.async_accept(new_session->socket(),
        boost::bind(&BoostSslServer::HandleAccept, this, new_session,
                    boost::asio::placeholders::error));
    io_service_.run();
}

void BoostSslServer::HandleAccept(SslServerSession* new_session,
                                  const boost::system::error_code& error) {
    if (!error) {
        new_session->Start();
        new_session = new SslServerSession(io_service_, context_);
        acceptor_.async_accept(new_session->socket(),
            boost::bind(&BoostSslServer::HandleAccept, this, new_session,
            boost::asio::placeholders::error));
    } else {
        delete new_session;
    }
}

