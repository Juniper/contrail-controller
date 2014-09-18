/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include "base/util.h"
#include "io/tcp_server.h"
#include "control-node/control_node.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_table.h"
#include "bgp/bgp_xmpp_channel.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/inet/inet_route.h"
#include "bgp/inet/inet_table.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "db/db_table_partition.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_server.h"

using namespace boost::assign;
using namespace std;

struct ShowRouteData : public RequestPipeline::InstData {
    vector<ShowRouteTable> route_table_list;
};

class ShowRouteHandler {
public:
    struct ShowRouteData : public RequestPipeline::InstData {
        vector<ShowRouteTable> route_table_list;
    };

    ShowRouteHandler(const ShowRouteReq *req, int inst_id) :
        req_(req), inst_id_(inst_id) {}

    // Search for interesting prefixes in a given table for specified partition
    void BuildShowRouteTable(BgpTable *table,
            vector<ShowRoute> &route_list,
            int count) {
        DBTablePartition *partition =
            static_cast<DBTablePartition *>(table->GetTablePartition(inst_id_));
        BgpRoute *route;

        bool exact_lookup = false;
        if (!req_->get_prefix().empty() && !req_->get_longer_match()) {
            exact_lookup = true;
            auto_ptr<DBEntry> key = table->AllocEntryStr(req_->get_prefix());
            route = static_cast<BgpRoute *>(partition->Find(key.get()));
        } else if (table->name() == req_->get_start_routing_table()) {
            auto_ptr<DBEntry> key = table->AllocEntryStr(req_->get_start_prefix());
            route = static_cast<BgpRoute *>(partition->lower_bound(key.get()));
        } else {
            route = static_cast<BgpRoute *>(partition->GetFirst());
        }
        for (int i = 0; route && (!count || i < count);
             route = static_cast<BgpRoute *>(partition->GetNext(route)), i++) {
            if (!MatchPrefix(req_->get_prefix(), route,
                             req_->get_longer_match()))
                continue;
            ShowRoute show_route;
            route->FillRouteInfo(table, &show_route);
            route_list.push_back(show_route);
            if (exact_lookup)
                break;
        }
    }

    bool MatchPrefix(const string &expected_prefix, BgpRoute *route,
                     bool longer_match) {
        if (expected_prefix == "") return true;
        if (!longer_match) {
            return expected_prefix == route->ToString();
        }

        // Do longest prefix match.
        return route->IsMoreSpecific(expected_prefix);
    }

    bool match(const string &expected, const string &actual) {
        if (expected == "") return true;
        return expected == actual;
    }

    static RequestPipeline::InstData *CreateData(int stage) {
        return static_cast<RequestPipeline::InstData *>(new ShowRouteData);
    }

    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data);
    static bool CallbackS2(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data);
private:
    const ShowRouteReq *req_;
    int inst_id_;
};

bool ShowRouteHandler::CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
    ShowRouteData* mydata = static_cast<ShowRouteData*>(data);
    int inst_id = ps.stages_[stage].instances_[instNum];
    const ShowRouteReq *req = static_cast<const ShowRouteReq *>(ps.snhRequest_.get());
    ShowRouteHandler handler(req, inst_id);
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(req->client_context());
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();

    string exact_routing_table = req->get_routing_table();
    string exact_routing_instance;
    string start_routing_instance;
    if (exact_routing_table.empty()) {
        exact_routing_instance = req->get_routing_instance();
    } else {
        exact_routing_instance =
            RoutingInstance::GetVrfFromTableName(exact_routing_table);
    }
    if (exact_routing_instance.empty()) {
        start_routing_instance = req->get_start_routing_instance();
    } else {
        start_routing_instance = exact_routing_instance;
    }

    RoutingInstanceMgr::NameIterator i =
        rim->name_lower_bound(start_routing_instance);
    uint32_t count = 0;
    while (i != rim->name_end()) {
        if (!handler.match(exact_routing_instance, i->first))
            break;
        RoutingInstance::RouteTableList::const_iterator j;
        if (req->get_start_routing_instance() == i->first)
            j = i->second->GetTables().lower_bound(req->get_start_routing_table());
        else
            j = i->second->GetTables().begin();
        for (;j != i->second->GetTables().end(); j++) {
            BgpTable *table = j->second;
            if (!handler.match(req->get_routing_table(), table->name()))
                continue;
            ShowRouteTable srt;
            srt.set_routing_instance(i->first);
            srt.set_routing_table_name(table->name());
            srt.set_deleted(table->IsDeleted());

            // Encode routing-table stats.
            srt.prefixes = table->Size();
            srt.primary_paths = table->GetPrimaryPathCount();
            srt.secondary_paths = table->GetSecondaryPathCount();
            srt.infeasible_paths = table->GetInfeasiblePathCount();
            srt.paths = srt.primary_paths + srt.secondary_paths;

            vector<ShowRoute> route_list;
            handler.BuildShowRouteTable(table, route_list,
                req->get_count() ? req->get_count() - count : 0);
            if (route_list.size()) {
                srt.set_routes(route_list);
                mydata->route_table_list.push_back(srt);
            }
            count += route_list.size();
            if (req->get_count() && count >= req->get_count()) break;
        }
        if (req->get_count() && count >= req->get_count()) break;

        i++;
    }
    return true;
}

