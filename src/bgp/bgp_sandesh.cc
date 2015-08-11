/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_sandesh.h"

#include <boost/assign/list_of.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/foreach.hpp>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include "base/time_util.h"
#include "base/util.h"
#include "bgp/bgp_config.h"
#include "bgp/bgp_multicast.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_session_manager.h"
#include "bgp/bgp_table.h"
#include "bgp/ermvpn/ermvpn_table.h"
#include "bgp/inet/inet_route.h"
#include "bgp/inet/inet_table.h"
#include "bgp/origin-vn/origin_vn.h"
#include "bgp/routing-instance/peer_manager.h"
#include "bgp/routing-instance/routing_instance.h"
#include "bgp/security_group/security_group.h"
#include "bgp/tunnel_encap/tunnel_encap.h"
#include "control-node/control_node.h"
#include "db/db_table_partition.h"
#include "io/tcp_server.h"

using namespace boost::assign;
using namespace std;

struct ShowRouteData : public RequestPipeline::InstData {
    vector<ShowRouteTable> route_table_list;
};

const string kShowRouteIterSeparator = "||";

class ShowRouteHandler {
public:
    // kMaxCount can be a function of 'count' field in ShowRouteReq
    static const uint32_t kUnitTestMaxCount = 100;
    static const uint32_t kMaxCount = 1000;
    static uint32_t GetMaxCount(bool test_mode) {
        if (test_mode) {
            return kUnitTestMaxCount;
        } else {
            return kMaxCount;
        }
    }

    struct ShowRouteData : public RequestPipeline::InstData {
        vector<ShowRouteTable> route_table_list;
    };

    ShowRouteHandler(const ShowRouteReq *req, int inst_id) :
        req_(req), inst_id_(inst_id) {}

