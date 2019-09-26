/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_show_route.h"

#include <boost/assign/list_of.hpp>
#include <sandesh/request_pipeline.h>

#include "base/regex.h"
#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_route.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_table.h"
#include "bgp/routing-instance/routing_instance.h"

using boost::assign::list_of;
using std::auto_ptr;
using std::string;
using std::vector;

static bool IsLess(const ShowRoute &lhs, const ShowRoute &rhs,
                   const BgpSandeshContext *bsc, const string &table_name) {
    BgpTable *table = static_cast<BgpTable *>
        (bsc->bgp_server->database()->FindTable(table_name));
    if (!table) {
        return false;
    }
    auto_ptr<DBEntry> lhs_entry = table->AllocEntryStr(lhs.get_prefix());
    auto_ptr<DBEntry> rhs_entry = table->AllocEntryStr(rhs.get_prefix());

    Route *lhs_route = static_cast<Route *>(lhs_entry.get());
    Route *rhs_route = static_cast<Route *>(rhs_entry.get());

    return lhs_route->IsLess(*rhs_route);
}

static bool IsLess(const ShowRouteTable &lhs, const ShowRouteTable &rhs,
                   const BgpSandeshContext *bsc, const string &table_name) {
    if (lhs.get_routing_instance() < rhs.get_routing_instance()) {
        return true;
    }
    if (lhs.get_routing_instance() == rhs.get_routing_instance()) {
        return lhs.get_routing_table_name() < rhs.get_routing_table_name();
    }
    return false;
}

template <class T>
void MergeSort(vector<T> *result, vector<const vector<T> *> *input, int limit,
               const BgpSandeshContext *bsc, const string &table_name);

int MergeValues(ShowRoute *result, vector<const ShowRoute *> *input,
                int limit, const BgpSandeshContext *bsc) {
    assert(input->size() == 1);
    *result = *input->at(0);
    return 1;
}

int MergeValues(ShowRouteTable *result, vector<const ShowRouteTable *> *input,
                int limit, const BgpSandeshContext *bsc) {
    vector<const vector<ShowRoute> *> list;
    result->set_routing_instance(input->at(0)->get_routing_instance());
    result->set_routing_table_name(input->at(0)->get_routing_table_name());
    result->set_deleted(input->at(0)->get_deleted());
    result->set_deleted_at(input->at(0)->get_deleted_at());
    result->set_prefixes(input->at(0)->get_prefixes());
    result->set_primary_paths(input->at(0)->get_primary_paths());
    result->set_secondary_paths(input->at(0)->get_secondary_paths());
    result->set_infeasible_paths(input->at(0)->get_infeasible_paths());
    result->set_stale_paths(input->at(0)->get_stale_paths());
    result->set_llgr_stale_paths(input->at(0)->get_llgr_stale_paths());
    result->set_paths(input->at(0)->get_paths());
    result->set_listeners(input->at(0)->get_listeners());

    int count = 0;
    for (size_t i = 0; i < input->size(); ++i) {
        if (input->at(i)->get_routes().size())
            list.push_back(&input->at(i)->get_routes());
    }
    MergeSort(const_cast<vector<ShowRoute> *>(&result->get_routes()), &list,
              limit ? limit - count : 0, bsc,
              input->at(0)->get_routing_table_name());
    count += result->get_routes().size();
    return count;
}

//
// Merge n number of vectors in result.
// Input is a vector of pointers to vector.
//
template <class T>
void MergeSort(vector<T> *result, vector<const vector<T> *> *input, int limit,
               const BgpSandeshContext *bsc, const string &table_name) {
    size_t size = input->size();
    vector<size_t> index(size, 0);
    int count = 0;
    while (limit == 0 || count < limit) {
        size_t best_index = size;
        const T *best = NULL;
        for (size_t i = 0; i < size; ++i) {
            if (index[i] == input->at(i)->size())
                continue;
            if (best == NULL ||
                IsLess(input->at(i)->at(index[i]), *best, bsc, table_name)) {
                best = &input->at(i)->at(index[i]);
                best_index = i;
                continue;
            }
        }
        if (best_index >= size)
            break;
        T table;
        vector<const T *> list;
        for (size_t j = best_index; j < size; ++j) {
            if (index[j] == input->at(j)->size())
                continue;
            if (IsLess(input->at(j)->at(index[j]), *best, bsc, table_name) ||
                IsLess(*best, input->at(j)->at(index[j]), bsc, table_name)) {
                continue;
            }
            list.push_back(&input->at(j)->at(index[j]));
            index[j]++;
        }
        count += MergeValues(&table, &list, limit ? limit - count : 0, bsc);
        result->push_back(table);
    }
}

char ShowRouteHandler::kIterSeparator[] = "||";
uint32_t ShowRouteHandler::GetMaxCount(bool test_mode) {
    if (test_mode) {
        return kUnitTestMaxCount;
    } else {
        return kMaxCount;
    }
}

ShowRouteHandler::ShowRouteHandler(const ShowRouteReq *req, int inst_id) :
        req_(req), inst_id_(inst_id), prefix_expr_(req->get_prefix()) {
}

