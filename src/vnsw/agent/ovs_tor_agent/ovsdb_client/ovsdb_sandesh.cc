/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <ovs_tor_agent/tor_agent_init.h>

#include <ovsdb_client.h>
#include <ovsdb_client_idl.h>
#include <ovsdb_client_session.h>
#include <ovsdb_object.h>
#include <ovsdb_sandesh.h>
#include <ovsdb_types.h>

using namespace OVSDB;

static void SandeshError(const std::string msg, const std::string &context) {
    ErrorResp *resp = new ErrorResp();
    resp->set_resp(msg);
    resp->set_context(context);
    resp->Response();
}

static void SetErrorMsg(bool &error, std::string &error_msg, std::string msg) {
    if (error) {
        // previous error present, add separator
        error_msg += ", ";
    }
    error_msg += msg;
    error = true;
}

OvsdbSandeshTask::OvsdbSandeshTask(std::string resp_ctx,
                                   AgentSandeshArguments &args) :
    Task((TaskScheduler::GetInstance()->GetTaskId("Agent::KSync")), 0),
    ip_(), port_(0), resp_(NULL), resp_data_(resp_ctx), first_(0),
    last_(kEntriesPerPage - 1), total_count_(0), needs_next_(false),
    error_(false), error_msg_("") {
    if (!args.Get("session_ip", &ip_)) {
        SetErrorMsg(error_, error_msg_, "Error fetching session ip");
    }

    int val;
    if (args.Get("session_port", &val)) {
        port_ = (uint32_t) val;
    } else {
        SetErrorMsg(error_, error_msg_, "Error fetching session port");
    }

    if (args.Get("first", &val)) {
        first_ = (uint32_t) val;
    } else {
        SetErrorMsg(error_, error_msg_, "Error fetching first");
    }

    if (args.Get("last", &val)) {
        last_ = (uint32_t) val;
    } else {
        SetErrorMsg(error_, error_msg_, "Error fetching last");
    }
}

OvsdbSandeshTask::OvsdbSandeshTask(std::string resp_ctx, const std::string &ip,
                                   uint32_t port) :
    Task((TaskScheduler::GetInstance()->GetTaskId("Agent::KSync")), 0),
    ip_(ip), port_(port), resp_(NULL), resp_data_(resp_ctx), first_(0),
    last_(kEntriesPerPage - 1), total_count_(0), needs_next_(false),
    error_(false), error_msg_("") {
}

OvsdbSandeshTask::~OvsdbSandeshTask() {
}

bool OvsdbSandeshTask::Run() {
    if (error_) {
        SandeshError(error_msg_, resp_data_);
        return true;
    }
    OvsdbClient *ovsdb_client = Agent::GetInstance()->ovsdb_client();
    OvsdbClientSession *session;
    if (ip_.empty()) {
        session = ovsdb_client->NextSession(NULL);
    } else {
        boost::system::error_code ec;
        Ip4Address ip_addr = Ip4Address::from_string(ip_, ec);
        session = ovsdb_client->FindSession(ip_addr, port_);
    }
    uint32_t table_size = 0;
    uint32_t display_count = 0;
    if (NoSessionObject() == true ||
        (session != NULL && session->client_idl() != NULL)) {
        KSyncObject *table = NULL;
        if (NoSessionObject()) {
            table = GetObject(NULL);
        } else {
            ip_ = session->remote_ip().to_string();
            port_ = session->remote_port();
            table = GetObject(session);
        }

        if (table == NULL) {
            SandeshError("Error: Ovsdb Object not available", resp_data_);
            return true;
        }

        KSyncEntry *entry = table->Next(NULL);
        table_size = table->Size();
        uint8_t cur_count = 0;
        while (entry != NULL) {
            if (FilterAllow == Filter(entry)) {
                if ((first_ == (uint32_t)-1 || total_count_ >= first_) &&
                    (total_count_ <= last_)) {
                    cur_count++;
                    display_count++;
                    // allocate resp if not available
                    if (resp_ == NULL)
                        resp_ = Alloc();
                    UpdateResp(entry, resp_);
                }
                total_count_++;
            }
            entry = table->Next(entry);
            if (total_count_ == (last_ + 1)) {
                if (entry != NULL) {
                    // will need next page link
                    needs_next_ = true;
                }
            }
            if (cur_count == kEntriesPerSandesh && entry != NULL) {
                SendResponse(true);
                cur_count = 0;
            }
        }
    } else {
        SandeshError("Error: Session not available", resp_data_);
        return true;
    }

    SendResponse(true);

    EncodeSendPageReq(display_count, table_size);
    return true;
}

