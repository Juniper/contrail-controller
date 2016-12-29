/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "http/http_server.h"

#include "http/http_session.h"
#include "io/event_manager.h"

using namespace std;

HttpServer::HttpServer(EventManager *evm)
    : SslServer(evm, boost::asio::ssl::context::tlsv1_server, false, false) {
    //ctor
}

HttpServer::HttpServer(EventManager *evm, const struct SslConfig *config)
    : SslServer(evm, boost::asio::ssl::context::tlsv1_server, config->ssl_enabled, false) {
    //ctor
    if (config->ssl_enabled) {

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
        std::string ca_cert_filename = config->path_to_ca_cert;
        if (!ca_cert_filename.empty()) {

            // Verify peer has CA signed certificate
            ctx->set_verify_mode(boost::asio::ssl::verify_peer, ec);
            if (ec.value() != 0) {
                exit(EINVAL);
            }

            ctx->load_verify_file(config->path_to_ca_cert, ec);
            if (ec.value() != 0) {
                exit(EINVAL);
            }
        }

        // server certificate
        ctx->use_certificate_file(config->path_to_server_cert,
                                  boost::asio::ssl::context::pem, ec);
        if (ec.value() != 0) {
            exit(EINVAL);
        }

        // server private key
        ctx->use_private_key_file(config->path_to_server_priv_key,
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

TcpSession *HttpServer::AllocSession(Socket *socket) {
    HttpSession *session = new HttpSession(this, socket);
    boost::system::error_code ec = session->SetSocketOptions();
    if (ec) {
        delete session;
        return NULL;
    }
    return session;
}

bool HttpServer::AcceptSession(TcpSession *session) {
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