    // Search for interesting prefixes in a given table for specified partition
    void BuildShowRouteTable(BgpTable *table, vector<ShowRoute> &route_list,
                             int count) {
        DBTablePartition *partition =
            static_cast<DBTablePartition *>(table->GetTablePartition(inst_id_));
        BgpRoute *route = NULL;

        bool exact_lookup = false;
        if (!req_->get_prefix().empty() && !req_->get_longer_match()) {
            exact_lookup = true;
            auto_ptr<DBEntry> key = table->AllocEntryStr(req_->get_prefix());
            route = static_cast<BgpRoute *>(partition->Find(key.get()));
        } else if (table->name() == req_->get_start_routing_table()) {
            auto_ptr<DBEntry> key =
                table->AllocEntryStr(req_->get_start_prefix());
            route = static_cast<BgpRoute *>(partition->lower_bound(key.get()));
        } else {
            route = static_cast<BgpRoute *>(partition->GetFirst());
        }
        for (int i = 0; route && (!count || i < count);
             route = static_cast<BgpRoute *>(partition->GetNext(route)), ++i) {
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

    static bool CallbackS1Common(const ShowRouteReq *req, int inst_id,
                                 ShowRouteData* mydata);
    static void CallbackS2Common(const ShowRouteReq *req,
                                 const RequestPipeline::PipeSpec ps,
                                 ShowRouteResp *resp);

    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum, RequestPipeline::InstData *data);
    static bool CallbackS2(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum, RequestPipeline::InstData *data);

    static bool CallbackS1Iterate(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps, int stage, int instNum,
            RequestPipeline::InstData *data);
    static bool CallbackS2Iterate(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps, int stage, int instNum,
            RequestPipeline::InstData *data);

    static string SaveContextAndPopLast(const ShowRouteReq *req,
            vector<ShowRouteTable> *route_table_list);
    static bool ConvertReqIterateToReq(const ShowRouteReqIterate *req_iterate,
                                       ShowRouteReq *req);
    static uint32_t GetMaxRouteCount(const ShowRouteReq *req);

private:
    const ShowRouteReq *req_;
    int inst_id_;
};

uint32_t ShowRouteHandler::GetMaxRouteCount(const ShowRouteReq *req) {
    BgpSandeshContext *bsc =
        static_cast<BgpSandeshContext *>(req->client_context());
    uint32_t max_count =
        ShowRouteHandler::GetMaxCount(bsc->test_mode());

    uint32_t route_count = req->get_count() + 1;
    if (!req->get_count() || (req->get_count() > max_count)) {
        // Set the count to our default if input is zero or too high
        route_count = max_count + 1;
    }
    return route_count;
}

// Get the information from req_iterate and fill into req
bool ShowRouteHandler::ConvertReqIterateToReq(const ShowRouteReqIterate *req_iterate,
                                              ShowRouteReq *req) {
    // First, set the context from the original request since we might return
    // due to parsing errors.
    req->set_context(req_iterate->context());

    // Format of route_info:
    // UserRI||UserRT||UserPfx||NextRI||NextRT||NextPfx||count||longer_match
    //
    // User* values were entered by the user and Next* values indicate 'where'
    // we need to start this iteration.
    string route_info = req_iterate->get_route_info();
    size_t sep_size = kShowRouteIterSeparator.size();

    size_t pos1 = route_info.find(kShowRouteIterSeparator);
    if (pos1 == string::npos) {
        return false;
    }
    string user_ri = route_info.substr(0, pos1);

    size_t pos2 = route_info.find(kShowRouteIterSeparator, (pos1 + sep_size));
    if (pos2 == string::npos) {
        return false;
    }
    string user_rt = route_info.substr((pos1 + sep_size),
                                       pos2 - (pos1 + sep_size));

    size_t pos3 = route_info.find(kShowRouteIterSeparator, (pos2 + sep_size));
    if (pos3 == string::npos) {
        return false;
    }
    string user_prefix = route_info.substr((pos2 + sep_size),
                                           pos3 - (pos2 + sep_size));

    size_t pos4 = route_info.find(kShowRouteIterSeparator, (pos3 + sep_size));
    if (pos4 == string::npos) {
        return false;
    }
    string next_ri = route_info.substr((pos3 + sep_size),
                                       pos4 - (pos3 + sep_size));

    size_t pos5 = route_info.find(kShowRouteIterSeparator, (pos4 + sep_size));
    if (pos5 == string::npos) {
        return false;
    }
    string next_rt = route_info.substr((pos4 + sep_size),
                                       pos5 - (pos4 + sep_size));

    size_t pos6 = route_info.find(kShowRouteIterSeparator, (pos5 + sep_size));
    if (pos6 == string::npos) {
        return false;
    }
    string next_prefix = route_info.substr((pos5 + sep_size),
                                           pos6 - (pos5 + sep_size));

    size_t pos7 = route_info.find(kShowRouteIterSeparator, (pos6 + sep_size));
    if (pos7 == string::npos) {
        return false;
    }
    string count_str = route_info.substr((pos6 + sep_size),
                                         pos7 - (pos6 + sep_size));

    string longer_match = route_info.substr(pos7 + sep_size);

    req->set_routing_instance(user_ri);
    req->set_routing_table(user_rt);
    req->set_prefix(user_prefix);
    req->set_start_routing_instance(next_ri);
    req->set_start_routing_table(next_rt);
    req->set_start_prefix(next_prefix);
    req->set_count(atoi(count_str.c_str()));
    req->set_longer_match(StringToBool(longer_match));

    return true;
}

bool ShowRouteHandler::CallbackS1Common(const ShowRouteReq *req, int inst_id,
                                        ShowRouteData* mydata) {
    uint32_t max_count = ShowRouteHandler::GetMaxRouteCount(req);

    ShowRouteHandler handler(req, inst_id);
    BgpSandeshContext *bsc =
        static_cast<BgpSandeshContext *>(req->client_context());
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

    RoutingInstanceMgr::name_iterator i =
        rim->name_lower_bound(start_routing_instance);
    uint32_t count = 0;
    while (i != rim->name_end()) {
        if (!handler.match(exact_routing_instance, i->first)) {
            break;
        }
        RoutingInstance::RouteTableList::const_iterator j;
        if (req->get_start_routing_instance() == i->first) {
            j = i->second->GetTables().
                                lower_bound(req->get_start_routing_table());
        } else {
            j = i->second->GetTables().begin();
        }
        for (; j != i->second->GetTables().end(); ++j) {
            BgpTable *table = j->second;
            if (!handler.match(req->get_routing_table(), table->name())) {
                continue;
            }
            ShowRouteTable srt;
            srt.set_routing_instance(i->first);
            srt.set_routing_table_name(table->name());
            srt.set_deleted(table->IsDeleted());
            srt.set_deleted_at(
                UTCUsecToString(table->deleter()->delete_time_stamp_usecs()));

            // Encode routing-table stats.
            srt.prefixes = table->Size();
            srt.primary_paths = table->GetPrimaryPathCount();
            srt.secondary_paths = table->GetSecondaryPathCount();
            srt.infeasible_paths = table->GetInfeasiblePathCount();
            srt.paths = srt.primary_paths + srt.secondary_paths;

            vector<ShowTableListener> listeners;
            table->FillListeners(&listeners);
            srt.set_listeners(listeners);

            vector<ShowRoute> route_list;
            handler.BuildShowRouteTable(table, route_list,
                                        max_count ? max_count - count : 0);
            if (route_list.size() || table->IsDeleted()) {
                srt.set_routes(route_list);
                mydata->route_table_list.push_back(srt);
            }
            count += route_list.size();
            if (count >= max_count) {
                break;
            }
        }
        if (count >= max_count) {
            break;
        }

        i++;
    }

    return true;
}

bool ShowRouteHandler::CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum, RequestPipeline::InstData *data) {
    ShowRouteData* mydata = static_cast<ShowRouteData*>(data);
    int inst_id = ps.stages_[stage].instances_[instNum];
    const ShowRouteReq *req =
        static_cast<const ShowRouteReq *>(ps.snhRequest_.get());

    return CallbackS1Common(req, inst_id, mydata);
}