std::string OvsdbSandeshTask::EncodeFirstPage() {
    AgentSandeshArguments args;
    std::string s;
    args.Add("ovsdb_table", GetTableType());
    args.Add("session_ip", ip_);
    args.Add("session_port", port_);
    EncodeArgs(args);
    args.Add("first", 0);
    args.Add("last", kEntriesPerPage - 1);
    args.Encode(&s);

    return s;
}

void OvsdbSandeshTask::EncodeSendPageReq(uint32_t display_count,
                                         uint32_t table_size) {
    OvsdbPageResp *resp = new OvsdbPageResp();
    AgentSandeshArguments args;
    OvsdbPageRespData resp_data;
    std::string s;
    args.Add("ovsdb_table", GetTableType());
    args.Add("session_ip", ip_);
    args.Add("session_port", port_);
    EncodeArgs(args);

    // encode first page
    args.Add("first", 0);
    args.Add("last", kEntriesPerPage - 1);
    args.Encode(&s);
    args.Del("first");
    args.Del("last");
    resp_data.set_first_page(s);
    s.clear();

    uint32_t first = 0;
    // encode prev page
    if (first_ != 0 && first_ != (uint32_t)-1 && first_ <= total_count_) {
        first = first_;
        args.Add("first",
                 (first_ < kEntriesPerPage) ? 0 : (first_ - kEntriesPerPage));
        args.Add("last",
                 (first_ < kEntriesPerPage) ? (kEntriesPerPage -1) : (first_ - 1));
        args.Encode(&s);
        args.Del("first");
        args.Del("last");
        resp_data.set_prev_page(s);
        s.clear();
    }

    // encode next page
    if (needs_next_) {
        args.Add("first", last_ + 1);
        args.Add("last", last_ + kEntriesPerPage);
        args.Encode(&s);
        args.Del("first");
        args.Del("last");
        resp_data.set_next_page(s);
        s.clear();
    }

    // encode all page
    args.Add("first", -1);
    args.Add("last", -1);
    args.Encode(&s);
    args.Del("first");
    args.Del("last");
    resp_data.set_all(s);
    s.clear();

    std::stringstream match_str;
    if (display_count != 0) {
        uint32_t last = first + display_count - 1;
        match_str << first << " - " << (last);
    } else {
        match_str << "0";
    }
    match_str << " / " << total_count_;
    resp_data.set_entries(match_str.str());
    resp_data.set_table_size(table_size);

    resp->set_req(resp_data);
    resp_ = resp;
    SendResponse(false);
}

void OvsdbSandeshTask::SendResponse(bool more) {
    if (resp_ == NULL) {
        // no response formed to send
        return;
    }
    resp_->set_context(resp_data_);
    resp_->set_more(more);
    resp_->Response();
    resp_ = NULL;
}

void OvsdbPageReq::HandleRequest() const {
    AgentSandeshArguments args;
    args.Decode(get_key());

    OvsdbSandeshTask *task = NULL;
    int table_type;
    args.Get("ovsdb_table", &table_type);
    switch (table_type) {
    case OvsdbSandeshTask::PHYSICAL_PORT_TABLE:
        task = new PhysicalPortSandeshTask(context(), args);
        break;
    case OvsdbSandeshTask::LOGICAL_SWITCH_TABLE:
        task = new LogicalSwitchSandeshTask(context(), args);
        break;
    case OvsdbSandeshTask::VLAN_PORT_TABLE:
        task = new VlanPortBindingSandeshTask(context(), args);
        break;
    case OvsdbSandeshTask::VRF_TABLE:
        task = new OvsdbVrfSandeshTask(context(), args);
        break;
    case OvsdbSandeshTask::UNICAST_REMOTE_TABLE:
        task = new UnicastMacRemoteSandeshTask(context(), args);
        break;
    case OvsdbSandeshTask::UNICAST_LOCAL_TABLE:
        task = new UnicastMacLocalSandeshTask(context(), args);
        break;
    case OvsdbSandeshTask::MULTICAST_LOCAL_TABLE:
        task = new MulticastMacLocalSandeshTask(context(), args);
        break;
    case OvsdbSandeshTask::HA_STALE_DEV_VN_TABLE:
        task = new HaStaleDevVnSandeshTask(context(), args);
        break;
    case OvsdbSandeshTask::HA_STALE_L2_ROUTE_TABLE:
        task = new HaStaleL2RouteSandeshTask(context(), args);
        break;
    default:
        break;
    }

    if (task != NULL) {
        TaskScheduler *scheduler = TaskScheduler::GetInstance();
        scheduler->Enqueue(task);
    } else {
        SandeshError("Error: Invalid table type", context());
    }
}

