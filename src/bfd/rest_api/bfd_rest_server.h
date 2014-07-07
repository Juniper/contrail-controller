/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#ifndef BFD_CONFIG_SERVER_H_
#define BFD_CONFIG_SERVER_H_

#include <map>
#include <boost/asio/ip/address.hpp>
#include <boost/regex.hpp>

#include "http/http_request.h"
#include "http/http_session.h"
#include "bfd/bfd_common.h"
#include "bfd/rest_api/bfd_client_session.h"
#include "bfd/rest_api/bfd_json_config.h"

namespace BFD {

class RESTServer {
  public:
    explicit RESTServer(Server* bfd_server);
    ~RESTServer();

    void OnHttpSessionEvent(HttpSession* session, enum TcpSession::Event event);
    void HandleRequest(HttpSession* session, const HttpRequest* request);

 protected:
    typedef std::map<HttpSession *, ClientId> HttpSessionMap;
    typedef std::map<ClientId, RESTClientSession *> ClientMap;

    ClientId UniqClientId();

    RESTClientSession* GetClientSession(ClientId client_id, HttpSession* session);

    void CreateRESTClientSession(HttpSession* session,
        const HttpRequest* request);
    void DeleteRESTClientSession(ClientId client_id,
        HttpSession* session, const HttpRequest* request);
    void CreateBFDConnection(ClientId client_id,
        HttpSession* session, const HttpRequest* request);
    void GetBFDConnection(ClientId client_id,
        const boost::asio::ip::address& ip, HttpSession* session,
        const HttpRequest* request);
    void DeleteBFDConnection(ClientId client_id,
        const boost::asio::ip::address& ip,
        HttpSession* session, const HttpRequest* request);
    void MonitorRESTClientSession(ClientId client_id, HttpSession* session,
        const HttpRequest* request);

    // REST handlers
    struct RESTData {
        const boost::smatch* match;
        enum http_method method;
        HttpSession* session;
        const HttpRequest* request;
    };

    class HandlerSpecifier {
     public:
        typedef void (RESTServer::*HandlerFunc)(const struct RESTData&);

        HandlerSpecifier(const boost::regex& request_regex,
                         enum http_method method,
                         HandlerFunc handler_func)
            : request_regex(request_regex),
              method(method),
              handler_func(handler_func) {}

        boost::regex request_regex;
        enum http_method method;
        HandlerFunc handler_func;
    };

    static const std::vector<HandlerSpecifier> RESTHandlers_;

    void SessionHandler(const struct RESTData&);
    void ClientHandler(const struct RESTData&);
    void ClientIPConnectionHandler(const struct RESTData&);
    void ClientIPAddressHandlerGet(const struct RESTData&);
    void ClientIPAddressHandlerDelete(const struct RESTData&);
    void ClientMonitorHandler(const struct RESTData&);

    HttpSessionMap http_session_map_;
    ClientMap client_sessions_;   
    Server *bfd_server_;
    tbb::mutex mutex_;
};

}  // namespace BFD

#endif  // SRC_BFD_CONFIG_SERVER_H_
