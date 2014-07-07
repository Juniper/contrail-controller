/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#ifndef SRC_BFD_REST_API_BFD_CLIENT_SESSION_H_
#define SRC_BFD_REST_API_BFD_CLIENT_SESSION_H_

#include "bfd/bfd_common.h"
#include "bfd/bfd_server.h"

#include <set>
#include <string>
#include <boost/intrusive_ptr.hpp>

class HttpSession;

namespace BFD {

// RESTClientSession instances are used solely by RESTServer.
// Access is guarded by RESTServer::mutex_.
class RESTClientSession {
 public:
    RESTClientSession(Server* server, ClientId client_id);
    ~RESTClientSession();

    Session* GetSession(const boost::asio::ip::address& ip);

    void Notify();
    void AddMonitoringHttpSession(HttpSession* session);

    ResultCode AddBFDConnection(const boost::asio::ip::address& remoteHost,
                            const SessionConfig& config);
    ResultCode DeleteBFDConnection(const boost::asio::ip::address& remoteHost);

 private:
    typedef std::set<boost::asio::ip::address> Sessions;
    typedef std::set<boost::intrusive_ptr<HttpSession> > HttpSessionSet;

    ClientId client_id_;
    Server *server_;
    Sessions bfd_sessions_;
    HttpSessionSet http_sessions_;
    bool changed_;
};

}  // namespace BFD

#endif  // SRC_BFD_REST_API_BFD_CLIENT_SESSION_H_
