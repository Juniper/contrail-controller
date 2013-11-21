/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "http/http_server.h"

#include "http/http_session.h"
#include "io/event_manager.h"

using namespace std;

HttpServer::HttpServer(EventManager *evm) : TcpServer(evm) {
    //ctor
}

HttpServer::~HttpServer() {
    //dtor
}

void HttpServer::Shutdown() {
    http_handlers_.clear();
    TcpServer::Shutdown();
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