bool ShowRoute::operator<(const ShowRoute &rhs) const {
    return prefix < rhs.prefix;
}
bool ShowRouteTable::operator<(const ShowRouteTable &rhs) const {
    if (routing_instance < rhs.routing_instance) return true;
    if (routing_instance == rhs.routing_instance)
        return routing_table_name < rhs.routing_table_name;
    return false;
}

template <class T>
void MergeSort(vector<T> &result, vector<const vector<T> *> &input, int limit);

int MergeValues(ShowRoute &result, vector<const ShowRoute *> &input, int limit) {
    assert(input.size() == 1);
    result = *input[0];
    return 1;
}

int MergeValues(ShowRouteTable &result, vector<const ShowRouteTable *> &input,
                 int limit) {
    vector<const vector<ShowRoute> *> list;
    result.routing_instance = input[0]->routing_instance;
    result.routing_table_name = input[0]->routing_table_name;
    result.deleted = input[0]->deleted;
    result.prefixes = input[0]->prefixes;
    result.primary_paths = input[0]->primary_paths;
    result.secondary_paths = input[0]->secondary_paths;
    result.infeasible_paths = input[0]->infeasible_paths;
    result.paths = input[0]->paths;

    int count = 0;
    for (size_t i = 0; i < input.size(); i++) {
        if (input[i]->routes.size())
            list.push_back(&input[i]->routes);
    }
    MergeSort(result.routes, list, limit ? limit - count : 0);
    count += result.routes.size();
    return count;
}

// Merge n number of vectors in result. input is a vector of pointers to vector
template <class T>
void MergeSort(vector<T> &result, vector<const vector<T> *> &input, int limit) {
    size_t size = input.size();
    size_t index[size];
    bzero(index, sizeof(index));
    int count = 0;
    while (limit == 0 || count < limit) {
        size_t best_index = size;
        const T *best = NULL;
        for (size_t i = 0; i < size; i++) {
            if (index[i] == input[i]->size()) continue;
            if (best == NULL ||
                input[i]->at(index[i]) < *best) {
                best = &input[i]->at(index[i]);
                best_index = i;
                continue;
            }
        }
        if (best_index >= size) break;
        T table;
        vector<const T *> list;
        for (size_t j = best_index; j < size; j++) {
            if (index[j] == input[j]->size()) continue;
            if (input[j]->at(index[j]) < *best ||
                *best < input[j]->at(index[j]))
                continue;
            list.push_back(&input[j]->at(index[j]));
            index[j]++;
        }
        count += MergeValues(table, list, limit ? limit - count : 0);
        result.push_back(table);
    }

}

bool ShowRouteHandler::CallbackS2(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
    const ShowRouteReq *req = static_cast<const ShowRouteReq *>(ps.snhRequest_.get());
    const RequestPipeline::StageData *sd = ps.GetStageData(0);
    ShowRouteResp *resp = new ShowRouteResp;
    vector<ShowRouteTable> route_table_list;
    vector<const vector<ShowRouteTable> *> table_lists;
    for (size_t i = 0; i < sd->size(); i++) {
        const ShowRouteData &old_data = static_cast<const ShowRouteData &>(sd->at(i));
        if (old_data.route_table_list.size())
            table_lists.push_back(&old_data.route_table_list);
    }
    MergeSort(route_table_list, table_lists, req->get_count());
    resp->set_tables(route_table_list);
    resp->set_context(req->context());
    resp->Response();
    return true;
}

// handler for 'show route'
void ShowRouteReq::HandleRequest() const {
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(client_context());

    RequestPipeline::PipeSpec ps(this);

    // Request pipeline has 2 stages. In first stage, we spawn one task per
    // partition and generate the list of routes. In second stage, we look
    // at the generated list and merge it so that we can send it out
    //
    // In future, we can enhance it to have many stages and send partial output
    // after every second stage
    RequestPipeline::StageSpec s1, s2;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    s1.taskId_ = scheduler->GetTaskId("db::DBTable");
    s1.allocFn_ = ShowRouteHandler::CreateData;

    s1.cbFn_ = ShowRouteHandler::CallbackS1;
    for (int i = 0; i < bsc->bgp_server->database()->PartitionCount(); i++) {
        s1.instances_.push_back(i);
    }

    s2.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s2.allocFn_ = ShowRouteHandler::CreateData;
    s2.cbFn_ = ShowRouteHandler::CallbackS2;
    s2.instances_.push_back(0);

    ps.stages_= list_of(s1)(s2);
    RequestPipeline rp(ps);
}

