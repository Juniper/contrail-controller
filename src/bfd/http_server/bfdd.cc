/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "http/http_request.h"
#include "http/http_server.h"
#include "http/http_session.h"
#include "io/event_manager.h"
#include <boost/bind.hpp>
#include <boost/asio/ip/host_name.hpp>
#include <boost/foreach.hpp>
#include <boost/tokenizer.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/thread/thread.hpp>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include <bfd/bfd_server.h>
#include <bfd/bfd_session.h>

#include <bfd/bfd_common.h>
#include <bfd/bfd_udp_connection.h>

#include <bfd/http_server/bfd_config_server.h>
#include <bfd/http_server/bfd_client_session.h>


using namespace BFD;

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh_constants.h"
#include "sandesh/sandesh.h"
#include "sandesh/request_pipeline.h"
#include <bfd/test/bfd_test_utils.h>
int
main(int argc, char *argv[]) {
    LoggingInit();

    EventManager evm;
    UDPConnectionManager cm(&evm);

    BFDServer bfd_server(&evm, &cm);
    BFDConfigServer server(&bfd_server);
    cm.RegisterCallback(boost::bind(&BFDServer::ProcessControlPacket, &bfd_server, _1));

    HttpServer *http(new HttpServer(&evm));
    http->RegisterHandler(HTTP_WILDCARD_ENTRY,
        boost::bind(&BFDConfigServer::HandleRequest, &server, _1, _2));

    http->Initialize(8090);

    evm.Run();

    http->Shutdown();
    TcpServerManager::DeleteServer(http);

    return 0;
}

