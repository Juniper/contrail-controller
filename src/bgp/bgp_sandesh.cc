/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_sandesh.h"

#include <boost/assign/list_of.hpp>
#include <boost/foreach.hpp>
#include <sandesh/request_pipeline.h>

#include "bgp/bgp_multicast.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/inet/inet_table.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"

using namespace boost::assign;
using namespace std;

class ShowNeighborStatisticsHandler {
public:
    static bool CallbackS1(const Sandesh *sr,
                           const RequestPipeline::PipeSpec ps, int stage,
                           int instNum, RequestPipeline::InstData * data);
    static size_t FillBgpNeighborStatistics(const ShowNeighborStatisticsReq *req,
                                   BgpServer *bgp_server);
};

size_t ShowNeighborStatisticsHandler::FillBgpNeighborStatistics(
           const ShowNeighborStatisticsReq *req, BgpServer *bgp_server) {
    size_t count = 0;

    if (req->get_bgp_or_xmpp().empty() ||
            boost::iequals(req->get_bgp_or_xmpp(), "bgp")) {
        RoutingInstanceMgr *rim = bgp_server->routing_instance_mgr();
        if (!req->get_domain().empty()) {
            RoutingInstance *ri = rim->GetRoutingInstance(req->get_domain());
            if (ri) {
                count += ri->peer_manager()->GetNeighborCount(req->get_up_or_down());
            }
        } else {
            RoutingInstanceMgr::RoutingInstanceIterator it = rim->begin();
            for (;it != rim->end(); it++) {
                count += it->peer_manager()->GetNeighborCount(req->get_up_or_down());
            }
        }
    }

    return count;
}

bool ShowNeighborStatisticsHandler::CallbackS1(
        const Sandesh *sr, const RequestPipeline::PipeSpec ps,
        int stage, int instNum, RequestPipeline::InstData *data) {

    const ShowNeighborStatisticsReq *req;
    BgpSandeshContext *bsc;

    req = static_cast<const ShowNeighborStatisticsReq *>(
                                ps.snhRequest_.get());
    bsc = static_cast<BgpSandeshContext *>(req->client_context());

    // Retrieve number of BGP peers.
    size_t count = FillBgpNeighborStatistics(req, bsc->bgp_server);

    // Retrieve numner of XMPP agents.
    if (req->get_bgp_or_xmpp().empty() ||
            boost::iequals(req->get_bgp_or_xmpp(), "xmpp")) {
        bsc->ShowNeighborStatisticsExtension(&count, req);
    }

    ShowNeighborStatisticsResp *resp = new ShowNeighborStatisticsResp;
    resp->set_bgp_or_xmpp(req->get_bgp_or_xmpp());
    resp->set_up_or_down(req->get_up_or_down());
    resp->set_domain(req->get_domain());
    resp->set_count(count);
    resp->set_context(req->context());
    resp->Response();

    return true;
}

void ShowNeighborStatisticsReq::HandleRequest() const {

    // Use config task as we need to examine both bgp and xmpp data bases
    // to compute the number of neighbors. Both BGP and XMPP peers are
    // inserted/deleted under config task.
    RequestPipeline::StageSpec s1;
    s1.taskId_ = TaskScheduler::GetInstance()->GetTaskId("bgp::Config");
    s1.instances_.push_back(0);
    s1.cbFn_ = ShowNeighborStatisticsHandler::CallbackS1;

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s1);
    RequestPipeline rp(ps);
}

class ClearBgpNeighborHandler {
public:
    static bool CallbackS1(const Sandesh *sr,
                           const RequestPipeline::PipeSpec ps, int stage,
                           int instNum, RequestPipeline::InstData * data);
};

bool ClearBgpNeighborHandler::CallbackS1(
        const Sandesh *sr, const RequestPipeline::PipeSpec ps,
        int stage, int instNum, RequestPipeline::InstData *data) {
    const ClearBgpNeighborReq *req;
    BgpSandeshContext *bsc;

    req = static_cast<const ClearBgpNeighborReq *>(ps.snhRequest_.get());
    bsc = static_cast<BgpSandeshContext *>(req->client_context());
    BgpPeer *peer = bsc->bgp_server->FindPeer(req->get_name());

    ClearBgpNeighborResp *resp = new ClearBgpNeighborResp;
    if (!bsc->test_mode()) {
        resp->set_success(false);
    } else if (peer) {
        peer->Clear(BgpProto::Notification::AdminReset);
        resp->set_success(true);
    } else {
        resp->set_success(false);
    }
    resp->set_context(req->context());
    resp->Response();

    return true;
}

