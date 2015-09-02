/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_metadata_client_h_
#define vnsw_agent_metadata_client_h_

#include "http/client/http_client.h"
#include "services/metadata_client_session.h"

class MetadataClient : public HttpClient {
public:
    MetadataClient(EventManager *evm) : HttpClient(evm) {}
    virtual ~MetadataClient() {}

    virtual TcpSession *AllocSession(Socket *socket) {
        MetadataClientSession *session =
            new MetadataClientSession(this, socket);
        return session;
    }

private:
    DISALLOW_COPY_AND_ASSIGN(MetadataClient);
};

#endif // vnsw_agent_metadata_client_h_
