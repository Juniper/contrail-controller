/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include <boost/bind.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/foreach.hpp>
#include <boost/tokenizer.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/thread/thread.hpp>
#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>

#include "base/logging.h"
#include "http/http_request.h"
#include "http/http_session.h"
#include "http/http_server.h"
#include "io/event_manager.h"
#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh_constants.h"
#include "sandesh/sandesh.h"
#include "sandesh/request_pipeline.h"

#include "bfd/bfd_client.h"
#include "bfd/bfd_server.h"
#include "bfd/bfd_session.h"
#include "bfd/bfd_common.h"
#include "bfd/bfd_udp_connection.h"
#include "bfd/rest_api/bfd_rest_server.h"
#include "bfd/rest_api/bfd_client_session.h"

using namespace BFD;

class Communicator : public Connection {
public:
    Communicator() { }
    virtual ~Communicator() { }

    virtual void SendPacket(
            const boost::asio::ip::udp::endpoint &local_endpoint,
            const boost::asio::ip::udp::endpoint &remote_endpoint,
            const SessionIndex &session_index,
            const boost::asio::mutable_buffer &send, int pktSize) {
    }
    virtual void NotifyStateChange(const SessionKey &key, const bool &up) {
    }
    virtual Server *GetServer() const { return server_; }
    virtual void SetServer(Server *server) { server_ = server; }

private:
    Server *server_;
};


int main(int argc, char *argv[]) {
    LoggingInit();
    EventManager evm;
    Communicator cm;
    Server server(&evm, &cm);
    Client bfd_client(server.communicator());
    evm.Run();
    return 0;
}