bool ShowRouteHandler::CallbackS1Iterate(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum, RequestPipeline::InstData *data) {
    ShowRouteData* mydata = static_cast<ShowRouteData*>(data);
    int inst_id = ps.stages_[stage].instances_[instNum];
    const ShowRouteReqIterate *req_iterate =
        static_cast<const ShowRouteReqIterate *>(ps.snhRequest_.get());

    ShowRouteReq *req = new ShowRouteReq;
    bool success = ConvertReqIterateToReq(req_iterate, req);
    if (success) {
        CallbackS1Common(req, inst_id, mydata);
    }
    req->Release();
    return true;
}

bool ItemIsLess(const ShowRoute &lhs, const ShowRoute &rhs,
                const BgpSandeshContext *bsc, const string &table_name) {
    BgpTable *table = static_cast<BgpTable *>
        (bsc->bgp_server->database()->FindTable(table_name));
    if (!table) {
        return false;
    }
    auto_ptr<DBEntry> lhs_entry = table->AllocEntryStr(lhs.prefix);
    auto_ptr<DBEntry> rhs_entry = table->AllocEntryStr(rhs.prefix);

    Route *lhs_route = static_cast<Route *>(lhs_entry.get());
    Route *rhs_route = static_cast<Route *>(rhs_entry.get());

    return lhs_route->IsLess(*rhs_route);
}

bool ItemIsLess(const ShowRouteTable &lhs, const ShowRouteTable &rhs,
                const BgpSandeshContext *bsc, const string &table_name) {
    if (lhs.routing_instance < rhs.routing_instance) {
        return true;
    }
    if (lhs.routing_instance == rhs.routing_instance) {
        return lhs.routing_table_name < rhs.routing_table_name;
    }
    return false;
}

template <class T>
void MergeSort(vector<T> &result, vector<const vector<T> *> &input, int limit,
               const BgpSandeshContext *bsc, const string &table_name);

int MergeValues(ShowRoute &result, vector<const ShowRoute *> &input,
                int limit, const BgpSandeshContext *bsc) {
    assert(input.size() == 1);
    result = *input[0];
    return 1;
}

int MergeValues(ShowRouteTable &result, vector<const ShowRouteTable *> &input,
                int limit, const BgpSandeshContext *bsc) {
    vector<const vector<ShowRoute> *> list;
    result.routing_instance = input[0]->routing_instance;
    result.routing_table_name = input[0]->routing_table_name;
    result.deleted = input[0]->deleted;
    result.deleted_at = input[0]->deleted_at;
    result.prefixes = input[0]->prefixes;
    result.primary_paths = input[0]->primary_paths;
    result.secondary_paths = input[0]->secondary_paths;
    result.infeasible_paths = input[0]->infeasible_paths;
    result.paths = input[0]->paths;
    result.listeners = input[0]->listeners;

    int count = 0;
    for (size_t i = 0; i < input.size(); ++i) {
        if (input[i]->routes.size())
            list.push_back(&input[i]->routes);
    }
    MergeSort(result.routes, list, limit ? limit - count : 0, bsc,
              input[0]->routing_table_name);
    count += result.routes.size();
    return count;
}

