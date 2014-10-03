/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_metadata_client_session_h_
#define vnsw_agent_metadata_client_session_h_

#include "http/http_session.h"
#include "services/metadata_client.h"

class MetadataClientSession : public HttpClientSession {
public:
    static const int kMetadataSessionInstance = 0;

    MetadataClientSession(HttpClient *client, Socket *socket)
        : HttpClientSession(client, socket) {}
    virtual ~MetadataClientSession() {}

    // return a session instance of 0 to ensure that only one
    // session request is processed at any time
    virtual int GetSessionInstance() const { return kMetadataSessionInstance; }

private:
    DISALLOW_COPY_AND_ASSIGN(MetadataClientSession);
};

#endif // vnsw_agent_metadata_client_session_h_