class ShowNeighborHandler {
public:
    struct ShowNeighborData : public RequestPipeline::InstData {
        vector<BgpNeighborResp> nbr_list;
    };
    struct ShowNeighborDataS2Key {
        string peer;
        string table;
        bool operator<(const ShowNeighborDataS2Key&rhs) const {
            if (peer != rhs.peer) return (peer < rhs.peer);
            return (table < rhs.table);
        }
    };
    struct ShowNeighborDataS2 : public RequestPipeline::InstData {
        typedef map<ShowNeighborDataS2Key, BgpNeighborRoutingTable> Map;
        Map peers;
    };
    static RequestPipeline::InstData *CreateData(int stage) {
        switch (stage) {
        case 0:
        case 2:
            return static_cast<RequestPipeline::InstData *>(new ShowNeighborData);
        case 1:
            return static_cast<RequestPipeline::InstData *>(new ShowNeighborDataS2);
        default:
            return NULL;
        }
    }
    static void FillNeighborStats(ShowNeighborDataS2 *data, BgpTable *table, int inst_id) {
        DBTablePartition *partition =
            static_cast<DBTablePartition *>(table->GetTablePartition(inst_id));
        BgpRoute *route = static_cast<BgpRoute *>(partition->GetFirst());
        ShowNeighborDataS2Key key;
        key.table = table->name();
        for (; route; route = static_cast<BgpRoute *>(partition->GetNext(route))) {
            Route::PathList &plist = route->GetPathList();
            Route::PathList::iterator it = plist.begin();
            for (; it != plist.end(); it++) {
                BgpPath *path = static_cast<BgpPath *>(it.operator->());
                // If paths are added with Peer as NULL(e.g. aggregate route)
                if (path->GetPeer() == NULL) continue;
                key.peer = path->GetPeer()->ToString();
                BgpNeighborRoutingTable &nt = data->peers[key];
                nt.received_prefixes++;
                if (path->IsFeasible()) nt.accepted_prefixes++;
                if (route->BestPath()->GetPeer() == path->GetPeer()) nt.active_prefixes++;
            }
        }
    }
    static void FillXmppNeighborInfo(vector<BgpNeighborResp> *, BgpServer *, BgpXmppChannel *);
    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data);
    static bool CallbackS2(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data);
    static bool CallbackS3(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data);
};

void ShowNeighborHandler::FillXmppNeighborInfo(
        vector<BgpNeighborResp> *nbr_list, BgpServer *bgp_server, BgpXmppChannel *bx_channel) {
    BgpNeighborResp resp;
    resp.set_peer(bx_channel->ToString());
    resp.set_peer_address(bx_channel->remote_endpoint().address().to_string());
    resp.set_deleted(bx_channel->peer_deleted());
    resp.set_local_address(bx_channel->local_endpoint().address().to_string());
    resp.set_peer_type("internal");
    resp.set_encoding("XMPP");
    resp.set_state(bx_channel->StateName());

    const XmppConnection *connection = bx_channel->channel()->connection();
    resp.set_configured_hold_time(connection->GetConfiguredHoldTime());
    resp.set_negotiated_hold_time(connection->GetNegotiatedHoldTime());

    PeerRibMembershipManager *mgr =
        bx_channel->Peer()->server()->membership_mgr();
    mgr->FillPeerMembershipInfo(bx_channel->Peer(), resp);
    bx_channel->FillTableMembershipInfo(&resp);
    bx_channel->FillInstanceMembershipInfo(&resp);

    BgpPeer::FillBgpNeighborDebugState(resp, bx_channel->Peer()->peer_stats());
    nbr_list->push_back(resp);
}

bool ShowNeighborHandler::CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData * data) {
    ShowNeighborData* mydata = static_cast<ShowNeighborData*>(data);
    const BgpNeighborReq *req = static_cast<const BgpNeighborReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(req->client_context());
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();
    if (req->get_domain() != "") {
        RoutingInstance *ri = rim->GetRoutingInstance(req->get_domain());
        if (ri)
            ri->peer_manager()->FillBgpNeighborInfo(mydata->nbr_list,
                req->get_ip_address());
    } else {
        RoutingInstanceMgr::RoutingInstanceIterator it = rim->begin();
        for (;it != rim->end(); it++) {
            it->peer_manager()->FillBgpNeighborInfo(mydata->nbr_list,
                req->get_ip_address());
        }
    }

    bsc->xmpp_peer_manager->VisitChannels(boost::bind(FillXmppNeighborInfo,
                                                      &mydata->nbr_list, bsc->bgp_server, _1));

    return true;
}

bool ShowNeighborHandler::CallbackS2(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
    ShowNeighborDataS2* mydata = static_cast<ShowNeighborDataS2*>(data);
    int inst_id = ps.stages_[stage].instances_[instNum];
    const BgpNeighborReq *req = static_cast<const BgpNeighborReq *>(ps.snhRequest_.get());
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(req->client_context());
    RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();
    RoutingInstanceMgr::NameIterator i = rim->name_begin();
    for (;i != rim->name_end(); i++) {
        RoutingInstance::RouteTableList::const_iterator j =
                i->second->GetTables().begin();
        for (;j != i->second->GetTables().end(); j++) {
            FillNeighborStats(mydata, j->second, inst_id);
        }
    }
    return true;
}