// Search for interesting prefixes in a given table for given partition
void ShowRouteHandler::BuildShowRouteTable(BgpTable *table,
        vector<ShowRoute> *route_list, int count) {
    if (inst_id_ >= table->PartitionCount())
        return;
    DBTablePartition *partition =
        static_cast<DBTablePartition *>(table->GetTablePartition(inst_id_));
    BgpRoute *route = NULL;

    if (table->name() == req_->get_start_routing_table()) {
        auto_ptr<DBEntry> key =
            table->AllocEntryStr(req_->get_start_prefix());
        route = static_cast<BgpRoute *>(partition->lower_bound(key.get()));
    } else {
        route = static_cast<BgpRoute *>(partition->GetFirst());
    }
    for (int i = 0; route && (!count || i < count);
         route = static_cast<BgpRoute *>(partition->GetNext(route)), ++i) {
        if (!MatchPrefix(req_->get_prefix(), route,
                         req_->get_longer_match(),
                         req_->get_shorter_match()))
            continue;
        ShowRoute show_route;
        route->FillRouteInfo(table, &show_route, req_->get_source(),
                             req_->get_protocol());
        if (!show_route.get_paths().empty())
            route_list->push_back(show_route);
    }
}

bool ShowRouteHandler::MatchPrefix(const string &expected_prefix,
                                   BgpRoute *route, bool longer_match,
                                   bool shorter_match) {
    if (expected_prefix.empty())
        return true;
    if (!longer_match && !shorter_match)
        return regex_search(route->ToString(), prefix_expr_);

    // Do longer match.
    if (longer_match && route->IsMoreSpecific(expected_prefix))
        return true;

    // Do shorter match.
    if (shorter_match && route->IsLessSpecific(expected_prefix))
        return true;

    return false;
}

bool ShowRouteHandler::match(const string &expected, const string &actual) {
    if (expected == "")
        return true;
    return expected == actual;
}

RequestPipeline::InstData *ShowRouteHandler::CreateData(int stage) {
    return static_cast<RequestPipeline::InstData *>(new ShowRouteData);
}

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
bool ShowRouteHandler::ConvertReqIterateToReq(
    const ShowRouteReqIterate *req_iterate, ShowRouteReq *req) {
    // First, set the context from the original request since we might return
    // due to parsing errors.
    req->set_context(req_iterate->context());

    // Format of route_info:
    // UserRI||UserRT||UserPfx||NextRI||NextRT||NextPfx||count||longer_match||
    // shorter_match
    //
    // User* values were entered by the user and Next* values indicate 'where'
    // we need to start this iteration.
    string route_info = req_iterate->get_route_info();
    size_t sep_size = strlen(kIterSeparator);

    size_t pos1 = route_info.find(kIterSeparator);
    if (pos1 == string::npos) {
        return false;
    }
    string user_ri = route_info.substr(0, pos1);

    size_t pos2 = route_info.find(kIterSeparator, (pos1 + sep_size));
    if (pos2 == string::npos) {
        return false;
    }
    string user_rt = route_info.substr((pos1 + sep_size),
                                       pos2 - (pos1 + sep_size));

    size_t pos3 = route_info.find(kIterSeparator, (pos2 + sep_size));
    if (pos3 == string::npos) {
        return false;
    }
    string user_prefix = route_info.substr((pos2 + sep_size),
                                           pos3 - (pos2 + sep_size));

    size_t pos4 = route_info.find(kIterSeparator, (pos3 + sep_size));
    if (pos4 == string::npos) {
        return false;
    }
    string next_ri = route_info.substr((pos3 + sep_size),
                                       pos4 - (pos3 + sep_size));

    size_t pos5 = route_info.find(kIterSeparator, (pos4 + sep_size));
    if (pos5 == string::npos) {
        return false;
    }
    string next_rt = route_info.substr((pos4 + sep_size),
                                       pos5 - (pos4 + sep_size));

    size_t pos6 = route_info.find(kIterSeparator, (pos5 + sep_size));
    if (pos6 == string::npos) {
        return false;
    }
    string next_prefix = route_info.substr((pos5 + sep_size),
                                           pos6 - (pos5 + sep_size));

    size_t pos7 = route_info.find(kIterSeparator, (pos6 + sep_size));
    if (pos7 == string::npos) {
        return false;
    }
    string count_str = route_info.substr((pos6 + sep_size),
                                         pos7 - (pos6 + sep_size));

    size_t pos8 = route_info.find(kIterSeparator, (pos7 + sep_size));
    if (pos8 == string::npos) {
        return false;
    }
    string user_source = route_info.substr((pos7 + sep_size),
                                           pos8 - (pos7 + sep_size));

    size_t pos9 = route_info.find(kIterSeparator, (pos8 + sep_size));
    if (pos9 == string::npos) {
        return false;
    }
    string user_protocol = route_info.substr((pos8 + sep_size),
                                             pos9 - (pos8 + sep_size));

    size_t pos10 = route_info.find(kIterSeparator, (pos9 + sep_size));
    if (pos10 == string::npos) {
        return false;
    }
    string user_family = route_info.substr((pos9 + sep_size),
                                           pos10 - (pos9 + sep_size));

    size_t pos11 = route_info.find(kIterSeparator, (pos10 + sep_size));
    if (pos11 == string::npos) {
        return false;
    }
    string longer_match = route_info.substr((pos10 + sep_size),
                                            pos11 - (pos10 + sep_size));

    string shorter_match = route_info.substr(pos11 + sep_size);

    req->set_routing_instance(user_ri);
    req->set_routing_table(user_rt);
    req->set_prefix(user_prefix);
    req->set_start_routing_instance(next_ri);
    req->set_start_routing_table(next_rt);
    req->set_start_prefix(next_prefix);
    req->set_count(atoi(count_str.c_str()));
    req->set_source(user_source);
    req->set_protocol(user_protocol);
    req->set_family(user_family);
    req->set_longer_match(StringToBool(longer_match));
    req->set_shorter_match(StringToBool(shorter_match));

    return true;
}

