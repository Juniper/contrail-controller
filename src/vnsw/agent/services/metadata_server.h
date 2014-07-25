/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_metadata_server_h_
#define vnsw_agent_metadata_server_h_

#include "http/http_server.h"
#include "services/metadata_server_session.h"

class MetadataServer : public HttpServer {
public:
    MetadataServer(EventManager *evm) : HttpServer(evm) {}
    virtual ~MetadataServer() {}

    virtual TcpSession *AllocSession(Socket *socket) {
        std::auto_ptr<MetadataServerSession>
            session(new MetadataServerSession(this, socket));
        boost::system::error_code ec = session->SetSocketOptions();
        if (ec) {
            return NULL;
        }
        return session.release();
    }

private:
    DISALLOW_COPY_AND_ASSIGN(MetadataServer);
};

#endif // vnsw_agent_metadata_server_h_