// Merge n number of vectors in result. input is a vector of pointers to vector
template <class T>
void MergeSort(vector<T> &result, vector<const vector<T> *> &input, int limit,
               const BgpSandeshContext *bsc, const string &table_name) {
    size_t size = input.size();
    size_t index[size];
    bzero(index, sizeof(index));
    int count = 0;
    while (limit == 0 || count < limit) {
        size_t best_index = size;
        const T *best = NULL;
        for (size_t i = 0; i < size; ++i) {
            if (index[i] == input[i]->size()) continue;
            if (best == NULL ||
                ItemIsLess(input[i]->at(index[i]), *best, bsc, table_name)) {
                best = &input[i]->at(index[i]);
                best_index = i;
                continue;
            }
        }
        if (best_index >= size) break;
        T table;
        vector<const T *> list;
        for (size_t j = best_index; j < size; ++j) {
            if (index[j] == input[j]->size()) continue;
            if (ItemIsLess(input[j]->at(index[j]), *best, bsc, table_name) ||
                ItemIsLess(*best, input[j]->at(index[j]), bsc, table_name)) {
                continue;
            }
            list.push_back(&input[j]->at(index[j]));
            index[j]++;
        }
        count += MergeValues(table, list, limit ? limit - count : 0, bsc);
        result.push_back(table);
    }
}

string ShowRouteHandler::SaveContextAndPopLast(const ShowRouteReq *req,
        vector<ShowRouteTable> *route_table_list) {

    // If there are no output results for the input parameters, we dont need to
    // save anything since there will be no subsequent iteration and there is
    // nothing to pop off.
    if (route_table_list->empty()) {
        return string("");
    }

    // Get the total number of routes that we have collected in this iteration
    // after the mergesort.
    uint32_t total_count = 0;
    for (size_t i = 0; i < route_table_list->size(); ++i) {
        total_count += route_table_list->at(i).routes.size();
    }

    ShowRouteTable *last_route_table =
        &route_table_list->at(route_table_list->size() - 1);
    string next_batch;

    // We always attempt to read one extra entry (GetMaxRouteCount() adds 1).
    // Case 1: (total_count > GetMaxRouteCount())
    // This cannot happen since the mergesort has already trimmed the list to
    // GetMaxRouteCount
 
    // Case 2: (total_count < GetMaxRouteCount())
    // There are no more entries matching the input criteria and we are done.
    if (total_count < ShowRouteHandler::GetMaxRouteCount(req)) {
        return next_batch;
    }

    // Case 3: (total_count == GetMaxRouteCount())
    // If total_count is equal to GetMaxRouteCount(), we have atleast one entry
    // for the next round i.e. we are not done and we need to init next_batch
    // with the right values from the extra entry and we also need to pop off
    // the extra entry.

    int new_count;
    bool next_round = true;
    if (req->get_count()) {
        // This is required for the last round if the user gave a count. Even
        // though we have read an extra entry, we are done and we do not want
        // to display any 'next_batch' http links.
        new_count = req->get_count() - total_count + 1;
        if (!new_count) {
            next_round = false;
        }
    } else {
        // If the user did not fill in the count, keep using zero.
        new_count = req->get_count();
    }

    if (next_round) {
        ShowRoute last_route =
            last_route_table->routes.at(last_route_table->routes.size() - 1);
        next_batch =
            req->get_routing_instance() + kShowRouteIterSeparator +
            req->get_routing_table() + kShowRouteIterSeparator +
            req->get_prefix() + kShowRouteIterSeparator +
            last_route_table->get_routing_instance() + kShowRouteIterSeparator +
            last_route_table->get_routing_table_name() +
                                                       kShowRouteIterSeparator +
            last_route.get_prefix() + kShowRouteIterSeparator +
            integerToString(new_count) + kShowRouteIterSeparator +
            BoolToString(req->get_longer_match());
    }

    // Pop off the last entry only after we have captured its values in
    // 'next_batch' above.
    last_route_table->routes.pop_back();
    if (last_route_table->routes.empty()) {
        route_table_list->pop_back();
    }

    return next_batch;
}

