/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#ifndef BFD_CONFIG_SERVER_H_
#define BFD_CONFIG_SERVER_H_

#include <map>

#include <bfd/http_server/bfd_json_config.h>

#include <boost/asio/ip/address.hpp>
#include <http/http_request.h>
#include <http/http_session.h>
#include <bfd/bfd_common.h>
#include <bfd/http_server/bfd_client_session.h>
namespace BFD {

class BFDConfigServer {
    BFDServer *bfd_server_;
    tbb::mutex mutex_;

    typedef std::map<HttpSession *, ClientId> HttpSessionMap;
    HttpSessionMap http_session_map_;
    typedef std::map<ClientId, ClientSession *> ClientMap;
    ClientMap client_sessions_;

    ClientId UniqClientId();

    ClientSession* GetClientSession(ClientId client_id, HttpSession* session);

    void CreateClientSession(HttpSession* session, const HttpRequest* request);
    void DeleteClientSession(ClientId client_id, HttpSession* session, const HttpRequest* request);
    void CreateBFDConnection(ClientId client_id, HttpSession* session, const HttpRequest* request);
    void GetBFDConnection(ClientId client_id, const boost::asio::ip::address& ip,
            HttpSession* session, const HttpRequest* request);
    void DeleteBFDConnection(ClientId client_id, const boost::asio::ip::address& ip,
            HttpSession* session, const HttpRequest* request);
    void MonitorClientSession(ClientId client_id, HttpSession* session, const HttpRequest* request);

 public:
    explicit BFDConfigServer(BFDServer* bfd_server);
    ~BFDConfigServer();

    void OnHttpSessionEvent(HttpSession* session, enum TcpSession::Event event);
    void HandleRequest(HttpSession* session, const HttpRequest* request);
};

}  // namespace BFD


#endif /* BFD_CONFIG_SERVER_H_ */