bool ShowNeighborHandler::CallbackS3(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
    const BgpNeighborReq *req = static_cast<const BgpNeighborReq *>(ps.snhRequest_.get());
    const RequestPipeline::StageData *sd[2] = { ps.GetStageData(0), ps.GetStageData(1) };
    const ShowNeighborData &nbrs =  static_cast<const ShowNeighborData &>(sd[0]->at(0));

    vector<BgpNeighborResp> nbr_list = nbrs.nbr_list;

    for (size_t i = 0; i < nbrs.nbr_list.size(); i++) {
        ShowNeighborDataS2Key key;
        key.peer = nbrs.nbr_list[i].peer;
        for (size_t j = 0; j < nbrs.nbr_list[i].routing_tables.size(); j++) {
            key.table = nbrs.nbr_list[i].routing_tables[j].name;
            for (size_t k = 0; k < sd[1]->size(); k++) {
                const ShowNeighborDataS2 &stats_data =
                        static_cast<const ShowNeighborDataS2 &>(sd[1]->at(k));
                ShowNeighborDataS2::Map::const_iterator it = stats_data.peers.find(key);
                if (it == stats_data.peers.end()) continue;
                nbr_list[i].routing_tables[j].active_prefixes +=
                        it->second.active_prefixes;
                nbr_list[i].routing_tables[j].received_prefixes +=
                        it->second.received_prefixes;
                nbr_list[i].routing_tables[j].accepted_prefixes +=
                        it->second.accepted_prefixes;
            }
        }
    }

    BgpNeighborListResp *resp = new BgpNeighborListResp;
    resp->set_neighbors(nbrs.nbr_list);
    resp->set_context(req->context());
    resp->Response();
    return true;
}

class ShowNeighborStatisticsHandler {
public:
    static void FillXmppNeighborStatistics(size_t *count, BgpServer *bgp_server,
                                           string domain, string up_or_down,
                                           BgpXmppChannel *channel);
    static bool CallbackS1(const Sandesh *sr,
                           const RequestPipeline::PipeSpec ps, int stage,
                           int instNum, RequestPipeline::InstData * data);
    static size_t FillBgpNeighborStatistics(const ShowNeighborStatisticsReq *req,
                                   BgpServer *bgp_server);
};

void ShowNeighborStatisticsHandler::FillXmppNeighborStatistics(
        size_t *count, BgpServer *bgp_server, string domain,
        string up_or_down, BgpXmppChannel *channel) {

    if (boost::iequals(up_or_down, "up") && !channel->Peer()->IsReady()) {
        return;
    }
    if (boost::iequals(up_or_down, "down") && channel->Peer()->IsReady()) {
        return;
    }

    if (!domain.empty()) {
        RoutingInstanceMgr *rim = bgp_server->routing_instance_mgr();
        RoutingInstance *ri = rim->GetRoutingInstance(domain);
        if (!ri) return;
        BgpTable *table = ri->GetTable(Address::INET);
        if (!table) return;

        if (!bgp_server->membership_mgr()->IPeerRibFind(channel->Peer(),
                                                        table)) {
            return;
        }
    }

    ++*count;
}

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
        bsc->xmpp_peer_manager->VisitChannels(
                boost::bind(FillXmppNeighborStatistics, &count,
                            bsc->bgp_server, req->get_domain(),
                            req->get_up_or_down(), _1));
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