void ShowRouteHandler::CallbackS2Common(const ShowRouteReq *req,
                                        const RequestPipeline::PipeSpec ps,
                                        ShowRouteResp *resp) {
    BgpSandeshContext *bsc =
        static_cast<BgpSandeshContext *>(req->client_context());
    const RequestPipeline::StageData *sd = ps.GetStageData(0);
    vector<ShowRouteTable> route_table_list;
    vector<const vector<ShowRouteTable> *> table_lists;
    for (size_t i = 0; i < sd->size(); ++i) {
        const ShowRouteData &old_data =
            static_cast<const ShowRouteData &>(sd->at(i));
        if (old_data.route_table_list.size()) {
            table_lists.push_back(&old_data.route_table_list);
        }
    }
    MergeSort(route_table_list, table_lists,
              ShowRouteHandler::GetMaxRouteCount(req), bsc, "");

    string next_batch = SaveContextAndPopLast(req, &route_table_list);
    resp->set_next_batch(next_batch);

    // Save the table in the message *after* popping the last entry above.
    resp->set_tables(route_table_list);
}

bool ShowRouteHandler::CallbackS2(const Sandesh *sr,
        const RequestPipeline::PipeSpec ps, int stage, int instNum,
        RequestPipeline::InstData *data) {
    const ShowRouteReq *req =
        static_cast<const ShowRouteReq *>(ps.snhRequest_.get());

    ShowRouteResp *resp = new ShowRouteResp;
    CallbackS2Common(req, ps, resp);
    resp->set_context(req->context());
    resp->Response();
    return true;
}

bool ShowRouteHandler::CallbackS2Iterate(const Sandesh *sr,
        const RequestPipeline::PipeSpec ps, int stage, int instNum,
        RequestPipeline::InstData *data) {
    const ShowRouteReqIterate *req_iterate =
        static_cast<const ShowRouteReqIterate *>(ps.snhRequest_.get());

    ShowRouteResp *resp = new ShowRouteResp;
    ShowRouteReq *req = new ShowRouteReq;
    bool success = ConvertReqIterateToReq(req_iterate, req);
    if (success) {
        CallbackS2Common(req, ps, resp);
    }
    resp->set_context(req->context());
    resp->Response();
    req->Release();
    return true;
}

// handler for 'show route'
void ShowRouteReq::HandleRequest() const {
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(client_context());

    RequestPipeline::PipeSpec ps(this);

    // Request pipeline has 2 stages. In first stage, we spawn one task per
    // partition and generate the list of routes. In second stage, we look
    // at the generated list and merge it so that we can send it back.
    RequestPipeline::StageSpec s1, s2;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();
    s1.taskId_ = scheduler->GetTaskId("db::DBTable");
    s1.allocFn_ = ShowRouteHandler::CreateData;

    s1.cbFn_ = ShowRouteHandler::CallbackS1;
    for (int i = 0; i < bsc->bgp_server->database()->PartitionCount(); ++i) {
        s1.instances_.push_back(i);
    }

    s2.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s2.allocFn_ = ShowRouteHandler::CreateData;
    s2.cbFn_ = ShowRouteHandler::CallbackS2;
    s2.instances_.push_back(0);

    ps.stages_= list_of(s1)(s2);
    RequestPipeline rp(ps);
}

