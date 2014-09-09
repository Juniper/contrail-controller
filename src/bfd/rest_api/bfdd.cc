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

#include "bfd/bfd_server.h"
#include "bfd/bfd_session.h"
#include "bfd/bfd_common.h"
#include "bfd/bfd_udp_connection.h"
#include "bfd/rest_api/bfd_rest_server.h"
#include "bfd/rest_api/bfd_client_session.h"

using BFD::Server;
using BFD::RESTServer;
using BFD::UDPConnectionManager;

int main(int argc, char *argv[]) {
    LoggingInit();

    EventManager evm;
    UDPConnectionManager cm(&evm);

    Server bfd_server(&evm, &cm);
    RESTServer server(&bfd_server);
    cm.RegisterCallback(boost::bind(&Server::ProcessControlPacket,
                        &bfd_server, _1));

    HttpServer *http(new HttpServer(&evm));
    http->RegisterHandler(HTTP_WILDCARD_ENTRY,
        boost::bind(&RESTServer::HandleRequest, &server, _1, _2));

    http->Initialize(8090);

    evm.Run();

    http->Shutdown();
    TcpServerManager::DeleteServer(http);

    return 0;
}