// handler for 'clear bgp neighbor'
void ClearBgpNeighborReq::HandleRequest() const {

    // Use config task since neighbors are added/deleted under that task.
    // to compute the number of neighbors.
    RequestPipeline::StageSpec s1;
    s1.taskId_ = TaskScheduler::GetInstance()->GetTaskId("bgp::Config");
    s1.instances_.push_back(0);
    s1.cbFn_ = ClearBgpNeighborHandler::CallbackS1;

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s1);
    RequestPipeline rp(ps);
}

class ShowMulticastManagerDetailHandler {
public:
    struct MulticastManagerDetailData : public RequestPipeline::InstData {
        vector<ShowMulticastTree> tree_list;
    };

    static RequestPipeline::InstData *CreateData(int stage) {
        return (new MulticastManagerDetailData);
    }

    static void FillMulticastLinkInfo(const McastForwarder *forwarder,
        ShowMulticastTreeLink *smtl) {
        smtl->set_address(forwarder->address().to_string());
        smtl->set_label(forwarder->label());
    }

    static void FillMulticastForwarderInfo(const McastForwarder *forwarder,
        ShowMulticastForwarder *smf) {
        smf->set_address(forwarder->address().to_string());
        smf->set_label(forwarder->label());
        smf->set_label_block(forwarder->label_block()->ToString());
        smf->set_router_id(forwarder->router_id().to_string());
        for (McastForwarderList::const_iterator it =
             forwarder->tree_links_.begin();
             it != forwarder->tree_links_.end(); ++it) {
            ShowMulticastTreeLink smtl;
            FillMulticastLinkInfo(*it, &smtl);
            smf->links.push_back(smtl);
        }
    }

    static void FillMulticastTreeInfo(const McastSGEntry *sg,
        ShowMulticastTree *smt) {
        smt->set_group(sg->group().to_string());
        smt->set_source(sg->source().to_string());
        for (uint8_t level = McastTreeManager::LevelFirst;
             level < McastTreeManager::LevelCount; ++level) {
            if (!sg->IsTreeBuilder(level))
                continue;
            for (McastSGEntry::ForwarderSet::const_iterator it =
                 sg->forwarder_sets_[level]->begin();
                 it != sg->forwarder_sets_[level]->end(); ++it) {
                ShowMulticastForwarder smf;
                FillMulticastForwarderInfo(*it, &smf);
                if (level == McastTreeManager::LevelNative) {
                    smt->level0_forwarders.push_back(smf);
                } else {
                    smt->level1_forwarders.push_back(smf);
                }
            }
        }
    }

    static void FillMulticastPartitionInfo(MulticastManagerDetailData *data,
            ErmVpnTable *table, int inst_id) {
        McastTreeManager *tm = table->GetTreeManager();
        McastManagerPartition *partition = tm->GetPartition(inst_id);
        for (McastManagerPartition::SGList::const_iterator it =
             partition->sg_list_.begin();
             it != partition->sg_list_.end(); it++) {
            ShowMulticastTree smt;
            FillMulticastTreeInfo(*it, &smt);
            data->tree_list.push_back(smt);
        }
    }

    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
        int inst_id = ps.stages_[stage].instances_[instNum];

        MulticastManagerDetailData *mydata =
            static_cast<MulticastManagerDetailData *>(data);
        const ShowMulticastManagerDetailReq *req =
            static_cast<const ShowMulticastManagerDetailReq *>(ps.snhRequest_.get());
        BgpSandeshContext *bsc =
            static_cast<BgpSandeshContext *>(req->client_context());
        DBTableBase *table = bsc->bgp_server->database()->FindTable(req->get_name());
        ErmVpnTable *mcast_table = dynamic_cast<ErmVpnTable *>(table);
        if (mcast_table && !mcast_table->IsVpnTable())
            FillMulticastPartitionInfo(mydata, mcast_table, inst_id);

