/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <string>

#include <boost/bind.hpp>
#include <boost/assign.hpp>
#include <boost/foreach.hpp>

#include "http/http_server.h"
#include "http/http_request.h"
#include "http/http_session.h"
#include "base/contrail_ports.h"
#include "port_ipc/port_ipc_handler.h"
#include "cmn/agent.h"
#include "rest_server.h"
#include "rest_common.h"

void RESTServer::VmPortPostHandler(const struct RESTData& data) {
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (pih) {
        std::string err_msg;
        if (pih->AddPortFromJson(data.request->Body(), false, err_msg)) {
            REST::SendResponse(data.session, "{}");
        } else {
            REST::SendErrorResponse(data.session, "{ " + err_msg + " }");
        }
    } else {
       REST::SendErrorResponse(data.session, "{ Operation Not Supported }");
    }
}

void RESTServer::VmPortSyncHandler(const struct RESTData& data) {
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (pih) {
        pih->SyncHandler();
        REST::SendResponse(data.session, "{}");
    } else {
       REST::SendErrorResponse(data.session, "{ Operation Not Supported }");
    }
}

void RESTServer::VmPortDeleteHandler(const struct RESTData& data) {
    std::string error;
    const std::string& port_id = (*data.match)[1];
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (pih) {
        if (pih->DeletePort(port_id, error)) {
            REST::SendResponse(data.session, "{}");
        } else {
            REST::SendErrorResponse(data.session, "{" + error + "}");
        }
    } else {
        REST::SendErrorResponse(data.session, "{ Operation Not Supported }");
    }
}

void RESTServer::VmPortGetHandler(const struct RESTData& data) {
    const std::string& port_id = (*data.match)[1];
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (pih) {
        std::string info;
        if (pih->GetPortInfo(port_id, info)) {
            REST::SendResponse(data.session, info);
        } else {
            REST::SendErrorResponse(data.session, "{ Not Found }", 404);
        }
    } else {
        REST::SendErrorResponse(data.session, "{ Operation Not Supported }");
    }
}

const std::vector<RESTServer::HandlerSpecifier> RESTServer::RESTHandlers_ =
    boost::assign::list_of
    (HandlerSpecifier(
        boost::regex("/port"),
        HTTP_POST,
        &RESTServer::VmPortPostHandler))
    (HandlerSpecifier(
        boost::regex("/syncports"),
        HTTP_POST,
        &RESTServer::VmPortSyncHandler))
    (HandlerSpecifier(
        boost::regex("/port/([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-"
                     "[0-9a-f]{12})"),
        HTTP_DELETE,
        &RESTServer::VmPortDeleteHandler))
    (HandlerSpecifier(
        boost::regex("/port/([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-"
                     "[0-9a-f]{12})"),
        HTTP_GET,
        &RESTServer::VmPortGetHandler));

RESTServer::RESTServer(Agent *agent)
    : agent_(agent), http_server_(new HttpServer(agent->event_manager())) {
    http_server_->RegisterHandler(HTTP_WILDCARD_ENTRY,
        boost::bind(&RESTServer::HandleRequest, this, _1, _2));
    http_server_->Initialize(ContrailPorts::PortIpcVrouterAgentPort());
}

void RESTServer::HandleRequest(HttpSession* session,
                               const HttpRequest* request) {
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
