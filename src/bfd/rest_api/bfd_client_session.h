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

#include "bfd/bfd_common.h"
#include "io/tcp_session.h"

class HttpSession;

namespace BFD {

// RESTClientSession instances are used solely by RESTServer.
// Access is guarded by RESTServer::mutex_.
class RESTClientSession {
 public:
    RESTClientSession(Server* server, ClientId client_id);
    ~RESTClientSession();

    Session *GetSession(const boost::asio::ip::address& ip,
                        const SessionIndex &index = SessionIndex()) const;
    Session *GetSession(const SessionKey &key) const;
    void Notify();
    void AddMonitoringHttpSession(HttpSession* session);

    ResultCode AddBFDConnection(const SessionKey &key,
                                const SessionConfig &config);
    ResultCode DeleteBFDConnection(const SessionKey &key);

 private:
    typedef std::set<SessionKey> Sessions;
    typedef std::set<boost::intrusive_ptr<HttpSession> > HttpSessionSet;

    void OnHttpSessionEvent(HttpSession* session, enum TcpSession::Event event);

    ClientId client_id_;
    Server *server_;
    Sessions bfd_sessions_;
    HttpSessionSet http_sessions_;
    bool changed_;
};

}  // namespace BFD

#endif  // SRC_BFD_REST_API_BFD_CLIENT_SESSION_H_