bool ShowRouteHandler::CallbackS1Common(const ShowRouteReq *req, int inst_id,
                                        ShowRouteData *mydata) {
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
            if (!req->get_family().empty() && req->get_family() !=
                    Address::FamilyToTableString(table->family())) {
                continue;
            }
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
            srt.set_prefixes(table->Size());
            srt.set_primary_paths(table->GetPrimaryPathCount());
            srt.set_secondary_paths(table->GetSecondaryPathCount());
            srt.set_infeasible_paths(table->GetInfeasiblePathCount());
            srt.set_stale_paths(table->GetStalePathCount());
            srt.set_llgr_stale_paths(table->GetLlgrStalePathCount());
            srt.set_paths(srt.get_primary_paths() + srt.get_secondary_paths());

            vector<ShowTableListener> listeners;
            table->FillListeners(&listeners);
            srt.set_listeners(listeners);

            vector<ShowRoute> route_list;
            handler.BuildShowRouteTable(table, &route_list,
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
    ShowRouteData *mydata = static_cast<ShowRouteData *>(data);
    int inst_id = ps.stages_[stage].instances_[instNum];
    const ShowRouteReq *req =
        static_cast<const ShowRouteReq *>(ps.snhRequest_.get());

    return CallbackS1Common(req, inst_id, mydata);
}

bool ShowRouteHandler::CallbackS1Iterate(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum, RequestPipeline::InstData *data) {
    ShowRouteData *mydata = static_cast<ShowRouteData *>(data);
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
        total_count += route_table_list->at(i).get_routes().size();
    }

    ShowRouteTable *last_route_table =
        &route_table_list->at(route_table_list->size() - 1);
    string next_batch;

    // We always attempt to read one extra entry (GetMaxRouteCount() adds 1).
    // Case 1: (total_count > GetMaxRouteCount())
    // This cannot happen since the merge sort has already trimmed the list to
    // GetMaxRouteCount

    // Case 2: (total_count < GetMaxRouteCount())
    // There are no more entries matching the input criteria and we are done.
    if (total_count < ShowRouteHandler::GetMaxRouteCount(req)) {
        return next_batch;
    }

    // Case 3: (total_count == GetMaxRouteCount())
    // If total_count is equal to GetMaxRouteCount(), we have at least 1 entry
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
            last_route_table->get_routes().at(
                    last_route_table->get_routes().size() - 1);
        next_batch =
            req->get_routing_instance() + kIterSeparator +
            req->get_routing_table() + kIterSeparator +
            req->get_prefix() + kIterSeparator +
            last_route_table->get_routing_instance() + kIterSeparator +
            last_route_table->get_routing_table_name() + kIterSeparator +
            last_route.get_prefix() + kIterSeparator +
            integerToString(new_count) + kIterSeparator +
            req->get_source() + kIterSeparator +
            req->get_protocol() + kIterSeparator +
            req->get_family() + kIterSeparator +
            BoolToString(req->get_longer_match()) + kIterSeparator +
            BoolToString(req->get_shorter_match());
    }

    // Pop off the last entry only after we have captured its values in
    // 'next_batch' above.
    const_cast<vector<ShowRoute> *>(
            &last_route_table->get_routes())->pop_back();
    if (last_route_table->get_routes().empty()) {
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
    MergeSort(&route_table_list, &table_lists,
              ShowRouteHandler::GetMaxRouteCount(req), bsc, "");

    string next_batch = SaveContextAndPopLast(req, &route_table_list);
    resp->set_next_batch(next_batch);

    // Save the table in the message *after* popping the last entry above.
    resp->set_tables(route_table_list);
}

bool ShowRouteHandler::CallbackS2(const Sandesh *sr,
        const RequestPipeline::PipeSpec &ps, int stage, int instNum,
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

    ps.stages_= list_of(s1)(s2)
        .convert_to_container<vector<RequestPipeline::StageSpec> >();
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

    ps.stages_= list_of(s1)(s2)
        .convert_to_container<vector<RequestPipeline::StageSpec> >();
    RequestPipeline rp(ps);
}
