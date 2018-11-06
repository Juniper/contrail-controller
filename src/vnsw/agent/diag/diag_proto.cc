/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <init/agent_init.h>
#include <oper/interface.h>
#include <oper/vm_interface.h>
#include <oper/metadata_ip.h>
#include <pkt/proto.h>
#include <pkt/proto_handler.h>
#include <diag/diag_proto.h>
#include <diag/diag_pkt_handler.h>
#include <diag/segment_health_check.h>

DiagProto::DiagProto(Agent *agent, boost::asio::io_service &io)
    : Proto(agent, "Agent::Diag", PktHandler::DIAG, io),
      session_map_(), stats_mutex_(), stats_() {
    agent->health_check_table()->RegisterHealthCheckCallback(
        boost::bind(&DiagProto::SegmentHealthCheckProcess, this, _1, _2),
        HealthCheckService::SEGMENT);
}

ProtoHandler *DiagProto::AllocProtoHandler(boost::shared_ptr<PktInfo> info,
                                           boost::asio::io_service &io) {
    return new DiagPktHandler(agent(), info, io);
}

bool DiagProto::SegmentHealthCheckProcess(
    HealthCheckTable::HealthCheckServiceAction action,
    HealthCheckInstanceService *service) {

    uint32_t intf_id = service->interface()->id();
    SessionMap::iterator it = session_map_.find(intf_id);

    switch (action) {
        case HealthCheckTable::CREATE_SERVICE:
        case HealthCheckTable::UPDATE_SERVICE:
            {
                /* When we get create/update request for already created service,
                 * we update the existing object to reflect change in properties
                 * of service.
                 */
                if (it != session_map_.end()) {
                    SegmentHealthCheckPkt *old_ptr = it->second;
                    old_ptr->set_service(NULL);
                    old_ptr->UpdateService(service);
                    return true;
                }
                SegmentHealthCheckPkt *ptr =
                    new SegmentHealthCheckPkt(service, agent_->diag_table());

                session_map_.insert(SessionPair(intf_id, ptr));
                /* Init will add ptr to DiagTable and sends
                 * health-check-packets
                 */
                ptr->Init();
                break;
            }

        case HealthCheckTable::DELETE_SERVICE:
            {
                /* Ignore delete request if we are not sending any
                 * health-check-packets on this interface.
                 */
                if (it == session_map_.end()) {
                    return true;
                }
                SegmentHealthCheckPkt *old_ptr = it->second;
                old_ptr->set_service(NULL);
                old_ptr->StopDelayTimer();
                session_map_.erase(it);

                /* DeleteEnqueue will remove old_ptr from DiagTable and
                 * frees it */
                old_ptr->EnqueueForceDelete();
                break;
            }

        case HealthCheckTable::RUN_SERVICE:
            break;

        case HealthCheckTable::STOP_SERVICE:
            break;

        default:
            assert(0);
    }

    return true;
}

void DiagProto::IncrementDiagStats(uint32_t itf_id, DiagStatsType type) {
    tbb::mutex::scoped_lock lock(stats_mutex_);
    DiagStats new_entry;
    std::pair<DiagStatsMap::iterator, bool> ret = stats_.insert(DiagStatsPair
                                                                (itf_id,
                                                                 new_entry));
    DiagStats &entry = ret.first->second;
    switch (type) {
        case REQUESTS_SENT:
            entry.requests_sent++;
            break;
        case REQUESTS_RECEIVED:
            entry.requests_received++;
            break;
        case REPLIES_SENT:
            entry.replies_sent++;
            break;
        case REPLIES_RECEIVED:
            entry.replies_received++;
            break;
        default:
            assert(0);
    }
}

void DiagProto::FillSandeshHealthCheckResponse(SegmentHealthCheckPktStatsResp
                                               *resp) {
    vector<SegmentHealthCheckStats> &list =
        const_cast<std::vector<SegmentHealthCheckStats>&>(resp->get_stats());
    tbb::mutex::scoped_lock lock(stats_mutex_);
    DiagProto::DiagStatsMap::const_iterator it = stats_.begin();
    while (it != stats_.end()) {
        SegmentHealthCheckStats item;
        DiagProto::DiagStats source = it->second;
        item.set_interface_index(it->first);
        item.set_requests_sent(source.requests_sent);
        item.set_requests_received(source.requests_received);
        item.set_replies_sent(source.replies_sent);
        item.set_replies_received(source.replies_received);
        list.push_back(item);
        ++it;
    }
}

void SegmentHealthCheckPktStats::HandleRequest() const {
    DiagProto *proto = Agent::GetInstance()->diag_table()->diag_proto();
    SegmentHealthCheckPktStatsResp *resp = new SegmentHealthCheckPktStatsResp();
    proto->FillSandeshHealthCheckResponse(resp);
    resp->set_context(context());
    resp->Response();
    return;
}