// handler for 'show bgp neighbor'
void BgpNeighborReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(client_context());

    // Request pipeline has 3 stages:
    // First stage to collect neighbor info
    // Second stage to collect stats
    // Third stage to collate all data and respond to the request
    RequestPipeline::StageSpec s1, s2, s3;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    s1.taskId_ = scheduler->GetTaskId("bgp::PeerMembership");
    s1.allocFn_ = ShowNeighborHandler::CreateData;
    s1.cbFn_ = ShowNeighborHandler::CallbackS1;
    s1.instances_.push_back(
            PeerRibMembershipManager::kMembershipTaskInstanceId);

    s2.taskId_ = scheduler->GetTaskId("db::DBTable");
    s2.allocFn_ = ShowNeighborHandler::CreateData;
    s2.cbFn_ = ShowNeighborHandler::CallbackS2;
    for (int i = 0; i < bsc->bgp_server->database()->PartitionCount(); i++) {
        s2.instances_.push_back(i);
    }

    s3.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s3.allocFn_ = ShowNeighborHandler::CreateData;
    s3.cbFn_ = ShowNeighborHandler::CallbackS3;
    s3.instances_.push_back(0);
    ps.stages_= list_of(s1)(s2)(s3);
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
    if (peer) {
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

    if (ControlNode::GetTestMode() == false) {
        ClearBgpNeighborResp *resp = new ClearBgpNeighborResp;
        resp->set_context(context());
        resp->set_more(false);
        resp->Response();
        return;
    }

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

class ShowRoutingInstanceHandler {
public:
    static void FillRoutingTableStats(ShowRoutingInstanceTable &rit,
                                      BgpTable *table) {
        rit.set_name(table->name());
        rit.set_walk_requests(
            table->database()->GetWalker()->walk_request_count());
        rit.set_walk_completes(
            table->database()->GetWalker()->walk_complete_count());
        rit.set_walk_cancels(
            table->database()->GetWalker()->walk_cancel_count());
        size_t markers;
        rit.set_pending_updates(table->GetPendingRiboutsCount(markers));
        rit.set_markers(markers);
        rit.prefixes = table->Size();
        rit.primary_paths = table->GetPrimaryPathCount();
        rit.secondary_paths = table->GetSecondaryPathCount();
        rit.infeasible_paths = table->GetInfeasiblePathCount();
        rit.paths = rit.primary_paths + rit.secondary_paths;
    }

    static void FillRoutingInstanceInfo(const RequestPipeline::StageData *sd,
            vector<ShowRoutingInstance> &ri_list,
            RoutingInstance *ri, PeerRibMembershipManager *pmm) {
        const RoutingInstance::RouteTableList &tables = ri->GetTables();
        RoutingInstance::RouteTableList::const_iterator it = tables.begin();
        ShowRoutingInstance inst;
        vector<ShowRoutingInstanceTable> rit_list;
        for (; it != tables.end(); it++) {
            ShowRoutingInstanceTable table;
            pmm->FillRoutingInstanceInfo(table, it->second);
            FillRoutingTableStats(table, it->second);
            rit_list.push_back(table);
        }
        if (rit_list.size()) {
            inst.set_name(ri->name());
            inst.set_virtual_network(ri->virtual_network());
            inst.set_vn_index(ri->virtual_network_index());
            inst.set_deleted(ri->deleted());
            std::vector<std::string> import_rt;
            BOOST_FOREACH(RouteTarget rt, ri->GetImportList()) {
                import_rt.push_back(rt.ToString());
            }
            inst.set_import_target(import_rt);

            std::vector<std::string> export_rt;
            BOOST_FOREACH(RouteTarget rt, ri->GetExportList()) {
                export_rt.push_back(rt.ToString());
            }
            inst.set_export_target(export_rt);
            inst.set_tables(rit_list);
            ri_list.push_back(inst);
        }
    }

    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
        const ShowRoutingInstanceReq *req =
            static_cast<const ShowRoutingInstanceReq *>(ps.snhRequest_.get());
        BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(
                                                 req->client_context());
        RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();
        PeerRibMembershipManager *pmm = bsc->bgp_server->membership_mgr();
        vector<ShowRoutingInstance> ri_list;
        const RequestPipeline::StageData *sd = ps.GetStageData(0);
        if (req->get_name() != "") {
            RoutingInstance *ri = rim->GetRoutingInstance(req->get_name());
            if (ri)
                FillRoutingInstanceInfo(sd, ri_list, ri, pmm);
        } else {
            RoutingInstanceMgr::RoutingInstanceIterator it = rim->begin();
            for (;it != rim->end(); it++) {
                FillRoutingInstanceInfo(sd, ri_list, &(*it), pmm);
            }
        }

        ShowRoutingInstanceResp *resp = new ShowRoutingInstanceResp;
        resp->set_instances(ri_list);
        resp->set_context(req->context());
        resp->Response();
        return true;
    }
};

void ShowRoutingInstanceReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);

    // Request pipeline has 2 stages:
    // First stage to collect routing instance stats
    // Second stage to collect peer info and fill stats from stage 1 and
    // respond to the request
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = ShowRoutingInstanceHandler::CallbackS1;
    s1.instances_.push_back(0);
    ps.stages_ = list_of(s1);
    RequestPipeline rp(ps);
}

class ShowMulticastManagerHandler {
public:
    struct MulticastManagerDataKey {
        string routing_table;
        bool operator<(const MulticastManagerDataKey &rhs) const {
            return (routing_table < rhs.routing_table);
        }
    };

    struct MulticastManagerData : public RequestPipeline::InstData {
        typedef map<MulticastManagerDataKey, uint32_t> Map;
        Map table_map;
    };

    static RequestPipeline::InstData *CreateData(int stage) {
        return (new MulticastManagerData);
    }

    static void FillMulticastManagerStats(MulticastManagerData *data,
            ErmVpnTable *table, int inst_id) {
        MulticastManagerDataKey key;
        key.routing_table = table->name();
        McastTreeManager *tm = table->GetTreeManager();
        McastManagerPartition *partition = tm->GetPartition(inst_id);
        data->table_map[key] = partition->size();
    }

    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
        int inst_id = ps.stages_[stage].instances_[instNum];

        MulticastManagerData *mydata =
            static_cast<MulticastManagerData *>(data);
        const ShowMulticastManagerReq *req =
            static_cast<const ShowMulticastManagerReq *>(ps.snhRequest_.get());
        BgpSandeshContext *bsc =
            static_cast<BgpSandeshContext *>(req->client_context());
        RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();
        for (RoutingInstanceMgr::NameIterator it = rim->name_begin();
             it != rim->name_end(); it++) {
            RoutingInstance *ri = it->second;
            ErmVpnTable *table =
                static_cast<ErmVpnTable *>(ri->GetTable(Address::ERMVPN));
            if (table)
                FillMulticastManagerStats(mydata, table, inst_id);
        }