        return true;
    }

    static void CombineMulticastPartitionInfo(
            const RequestPipeline::StageData *sd,
            vector<ShowMulticastTree> &tree_list) {
        for (size_t idx = 0; idx < sd->size(); idx++) {
            const MulticastManagerDetailData &data =
                static_cast<const MulticastManagerDetailData &>(sd->at(idx));
            tree_list.insert(tree_list.end(),
                             data.tree_list.begin(), data.tree_list.end());
        }
    }

    static bool CallbackS2(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
        const ShowMulticastManagerReq *req =
            static_cast<const ShowMulticastManagerReq *>(ps.snhRequest_.get());
        const RequestPipeline::StageData *sd = ps.GetStageData(0);
        vector<ShowMulticastTree> tree_list;
        CombineMulticastPartitionInfo(sd, tree_list);

        ShowMulticastManagerDetailResp *resp = new ShowMulticastManagerDetailResp;
        resp->set_trees(tree_list);
        resp->set_context(req->context());
        resp->Response();
        return true;
    }
};


void ShowMulticastManagerDetailReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(client_context());

    // Request pipeline has 2 stages.
    // First stage to collect multicast manager stats.
    // Second stage to fill stats from stage 1 and respond to the request.
    RequestPipeline::StageSpec s1, s2;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("db::DBTable");
    s1.allocFn_ = ShowMulticastManagerDetailHandler::CreateData;
    s1.cbFn_ = ShowMulticastManagerDetailHandler::CallbackS1;
    for (int i = 0; i < bsc->bgp_server->database()->PartitionCount(); i++) {
        s1.instances_.push_back(i);
    }

    s2.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s2.cbFn_ = ShowMulticastManagerDetailHandler::CallbackS2;
    s2.instances_.push_back(0);

    ps.stages_ = list_of(s1)(s2);
    RequestPipeline rp(ps);
}


class ShowRouteVrfHandler {
public:
    struct SearchRouteInVrfData : public RequestPipeline::InstData {
        vector<ShowRoute> routes;
    };

    static RequestPipeline::InstData *CreateData(int stage) {
        return (new SearchRouteInVrfData);
    }

    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
        SearchRouteInVrfData *sdata = static_cast<SearchRouteInVrfData *>(data);
        const ShowRouteVrfReq *req =
            static_cast<const ShowRouteVrfReq *>(ps.snhRequest_.get());
        BgpSandeshContext *bsc =
            static_cast<BgpSandeshContext *>(req->client_context());
        RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();
        RoutingInstance *ri = rim->GetRoutingInstance(req->get_vrf());
        if (!ri) return true;
        BgpTable *table = ri->GetTable(Address::INET);
        if (!table) return true;
        Ip4Prefix prefix(Ip4Prefix::FromString(req->get_prefix()));
        InetTable::RequestKey key(prefix, NULL);
        InetRoute *route = static_cast<InetRoute *>(table->Find(&key));
        if (!route) return true;
        ShowRoute show_route;
        show_route.set_prefix(route->ToString());
        show_route.set_last_modified(duration_usecs_to_string(
                       UTCTimestampUsec() - route->last_change_at()));
        vector<ShowRoutePath> show_route_paths;
        for(Route::PathList::const_iterator it = route->GetPathList().begin();
            it != route->GetPathList().end(); it++) {
            const BgpPath *path = static_cast<const BgpPath *>(it.operator->());
            ShowRoutePath srp;
            srp.set_protocol("BGP");
            const BgpAttr *attr = path->GetAttr();
            if (attr->as_path() != NULL)
                srp.set_as_path(attr->as_path()->path().ToString());
            srp.set_local_preference(attr->local_pref());
            if (path->GetPeer()) srp.set_source(path->GetPeer()->ToString());
            srp.set_last_modified(duration_usecs_to_string(
                    UTCTimestampUsec() - path->time_stamp_usecs()));
            srp.set_next_hop(attr->nexthop().to_string());
            srp.set_label(path->GetLabel());
            show_route_paths.push_back(srp);
        }
        show_route.set_paths(show_route_paths);
        sdata->routes.push_back(show_route);
        return true;
    }

    static bool CallbackS2(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
        const RequestPipeline::StageData *sd = ps.GetStageData(0);
        ShowRoute route;
        for (size_t i = 0; i < sd->size(); i++) {
            const SearchRouteInVrfData &data = static_cast<const SearchRouteInVrfData &>(sd->at(i));
            if (data.routes.size()) {
                route = data.routes.front();
                break;
            }
        }

        ShowRouteVrfResp *resp = new ShowRouteVrfResp;
        resp->set_route(route);
        const ShowRouteVrfReq *req =
            static_cast<const ShowRouteVrfReq *>(ps.snhRequest_.get());
        resp->set_context(req->context());
        resp->Response();
        return true;
    }
};

void ShowRouteVrfReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(client_context());

    // Request pipeline has 2 stages.
    // First stage to search in different partition
    // Second stage to fill stats from stage 1 and respond to the request.
    RequestPipeline::StageSpec s1, s2;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("db::DBTable");
    s1.allocFn_ = ShowRouteVrfHandler::CreateData;
    s1.cbFn_ = ShowRouteVrfHandler::CallbackS1;
    for (int i = 0; i < bsc->bgp_server->database()->PartitionCount(); i++) {
        s1.instances_.push_back(i);
    }

    s2.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s2.cbFn_ = ShowRouteVrfHandler::CallbackS2;
    s2.instances_.push_back(0);

    ps.stages_ = list_of(s1)(s2);
    RequestPipeline rp(ps);
}

class ShowBgpServerHandler {
public:
    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps, int stage, int instNum,
            RequestPipeline::InstData *data) {
        const ShowBgpServerReq *req =
            static_cast<const ShowBgpServerReq *>(ps.snhRequest_.get());
        BgpSandeshContext *bsc =
            static_cast<BgpSandeshContext *>(req->client_context());

        ShowBgpServerResp *resp = new ShowBgpServerResp;
        SocketIOStats peer_socket_stats;
        bsc->bgp_server->session_manager()->GetRxSocketStats(peer_socket_stats);
        resp->set_rx_socket_stats(peer_socket_stats);

        bsc->bgp_server->session_manager()->GetTxSocketStats(peer_socket_stats);
        resp->set_tx_socket_stats(peer_socket_stats);

        resp->set_context(req->context());
        resp->Response();
        return true;
    }
};

void ShowBgpServerReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);

    // Request pipeline has single stage to collect neighbor config info
    // and respond to the request
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = ShowBgpServerHandler::CallbackS1;
    s1.instances_.push_back(0);
    ps.stages_ = list_of(s1);
    RequestPipeline rp(ps);
}

BgpSandeshContext::BgpSandeshContext()
    : bgp_server(NULL),
      xmpp_peer_manager(NULL),
      test_mode_(false),
      page_limit_(0),
      iter_limit_(0) {
}

void BgpSandeshContext::SetNeighborShowExtensions(
    const NeighborListExtension &show_neighbor,
    const NeighborStatisticsExtension &show_neighbor_statistics) {
    show_neighbor_ext_ = show_neighbor;
    show_neighbor_statistics_ext_ = show_neighbor_statistics;
}

bool BgpSandeshContext::ShowNeighborExtension(const BgpSandeshContext *bsc,
    bool summary, uint32_t page_limit, uint32_t iter_limit,
    const string &start_neighbor, const string &search_string,
    vector<BgpNeighborResp> *list, string *next_neighbor) const {
    if (!show_neighbor_ext_)
        return true;
    bool done = show_neighbor_ext_(bsc, summary, page_limit, iter_limit,
        start_neighbor, search_string, list, next_neighbor);
    return done;
}

void BgpSandeshContext::ShowNeighborStatisticsExtension(
    size_t *count, const ShowNeighborStatisticsReq *req) const {
    if (!show_neighbor_statistics_ext_)
        return;
    show_neighbor_statistics_ext_(count, this, req);
}

void BgpSandeshContext::SetPeeringShowHandlers(
    const PeeringReqHandler &show_peering_req_handler,
    const PeeringReqIterateHandler &show_peering_req_iterate_handler) {
    show_peering_req_handler_ = show_peering_req_handler;
    show_peering_req_iterate_handler_ = show_peering_req_iterate_handler;
}

void BgpSandeshContext::PeeringShowReqHandler(
    const ShowBgpPeeringConfigReq *req) {
    if (show_peering_req_handler_) {
       show_peering_req_handler_(this, req);
    } else {
        ShowBgpPeeringConfigResp *resp = new ShowBgpPeeringConfigResp;
        resp->Response();
    }
}

void BgpSandeshContext::PeeringShowReqIterateHandler(
    const ShowBgpPeeringConfigReqIterate *req_iterate) {
    if (show_peering_req_iterate_handler_) {
       show_peering_req_iterate_handler_(this, req_iterate);
    } else {
        ShowBgpPeeringConfigResp *resp = new ShowBgpPeeringConfigResp;
        resp->Response();
    }
}
