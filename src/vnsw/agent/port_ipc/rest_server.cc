/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <vector>
#include <string>

#include <boost/bind.hpp>
#include <boost/assign.hpp>
#include <boost/foreach.hpp>

#include "base/regex.h"
#include "http/http_server.h"
#include "http/http_request.h"
#include "http/http_session.h"
#include "base/contrail_ports.h"
#include "port_ipc/port_ipc_handler.h"
#include "cmn/agent.h"
#include "rest_server.h"
#include "rest_common.h"

using contrail::regex;
using contrail::regex_match;
using contrail::regex_search;

class RestServerGetVmCfgTask : public Task {
public:
    RestServerGetVmCfgTask(Agent *agent,
                            const struct RESTServer::RESTData& data):
        Task(agent->task_scheduler()->GetTaskId("Agent::RestApi"), 0),
        agent_(agent),
        pih_(agent->port_ipc_handler()),
        vm_uuid_((*data.match)[1]),
        data_(data) {
    }
    virtual ~RestServerGetVmCfgTask() { }
    virtual bool Run();
    std::string Description() const { return "RestServerGetVmCfgTask"; }
private:
    Agent *agent_;
    const PortIpcHandler *pih_;
    std::string vm_uuid_;
    const struct RESTServer::RESTData& data_;
    DISALLOW_COPY_AND_ASSIGN(RestServerGetVmCfgTask);
};

bool RestServerGetVmCfgTask::Run() {

    std::string info;
    if (pih_->GetVmVnCfgPort(vm_uuid_, info)) {
        REST::SendResponse(data_.session, info);
    } else {
        REST::SendErrorResponse(data_.session, "{ Not Found }", 404);
    }
    return true;
}