        return true;
    }

    static void FillMulticastManagerInfo(const RequestPipeline::StageData *sd,
            vector<ShowMulticastManager> &mgr_list, ErmVpnTable *table) {
        ShowMulticastManager mgr;
        MulticastManagerDataKey key;
        key.routing_table = table->name();
        for (size_t idx = 0; idx < sd->size(); idx++) {
            const MulticastManagerData &data =
                static_cast<const MulticastManagerData &>(sd->at(idx));
            MulticastManagerData::Map::const_iterator dit =
                data.table_map.find(key);
            if (dit != data.table_map.end()) {
                mgr.total_trees += dit->second;
            }
        }

        mgr.set_name(table->name());
        mgr_list.push_back(mgr);
    }

    static bool CallbackS2(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
        const ShowMulticastManagerReq *req =
            static_cast<const ShowMulticastManagerReq *>(ps.snhRequest_.get());
        BgpSandeshContext *bsc =
            static_cast<BgpSandeshContext *>(req->client_context());
        RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();
        vector<ShowMulticastManager> mgr_list;
        const RequestPipeline::StageData *sd = ps.GetStageData(0);
        for (RoutingInstanceMgr::NameIterator it = rim->name_begin();
             it != rim->name_end(); it++) {
            RoutingInstance *ri = it->second;
            ErmVpnTable *table =
                static_cast<ErmVpnTable *>(ri->GetTable(Address::ERMVPN));
            if (table)
                FillMulticastManagerInfo(sd, mgr_list, table);
        }


        ShowMulticastManagerResp *resp = new ShowMulticastManagerResp;
        resp->set_managers(mgr_list);
        resp->set_context(req->context());
        resp->Response();
        return true;
    }
};

void ShowMulticastManagerReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(client_context());

    // Request pipeline has 2 stages.
    // First stage to collect multicast manager stats.
    // Second stage to fill stats from stage 1 and respond to the request.
    RequestPipeline::StageSpec s1, s2;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("db::DBTable");
    s1.allocFn_ = ShowMulticastManagerHandler::CreateData;
    s1.cbFn_ = ShowMulticastManagerHandler::CallbackS1;
    for (int i = 0; i < bsc->bgp_server->database()->PartitionCount(); i++) {
        s1.instances_.push_back(i);
    }

    s2.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s2.cbFn_ = ShowMulticastManagerHandler::CallbackS2;
    s2.instances_.push_back(0);

    ps.stages_ = list_of(s1)(s2);
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

    static void FillMulticastLinkInfo(ShowMulticastForwarder *fwd,
            const McastForwarder *forwarder) {
        ShowMulticastTreeLink link;
        link.set_address(forwarder->address().to_string());
        link.set_label(forwarder->label());
        fwd->links.push_back(link);
    }

    static void FillMulticastForwarderInfo(ShowMulticastTree *tree,
            const McastForwarder *forwarder) {
        ShowMulticastForwarder fwd;
        fwd.set_address(forwarder->address().to_string());
        fwd.set_label(forwarder->label());
        for (McastForwarderList::const_iterator it =
             forwarder->tree_links_.begin();
             it != forwarder->tree_links_.end(); it++) {
            FillMulticastLinkInfo(&fwd, *it);
        }
        tree->forwarders.push_back(fwd);
    }

    static void FillMulticastTreeInfo(MulticastManagerDetailData *data,
            const McastSGEntry *sg) {
        ShowMulticastTree tree;
        tree.set_group(sg->group().to_string());
        tree.set_source(sg->source().to_string());
        for (McastSGEntry::ForwarderSet::const_iterator it =
             sg->forwarder_sets_[0]->begin();
             it != sg->forwarder_sets_[0]->end(); it++) {
            FillMulticastForwarderInfo(&tree, *it);
        }
        data->tree_list.push_back(tree);
    }

    static void FillMulticastPartitionInfo(MulticastManagerDetailData *data,
            ErmVpnTable *table, int inst_id) {
        McastTreeManager *tm = table->GetTreeManager();
        McastManagerPartition *partition = tm->GetPartition(inst_id);
        for (McastManagerPartition::SGList::const_iterator it =
             partition->sg_list_.begin();
             it != partition->sg_list_.end(); it++) {
            FillMulticastTreeInfo(data, *it);
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
        if (mcast_table)
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

class ShowBgpInstanceConfigHandler {
public:
    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
        const ShowBgpInstanceConfigReq *req =
            static_cast<const ShowBgpInstanceConfigReq *>(ps.snhRequest_.get());
        BgpSandeshContext *bsc =
            static_cast<BgpSandeshContext *>(req->client_context());
        BgpConfigManager *bcm = bsc->bgp_server->config_manager();
        const BgpConfigData &bcd = bcm->config();

        vector<ShowBgpInstanceConfig> ri_list;
        for (BgpConfigData::BgpInstanceMap::const_iterator loc = bcd.instances().begin();
             loc != bcd.instances().end(); ++loc) {
            ShowBgpInstanceConfig inst;
            inst.set_name(loc->second->name());
            inst.set_virtual_network(loc->second->virtual_network());
            inst.set_virtual_network_index(loc->second->virtual_network_index());

            std::vector<std::string> import_list;
            BOOST_FOREACH(std::string rt, loc->second->import_list()) {
                import_list.push_back(rt);
            }
            inst.set_import_target(import_list);

            std::vector<std::string> export_list;
            BOOST_FOREACH(std::string rt, loc->second->export_list()) {
                export_list.push_back(rt);
            }
            inst.set_export_target(export_list);

            const autogen::RoutingInstance *rti = loc->second->instance_config();
            if (rti && rti->IsPropertySet(autogen::RoutingInstance::SERVICE_CHAIN_INFORMATION)) {
                const autogen::ServiceChainInfo &sci = rti->service_chain_information();
                ShowBgpServiceChainConfig scc;
                scc.set_routing_instance(sci.routing_instance);
                scc.set_service_instance(sci.service_instance);
                scc.set_chain_address(sci.service_chain_address);
                scc.set_prefixes(sci.prefix);
                inst.set_service_chain_info(scc);
            }
            if (rti && rti->IsPropertySet(autogen::RoutingInstance::STATIC_ROUTE_ENTRIES)) {
                std::vector<ShowBgpStaticRouteConfig> static_route_list;
                const std::vector<autogen::StaticRouteType> &cfg_list =
                    rti->static_route_entries();
                for (std::vector<autogen::StaticRouteType>::const_iterator 
                     static_it = cfg_list.begin(); static_it != cfg_list.end();
                     static_it++) {
                    ShowBgpStaticRouteConfig src;
                    src.set_prefix(static_it->prefix);
                    src.set_targets(static_it->route_target);
                    src.set_nexthop(static_it->prefix);
                    static_route_list.push_back(src);
                }
                inst.set_static_routes(static_route_list);
            }

            ri_list.push_back(inst);
        }

        ShowBgpInstanceConfigResp *resp = new ShowBgpInstanceConfigResp;
        resp->set_instances(ri_list);
        resp->set_context(req->context());
        resp->Response();
        return true;
    }
};

void ShowBgpInstanceConfigReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);

    // Request pipeline has single stage to collect instance config info
    // and respond to the request
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = ShowBgpInstanceConfigHandler::CallbackS1;
    s1.instances_.push_back(0);
    ps.stages_ = list_of(s1);
    RequestPipeline rp(ps);
}