// handler for 'show route' that shows batches of routes, iteratively
void ShowRouteReqIterate::HandleRequest() const {
    BgpSandeshContext *bsc = static_cast<BgpSandeshContext *>(client_context());

    RequestPipeline::PipeSpec ps(this);

    // Request pipeline has 2 stages. In first stage, we spawn one task per
    // partition and generate the list of routes. In second stage, we look
    // at the generated list and merge it so that we can send it back.
    RequestPipeline::StageSpec s1, s2;
    TaskScheduler *scheduler = TaskScheduler::GetInstance();

    s1.taskId_ = scheduler->GetTaskId("db::DBTable");
    s1.allocFn_ = ShowRouteHandler::CreateData;
    s1.cbFn_ = ShowRouteHandler::CallbackS1Iterate;
    for (int i = 0; i < bsc->bgp_server->database()->PartitionCount(); ++i) {
        s1.instances_.push_back(i);
    }

    s2.taskId_ = scheduler->GetTaskId("bgp::ShowCommand");
    s2.allocFn_ = ShowRouteHandler::CreateData;
    s2.cbFn_ = ShowRouteHandler::CallbackS2Iterate;
    s2.instances_.push_back(0);

    ps.stages_= list_of(s1)(s2);
    RequestPipeline rp(ps);
}

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
        const string &search_string = req->get_search_string();
        BgpSandeshContext *bsc =
            static_cast<BgpSandeshContext *>(req->client_context());
        RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();
        for (RoutingInstanceMgr::name_iterator it = rim->name_begin();
             it != rim->name_end(); it++) {
            RoutingInstance *ri = it->second;
            if (ri->IsDefaultRoutingInstance())
                continue;
            ErmVpnTable *table =
                static_cast<ErmVpnTable *>(ri->GetTable(Address::ERMVPN));
            if (!table)
                continue;
            if (!search_string.empty() &&
                table->name().find(search_string) == string::npos) {
                continue;
            }
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
        const string &search_string = req->get_search_string();
        BgpSandeshContext *bsc =
            static_cast<BgpSandeshContext *>(req->client_context());
        RoutingInstanceMgr *rim = bsc->bgp_server->routing_instance_mgr();
        vector<ShowMulticastManager> mgr_list;
        const RequestPipeline::StageData *sd = ps.GetStageData(0);
        for (RoutingInstanceMgr::name_iterator it = rim->name_begin();
             it != rim->name_end(); it++) {
            RoutingInstance *ri = it->second;
            if (ri->IsDefaultRoutingInstance())
                continue;
            ErmVpnTable *table =
                static_cast<ErmVpnTable *>(ri->GetTable(Address::ERMVPN));
            if (!table)
                continue;
            if (!search_string.empty() &&
                table->name().find(search_string) == string::npos) {
                continue;
            }
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

        vector<ShowBgpPeeringConfig> peering_list;
        typedef std::pair<std::string, const BgpNeighborConfig *> pair_t;
        BOOST_FOREACH(pair_t item, bcm->NeighborMapItems(
            BgpConfigManager::kMasterInstance)) {
            ShowBgpPeeringConfig peering;
            const BgpNeighborConfig *neighbor = item.second;
            peering.set_instance_name(neighbor->instance_name());
            peering.set_name(neighbor->name());
            peering.set_neighbor_count(1);

            vector<ShowBgpSessionConfig> session_list;
            ShowBgpSessionConfig session;
            session.set_uuid(neighbor->uuid());
            vector<ShowBgpSessionAttributesConfig> attribute_list;
            ShowBgpSessionAttributesConfig attribute;
            attribute.set_address_families(neighbor->address_families());
            attribute_list.push_back(attribute);
            session.set_attributes(attribute_list);
            session_list.push_back(session);
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

        vector<ShowBgpNeighborConfig> nbr_list;

        typedef std::pair<std::string, const BgpNeighborConfig *> pair_t;
        BOOST_FOREACH(pair_t item, bcm->NeighborMapItems(
            BgpConfigManager::kMasterInstance)) {
            const BgpNeighborConfig *neighbor = item.second;
            ShowBgpNeighborConfig nbr;
            nbr.set_instance_name(neighbor->instance_name());
            nbr.set_name(neighbor->name());
            Ip4Address localid(ntohl(neighbor->local_identifier()));
            nbr.set_local_identifier(localid.to_string());
            nbr.set_local_as(neighbor->local_as());
            nbr.set_autonomous_system(neighbor->peer_as());
            Ip4Address peerid(ntohl(neighbor->peer_identifier()));
            nbr.set_identifier(peerid.to_string());
            nbr.set_address(neighbor->peer_address().to_string());
            nbr.set_address_families(neighbor->address_families());
            nbr.set_last_change_at(
                UTCUsecToString(neighbor->last_change_at()));
            nbr.set_auth_type(neighbor->auth_data().KeyTypeToString());
            if (bsc->test_mode()) {
                nbr.set_auth_keys(neighbor->auth_data().KeysToStringDetail());
            }

            nbr_list.push_back(nbr);
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