void RESTServer::VmPortPostHandler(const struct RESTData& data) {
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (pih) {
        std::string err_msg;
        if (pih->AddPortFromJson(data.request->Body(), false, err_msg, true)) {
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

void RESTServer::GatewayPostHandler(const struct RESTData& data) {
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (pih) {
        std::string err_msg;
        if (pih->AddVgwFromJson(data.request->Body(), err_msg)) {
            REST::SendResponse(data.session, "{}");
        } else {
            REST::SendErrorResponse(data.session, "{ " + err_msg + " }");
        }
    } else {
       REST::SendErrorResponse(data.session, "{ Operation Not Supported }");
    }
}

void RESTServer::GatewayDeleteHandler(const struct RESTData& data) {
    std::string error;
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (pih) {
        std::string err_msg;
        if (pih->DelVgwFromJson(data.request->Body(), err_msg)) {
            REST::SendResponse(data.session, "{}");
        } else {
            REST::SendErrorResponse(data.session, "{" + error + "}");
        }
    } else {
        REST::SendErrorResponse(data.session, "{ Operation Not Supported }");
    }
}

void RESTServer::VmVnPortPostHandler(const struct RESTData& data) {
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (pih) {
        std::string err_msg;
        if (pih->AddVmVnPort(data.request->Body(), false, err_msg, true)) {
            REST::SendResponse(data.session, "{}");
        } else {
            REST::SendErrorResponse(data.session, "{ " + err_msg + " }");
        }
    } else {
       REST::SendErrorResponse(data.session, "{ Operation Not Supported }");
    }
}

void RESTServer::VmVnPortDeleteHandler(const struct RESTData& data) {
    std::string error;
    const std::string &vm_uuid = (*data.match)[1];
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (pih) {
        if (pih->DeleteVmVnPort(data.request->Body(), vm_uuid, error)) {
            REST::SendResponse(data.session, "{}");
        } else {
            REST::SendErrorResponse(data.session, "{" + error + "}");
        }
    } else {
        REST::SendErrorResponse(data.session, "{ Operation Not Supported }");
    }
}

void RESTServer::VmVnPortGetHandler(const struct RESTData& data) {
    const std::string &vm_uuid = (*data.match)[1];
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (pih) {
        std::string info;
        if (pih->GetVmVnPort(vm_uuid, info)) {
            REST::SendResponse(data.session, info);
        } else {
            REST::SendErrorResponse(data.session, "{ Not Found }", 404);
        }
    } else {
        REST::SendErrorResponse(data.session, "{ Operation Not Supported }");
    }
}

void RESTServer::VmVnPortCfgGetHandler(const struct RESTData& data) {
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (pih) {
        RestServerGetVmCfgTask *t =
            new RestServerGetVmCfgTask(agent_, data);
        agent_->task_scheduler()->Enqueue(t);
    } else {
        REST::SendErrorResponse(data.session, "{ Operation Not Supported }");
    }
}

void RESTServer::VmPortEnableHandler(const struct RESTData& data) {
    std::string error;
    const std::string& port_id = (*data.match)[1];
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (pih) {
        if (pih->EnablePort(port_id, error)) {
            REST::SendResponse(data.session, "{}");
        } else {
            REST::SendErrorResponse(data.session, "{" + error + "}");
        }
    } else {
        REST::SendErrorResponse(data.session, "{ Operation Not Supported }");
    }
}

void RESTServer::VmPortDisableHandler(const struct RESTData& data) {
    std::string error;
    const std::string& port_id = (*data.match)[1];
    PortIpcHandler *pih = agent_->port_ipc_handler();
    if (pih) {
        if (pih->DisablePort(port_id, error)) {
            REST::SendResponse(data.session, "{}");
        } else {
            REST::SendErrorResponse(data.session, "{" + error + "}");
        }
    } else {
        REST::SendErrorResponse(data.session, "{ Operation Not Supported }");
    }
}

const std::vector<RESTServer::HandlerSpecifier> RESTServer::RESTHandlers_ =
    boost::assign::list_of
    (HandlerSpecifier(
        regex("/port"),
        HTTP_POST,
        &RESTServer::VmPortPostHandler))
    (HandlerSpecifier(
        regex("/syncports"),
        HTTP_POST,
        &RESTServer::VmPortSyncHandler))
    (HandlerSpecifier(
        regex("/port/([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-"
              "[0-9a-f]{12})"),
        HTTP_DELETE,
        &RESTServer::VmPortDeleteHandler))
    (HandlerSpecifier(
        regex("/port/([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-"
              "[0-9a-f]{12})"),
        HTTP_GET,
        &RESTServer::VmPortGetHandler))
    (HandlerSpecifier(
        regex("/gateway"),
        HTTP_POST,
        &RESTServer::GatewayPostHandler))
    (HandlerSpecifier(
        regex("/gateway"),
        HTTP_DELETE,
        &RESTServer::GatewayDeleteHandler))
    (HandlerSpecifier(
        regex("/vm"),
        HTTP_POST,
        &RESTServer::VmVnPortPostHandler))
    (HandlerSpecifier(
        regex("/vm/([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-"
              "[0-9a-f]{12})"),
        HTTP_DELETE,
        &RESTServer::VmVnPortDeleteHandler))
    (HandlerSpecifier(
        regex("/vm/([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-"
              "[0-9a-f]{12})"),
        HTTP_GET,
        &RESTServer::VmVnPortGetHandler))
    (HandlerSpecifier(
        regex("/vm-cfg/([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-"
              "[0-9a-f]{12})"),
        HTTP_GET,
        &RESTServer::VmVnPortCfgGetHandler))
    (HandlerSpecifier(
        regex("/enable-port/([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-"
              "[0-9a-f]{4}-[0-9a-f]{12})"),
        HTTP_PUT,
        &RESTServer::VmPortEnableHandler))
    (HandlerSpecifier(
        regex("/disable-port/([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-"
              "[0-9a-f]{4}-[0-9a-f]{12})"),
        HTTP_PUT,
        &RESTServer::VmPortDisableHandler));

RESTServer::RESTServer(Agent *agent)
    : agent_(agent), http_server_(new HttpServer(agent->event_manager())) {
    http_server_->RegisterHandler(HTTP_WILDCARD_ENTRY,
        boost::bind(&RESTServer::HandleRequest, this, _1, _2));
}

void RESTServer::InitDone() {
    http_server_->Initialize(ContrailPorts::PortIpcVrouterAgentPort());
}

void RESTServer::HandleRequest(HttpSession* session,
                               const HttpRequest* request) {
    std::string path = request->UrlPath();
    BOOST_FOREACH(const RESTServer::HandlerSpecifier& hs, RESTHandlers_) {
        boost::smatch match;
        if (regex_match(path, match, hs.request_regex) &&
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