class ShowBgpPeeringConfigHandler {
public:
    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
        const ShowBgpPeeringConfigReq *req =
            static_cast<const ShowBgpPeeringConfigReq *>(ps.snhRequest_.get());
        BgpSandeshContext *bsc =
            static_cast<BgpSandeshContext *>(req->client_context());
        BgpConfigManager *bcm = bsc->bgp_server->config_manager();
        const BgpConfigData &bcd = bcm->config();

        vector<ShowBgpPeeringConfig> peering_list;
        for (BgpConfigData::BgpPeeringMap::const_iterator loc = bcd.peerings().begin();
             loc != bcd.peerings().end(); ++loc) {

            ShowBgpPeeringConfig peering;
            peering.set_instance_name(loc->second->instance()->name());
            peering.set_name(loc->second->name());
            peering.set_neighbor_count(loc->second->size());

            if (!loc->second->bgp_peering()) {
                peering_list.push_back(peering);
                continue;
            }

            vector<ShowBgpSessionConfig> session_list;
            const autogen::BgpPeeringAttributes &attr = loc->second->bgp_peering()->data();
            for (autogen::BgpPeeringAttributes::const_iterator iter1 = attr.begin();
                 iter1 != attr.end(); ++iter1) {
                ShowBgpSessionConfig session;
                session.set_uuid(iter1->uuid);

                vector<ShowBgpSessionAttributesConfig> attribute_list;
                for (std::vector<autogen::BgpSessionAttributes>::const_iterator iter2 =
                     iter1->attributes.begin();
                     iter2 != iter1->attributes.end(); ++iter2) {
                    ShowBgpSessionAttributesConfig attribute;

                    attribute.set_bgp_router(iter2->bgp_router);
                    attribute.set_address_families(iter2->address_families.family);
                    attribute_list.push_back(attribute);
                }

                session.set_attributes(attribute_list);
                session_list.push_back(session);
            }

            peering.set_sessions(session_list);
            peering_list.push_back(peering);
        }

        ShowBgpPeeringConfigResp *resp = new ShowBgpPeeringConfigResp;
        resp->set_peerings(peering_list);
        resp->set_context(req->context());
        resp->Response();
        return true;
    }
};

void ShowBgpPeeringConfigReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);

    // Request pipeline has single stage to collect peering config info
    // and respond to the request
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = ShowBgpPeeringConfigHandler::CallbackS1;
    s1.instances_.push_back(0);
    ps.stages_ = list_of(s1);
    RequestPipeline rp(ps);
}

