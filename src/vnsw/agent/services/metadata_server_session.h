/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_metadata_server_session_h_
#define vnsw_agent_metadata_server_session_h_

#include "http/http_session.h"
#include "services/metadata_server.h"

class MetadataServerSession : public HttpSession {
public:
    static const int kMetadataSessionInstance = 0;

    MetadataServerSession(HttpServer *server, Socket *socket)
        : HttpSession(server, socket) {}
    virtual ~MetadataServerSession() {}

    // return a session instance of 1 to ensure that only one
    // session request is processed at any time
    virtual int GetSessionInstance() const { return kMetadataSessionInstance; }

private:
    DISALLOW_COPY_AND_ASSIGN(MetadataServerSession);
};

#endif // vnsw_agent_metadata_server_session_h_
