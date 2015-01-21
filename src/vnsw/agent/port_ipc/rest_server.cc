/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <string>

#include <boost/regex.hpp>
#include <boost/bind.hpp>
#include <boost/random.hpp>
#include <boost/assign.hpp>
#include <boost/foreach.hpp>

#include <rapidjson/document.h>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <base/logging.h>
#include <base/contrail_ports.h>
#include "rest_server.h"
#include "rest_common.h"
#include <port_ipc/port_ipc_handler.h>

void RESTServer::VmPortPutHandler(const struct RESTData& data) {
    PortIpcHandler pih(agent_);
    pih.AddPortFromJson(data.request->Body());
    REST::SendResponse(data.session, "{}");
}

void RESTServer::VmPortDeleteHandler(const struct RESTData& data) {
    const std::string& port_id = (*data.match)[1];
    PortIpcHandler pih(agent_);
    pih.DeletePort(port_id);
    REST::SendResponse(data.session, "{}");
}

const std::vector<RESTServer::HandlerSpecifier> RESTServer::RESTHandlers_ =
    boost::assign::list_of
    (HandlerSpecifier(
        boost::regex("/add_port"),
        HTTP_PUT,
        &RESTServer::VmPortPutHandler))
    (HandlerSpecifier(
        boost::regex("/port/([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-"
                     "[0-9a-f]{12})"),
        HTTP_DELETE,
        &RESTServer::VmPortDeleteHandler));

RESTServer::RESTServer(Agent *agent)
    : agent_(agent), http_server_(new HttpServer(agent->event_manager())) {
    http_server_->RegisterHandler(HTTP_WILDCARD_ENTRY,
        boost::bind(&RESTServer::HandleRequest, this, _1, _2));
    http_server_->Initialize(ContrailPorts::PortIpcVrouterAgentPort());
}

void RESTServer::HandleRequest(HttpSession* session,
                               const HttpRequest* request) {
    tbb::mutex::scoped_lock lock(mutex_);

    std::string path = request->UrlPath();
    BOOST_FOREACH(const RESTServer::HandlerSpecifier& hs, RESTHandlers_) {
        boost::smatch match;
        if (boost::regex_match(path, match, hs.request_regex) &&
            request->GetMethod() == hs.method) {
            struct RESTData data = {
                &match,
                request->GetMethod(),
                session,
                request
            };
            LOG(INFO, "Handling request: " << path);
            (this->*hs.handler_func)(data);
            // Though IMHO it contradicts most programmers' intuition,
            // currently handlers are responsible for freeing handled
            // requests.
            delete request;
            return;
        }
    }
    REST::SendErrorResponse(session, "Unknown Request", 404);
}

void RESTServer::Shutdown() {
    if (http_server_) {
        http_server_->Shutdown();
        TcpServerManager::DeleteServer(http_server_);
        http_server_ = NULL;
    }
}

RESTServer::~RESTServer() {
}