class ShowBgpNeighborConfigHandler {
public:
    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum,
            RequestPipeline::InstData *data) {
        const ShowBgpNeighborConfigReq *req =
            static_cast<const ShowBgpNeighborConfigReq *>(ps.snhRequest_.get());
        BgpSandeshContext *bsc =
            static_cast<BgpSandeshContext *>(req->client_context());
        BgpConfigManager *bcm = bsc->bgp_server->config_manager();
        const BgpConfigData &bcd = bcm->config();

        vector<ShowBgpNeighborConfig> nbr_list;
        for (BgpConfigData::BgpInstanceMap::const_iterator loc1 =
             bcd.instances().begin();
             loc1 != bcd.instances().end(); ++loc1) {

            const BgpInstanceConfig *bic = loc1->second;
            if (!bic->neighbors().size())
                continue;

            for (BgpInstanceConfig::NeighborMap::const_iterator loc2 =
                 bic->neighbors().begin();
                 loc2 != bic->neighbors().end(); ++loc2) {
                const autogen::BgpRouterParams &peer = loc2->second->peer_config();

                ShowBgpNeighborConfig nbr;
                nbr.set_instance_name(loc2->second->InstanceName());
                nbr.set_name(loc2->second->name());
                nbr.set_local_identifier(loc2->second->local_identifier());
                nbr.set_local_as(loc2->second->local_as());
                nbr.set_vendor(peer.vendor);
                nbr.set_autonomous_system(peer.autonomous_system);
                nbr.set_identifier(peer.identifier);
                nbr.set_address(peer.address);
                nbr.set_address_families(loc2->second->address_families());

                nbr_list.push_back(nbr);
            }
        }

        ShowBgpNeighborConfigResp *resp = new ShowBgpNeighborConfigResp;
        resp->set_neighbors(nbr_list);
        resp->set_context(req->context());
        resp->Response();
        return true;
    }
};

void ShowBgpNeighborConfigReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);

    // Request pipeline has single stage to collect neighbor config info
    // and respond to the request
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = ShowBgpNeighborConfigHandler::CallbackS1;
    s1.instances_.push_back(0);
    ps.stages_ = list_of(s1);
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

class ShowXmppServerHandler {
public:
    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps, int stage, int instNum,
            RequestPipeline::InstData *data) {
        const ShowXmppServerReq *req =
            static_cast<const ShowXmppServerReq *>(ps.snhRequest_.get());
        BgpSandeshContext *bsc =
            static_cast<BgpSandeshContext *>(req->client_context());

        ShowXmppServerResp *resp = new ShowXmppServerResp;
        SocketIOStats peer_socket_stats;
        bsc->xmpp_peer_manager->xmpp_server()->GetRxSocketStats(
                         peer_socket_stats);
        resp->set_rx_socket_stats(peer_socket_stats);

        bsc->xmpp_peer_manager->xmpp_server()->GetTxSocketStats(
                         peer_socket_stats);
        resp->set_tx_socket_stats(peer_socket_stats);

        resp->set_context(req->context());
        resp->Response();
        return true;
    }
};

void ShowXmppServerReq::HandleRequest() const {
    RequestPipeline::PipeSpec ps(this);

    // Request pipeline has single stage to collect neighbor config info
    // and respond to the request
    RequestPipeline::StageSpec s1;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    s1.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s1.cbFn_ = ShowXmppServerHandler::CallbackS1;
    s1.instances_.push_back(0);
    ps.stages_ = list_of(s1);
    RequestPipeline rp(ps);
}

class ClearComputeNodeHandler {
public:
    static bool CallbackS1(const Sandesh *sr,
        const RequestPipeline::PipeSpec ps, int stage, int instNum,
        RequestPipeline::InstData *data) {

        const ClearComputeNodeConnection *req =
            static_cast<const ClearComputeNodeConnection *>(ps.snhRequest_.get());
        BgpSandeshContext *bsc =
            static_cast<BgpSandeshContext *>(req->client_context());
        XmppServer *server = bsc->xmpp_peer_manager->xmpp_server();

        ClearComputeNodeConnectionResp *resp = new ClearComputeNodeConnectionResp;
        if (req->get_hostname_or_all() != "all") {
            if (server->ClearConnection(req->get_hostname_or_all())) {
                resp->set_sucess(true);
            } else {
                resp->set_sucess(false);
            }
        } else {
            if (server->ConnectionCount()) {
                server->ClearAllConnections();
                resp->set_sucess(true);
            } else {
                resp->set_sucess(false);
            }
        }

        resp->set_context(req->context());
        resp->Response();
        return(true);
    }
};

void ClearComputeNodeConnection::HandleRequest() const {

    if (ControlNode::GetTestMode() == false) {
        ClearComputeNodeConnectionResp *resp = new ClearComputeNodeConnectionResp;
        resp->set_context(context());
        resp->set_more(false);
        resp->Response();
        return;
    }

    // config task is used to create and delete connection objects.
    // hence use the same task to find the connection
    RequestPipeline::StageSpec s1;
    s1.taskId_ = TaskScheduler::GetInstance()->GetTaskId("bgp::Config");
    s1.instances_.push_back(0);
    s1.cbFn_ = ClearComputeNodeHandler::CallbackS1;

    RequestPipeline::PipeSpec ps(this);
    ps.stages_= list_of(s1);
    RequestPipeline rp(ps);
}
