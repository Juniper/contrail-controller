/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#ifndef BFD_CLIENT_SESSION_H_
#define BFD_CLIENT_SESSION_H_

#include <set>
#include <string>
#include <boost/intrusive_ptr.hpp>

#include <bfd/bfd_common.h>
#include <bfd/bfd_server.h>

class HttpSession;

namespace BFD {

// Access guarded by BFDConfigServer::mutex_
class ClientSession {
 public:
    ClientSession(BFDServer* server, ClientId client_id);

    BFDSession* GetSession(const boost::asio::ip::address& ip);

    ClientId client_id_;
    BFDServer *server_;
    typedef std::set<boost::asio::ip::address> BFDSessions;
    BFDSessions bfd_sessions_;
    typedef std::set<boost::intrusive_ptr<HttpSession> > HttpSessionSet;
    HttpSessionSet http_sessions_;
    bool changed_;

    void Notify();

    void AddMonitoringHttpSession(HttpSession* session);

    ResultCode AddBFDConnection(const boost::asio::ip::address& remoteHost, const BFDSessionConfig& config);
    ResultCode DeleteBFDConnection(const boost::asio::ip::address& remoteHost);

    ~ClientSession();
};


void SendResponse(HttpSession *session, const std::string &msg, int status_code = 200);
void SendErrorResponse(HttpSession *session, const std::string &error_msg, int status_code = 500);

};  // namespace BFD

#endif /* BFD_CLIENT_SESSION_H_ */
