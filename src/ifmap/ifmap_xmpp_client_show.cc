/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include <boost/bind.hpp>
#include <boost/assign/list_of.hpp>
#include "base/logging.h"
#include "bgp/bgp_sandesh.h"

#include "ifmap/ifmap_client.h"
#include "ifmap/ifmap_server.h"
#include "ifmap/ifmap_xmpp.h"
#include "ifmap/ifmap_server_show_types.h" // sandesh

using namespace boost::assign;
using namespace std;

class ShowIFMapXmppClientInfo {
public:
    static const int kMaxElementsPerRound = 50;

    struct ShowData : public RequestPipeline::InstData {
        vector<IFMapXmppClientInfo> send_buffer;
    };

    static RequestPipeline::InstData *AllocBuffer(int stage) {
        return static_cast<RequestPipeline::InstData *>(new ShowData);
    }

    struct TrackerData : public RequestPipeline::InstData {
        // init as 1 indicates we need to init 'first' to begin() since there is
        // no way to initialize an iterator here.
        TrackerData() : init(1) { }
        int init;
        vector<IFMapXmppClientInfo>::const_iterator first;
    };

    static RequestPipeline::InstData *AllocTracker(int stage) {
        return static_cast<RequestPipeline::InstData *>(new TrackerData);
    }

    static bool BufferStage(const Sandesh *sr,
                            const RequestPipeline::PipeSpec ps, int stage,
                            int instNum, RequestPipeline::InstData *data);
    static void CopyNode(IFMapXmppClientInfo *dest, IFMapClient *src);
    static bool SendStage(const Sandesh *sr, const RequestPipeline::PipeSpec ps,
                          int stage, int instNum,
                          RequestPipeline::InstData *data);
};

void ShowIFMapXmppClientInfo::CopyNode(IFMapXmppClientInfo *dest,
                                       IFMapClient *src) {
    dest->set_client_name(src->identifier());
    dest->set_client_index(src->index());
    dest->set_msgs_sent(src->msgs_sent());
    dest->set_msgs_blocked(src->msgs_blocked());
    dest->set_nodes_sent(src->nodes_sent());
    dest->set_links_sent(src->links_sent());
    dest->set_bytes_sent(src->bytes_sent());
    dest->set_is_blocked(src->send_is_blocked());

    VmRegInfo vm_reg_info;
    vm_reg_info.vm_list = src->vm_list();
    vm_reg_info.vm_count = vm_reg_info.vm_list.size();
    dest->set_vm_reg_info(vm_reg_info);
}

bool ShowIFMapXmppClientInfo::BufferStage(const Sandesh *sr,
                                     const RequestPipeline::PipeSpec ps,
                                     int stage, int instNum,
                                     RequestPipeline::InstData *data) {
    const IFMapXmppClientInfoShowReq *request = 
        static_cast<const IFMapXmppClientInfoShowReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc = 
        static_cast<BgpSandeshContext *>(request->client_context());
    IFMapServer* server = bsc->ifmap_server;

    IFMapServer::ClientMap client_map = server->GetClientMap();

    ShowData *show_data = static_cast<ShowData *>(data);
    show_data->send_buffer.reserve(client_map.size());

    for (IFMapServer::ClientMap::iterator iter = client_map.begin();
         iter != client_map.end(); ++iter) {
	IFMapXmppClientInfo dest;
        IFMapClient *src = iter->second;
	CopyNode(&dest, src);
        show_data->send_buffer.push_back(dest);
    }

    return true;
}

// Can be called multiple times i.e. approx total/kMaxElementsPerRound
bool ShowIFMapXmppClientInfo::SendStage(const Sandesh *sr,
                                   const RequestPipeline::PipeSpec ps,
                                   int stage, int instNum,
                                   RequestPipeline::InstData *data) {
    const RequestPipeline::StageData *prev_stage_data = ps.GetStageData(0);
    const ShowIFMapXmppClientInfo::ShowData &show_data = 
        static_cast<const ShowIFMapXmppClientInfo::ShowData &>
                                                       (prev_stage_data->at(0));

    // Data for this stage
    TrackerData *tracker_data = static_cast<TrackerData *>(data);

    vector<IFMapXmppClientInfo> dest_buffer;
    vector<IFMapXmppClientInfo>::const_iterator first, last;
    bool more = false;

    if (tracker_data->init) {
        first = show_data.send_buffer.begin();
        tracker_data->init = 0;
    } else {
        first = tracker_data->first;
    }
    int rem_num = show_data.send_buffer.end() - first;
    int send_num = (rem_num < kMaxElementsPerRound) ? rem_num :
                                                      kMaxElementsPerRound;
    last = first + send_num;
    copy(first, last, back_inserter(dest_buffer));
    // Decide if we want to be called again.
    if ((rem_num - send_num) > 0) {
        more = true;
    } else {
        more = false;
    }
    const IFMapXmppClientInfoShowReq *request = 
        static_cast<const IFMapXmppClientInfoShowReq *>(ps.snhRequest_.get());
    IFMapXmppClientInfoShowResp *response = new IFMapXmppClientInfoShowResp();
    response->set_client_stats(dest_buffer);
    response->set_context(request->context());
    response->set_more(more);
    response->Response();
    tracker_data->first = first + send_num;

    // Return 'false' to be called again
    return (!more);
}

void IFMapXmppClientInfoShowReq::HandleRequest() const {

    RequestPipeline::StageSpec s0, s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    // 2 stages - first: gather/read, second: send

    s0.taskId_ = scheduler->GetTaskId("db::DBTable");
    s0.allocFn_ = ShowIFMapXmppClientInfo::AllocBuffer;
    s0.cbFn_ = ShowIFMapXmppClientInfo::BufferStage;
    s0.instances_.push_back(0);

    // control-node ifmap show command task
    s1.taskId_ = scheduler->GetTaskId("cn_ifmap::ShowCommand");
    s1.allocFn_ = ShowIFMapXmppClientInfo::AllocTracker;
    s1.cbFn_ = ShowIFMapXmppClientInfo::SendStage;
    s1.instances_.push_back(0);

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s0)(s1);
    RequestPipeline rp(ps);
}
