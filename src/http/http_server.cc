/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "http/http_server.h"

#include "http/http_session.h"
#include "io/event_manager.h"

using namespace std;

HttpServer::HttpServer(EventManager *evm, const SslConfig &config)
    : SslServer(evm, boost::asio::ssl::context::tlsv1_server, config.ssl_enabled, false) {
    //ctor
    if (config.ssl_enabled) {

        // Get SSL context from base class and update
        boost::asio::ssl::context *ctx = context();
        boost::system::error_code ec;

        // set mode
        ctx->set_options(boost::asio::ssl::context::default_workarounds |
                         boost::asio::ssl::context::no_sslv3 |
			 boost::asio::ssl::context::no_sslv2, ec);
        if (ec.value() != 0) {
            exit(EINVAL);
        }

        // CA certificate, used to verify if the peer certificate
        // is signed by a trusted CA
        std::string ca_cert_filename = config.ca_cert;
        if (!ca_cert_filename.empty()) {

            // Verify peer has CA signed certificate
            ctx->set_verify_mode(boost::asio::ssl::verify_peer |
		    boost::asio::ssl::verify_fail_if_no_peer_cert, ec);
            if (ec.value() != 0) {
                exit(EINVAL);
            }

            ctx->load_verify_file(ca_cert_filename, ec);
            if (ec.value() != 0) {
                exit(EINVAL);
            }
        }

        // server certificate
        ctx->use_certificate_file(config.certfile,
                                  boost::asio::ssl::context::pem, ec);
        if (ec.value() != 0) {
            exit(EINVAL);
        }

        // server private key
        ctx->use_private_key_file(config.keyfile,
                                  boost::asio::ssl::context::pem, ec);
        if (ec.value() != 0) {
            exit(EINVAL);
        }
    }
}

HttpServer::~HttpServer() {
    //dtor
}

void HttpServer::Shutdown() {
    http_handlers_.clear();
    TcpServer::Shutdown();
}

SslSession *HttpServer::AllocSession(SslSocket *socket) {
    SslSession *session = new HttpSession(this, socket);
    boost::system::error_code err;
    HttpSession *https = static_cast<HttpSession *>(session);
    https->SetSocketOptions();
    return session;
}

bool HttpServer::AcceptSession(TcpSession *session) {
    HttpSession *h_session = dynamic_cast<HttpSession *>(session);
    h_session->AcceptSession();
    return true;
}

bool HttpServer::AcceptSession(SslSession *session) {
    HttpSession *h_session = dynamic_cast<HttpSession *>(session);
    h_session->AcceptSession();
    return true;
}

void HttpServer::RegisterHandler(const string &path, HttpHandlerFn handler) {
    http_handlers_.insert(make_pair(path, handler));
}

HttpServer::HttpHandlerFn HttpServer::GetHandler(const string &path) {
    HandlerTrie::iterator iter = http_handlers_.find(path);
    if (iter == http_handlers_.end()) {
        // check if wildcard entry is present
        iter = http_handlers_.find(HTTP_WILDCARD_ENTRY);
        if (iter == http_handlers_.end()) {
    	    return NULL;
        }
    }
    return iter->second;
}

