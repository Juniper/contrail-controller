/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __HTTP_SERVER_H__
#define __HTTP_SERVER_H__

#include <map>
#include <string>

#include <boost/function.hpp>

#include "io/ssl_server.h"
#include "base/util.h"

#define HTTP_WILDCARD_ENTRY "_match_any_"

class EventManager;
class HttpRequest;
class HttpSession;
struct SslConfig {
    bool ssl_enabled;
    std::string path_to_server_cert;
    std::string path_to_server_priv_key;
    std::string path_to_ca_cert;
};

class HttpServer : public SslServer {
public:
    typedef boost::function<void(HttpSession *session, const HttpRequest *)>
	HttpHandlerFn;
    HttpServer(EventManager *evm, const struct SslConfig *config);
    explicit HttpServer(EventManager *evm);
    virtual ~HttpServer();

    virtual TcpSession *AllocSession(Socket *socket);
    virtual SslSession *AllocSession(SslSocket *socket);
    virtual bool AcceptSession(TcpSession *session);

    void RegisterHandler(const std::string &path, HttpHandlerFn handler);
    HttpHandlerFn GetHandler(const std::string &path);
    void Shutdown();

private:
    typedef std::map<std::string, HttpHandlerFn> HandlerTrie;
    HandlerTrie http_handlers_;
    DISALLOW_COPY_AND_ASSIGN(HttpServer);
};

#endif // __HTTP_SERVER_H__
