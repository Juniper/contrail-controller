/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_SHOW_HANDLER_H__
#define SRC_BGP_BGP_SHOW_HANDLER_H__

#include "bgp/bgp_sandesh.h"

#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh.h>
#include <sandesh/request_pipeline.h>

#include <string>
#include <vector>

static char kIterSeparator[] = "||";

//
// Class template to handle single stage show commands with single key and
// a search string.
//
// Supports pagination of output as specified by page limit.
//
// Also supports an iteration limit, which is the maximum number of entries
// examined in one run. This is useful when the search string is non-empty
// and there are a large number of entries in table.  We don't want to look
// at potentially all entries in one shot in cases where most of them don't
// match the search string.
//
// Data is used to store partial pages of results as well as other context
// that needs to be maintained between successive runs of the callbacks in
// cases where we don't manage to fill a page of results in one run.
//
// Note that the infrastructure automatically reschedules and invokes the
// callback function again if it returns false.
//
template <typename ReqT, typename ReqIterateT, typename RespT, typename ShowT>
class BgpShowHandler {
public:
    static const uint32_t kPageLimit = 64;
    static const uint32_t kIterLimit = 1024;

    struct Data : public RequestPipeline::InstData {
        Data() : initialized(false) {
        }

        bool initialized;
        std::string search_string;
        std::string next_entry;
        std::string next_batch;
        std::vector<ShowT> show_list;
    };

    static RequestPipeline::InstData *CreateData(int stage) {
        return (new Data);
    }

    static void ConvertReqToData(const ReqT *req, Data *data);
    static bool ConvertReqIterateToData(const ReqIterateT *req_iterate,
        Data *data);
    static void SaveContextToData(const std::string &next_entry, bool done,
        Data *data);

    static void FillShowList(RespT *resp, const std::vector<ShowT> &show_list);
    static bool CallbackCommon(const BgpSandeshContext *bsc, Data *data);
    static bool Callback(const Sandesh *sr,
        const RequestPipeline::PipeSpec ps,
        int stage, int instNum, RequestPipeline::InstData *data);
    static bool CallbackIterate(const Sandesh *sr,
        const RequestPipeline::PipeSpec ps,
        int stage, int instNum, RequestPipeline::InstData *data);
};

//
// Initialize Data from ReqT.
//
template <typename ReqT, typename ReqIterateT, typename RespT, typename ShowT>
void BgpShowHandler<ReqT, ReqIterateT, RespT, ShowT>::ConvertReqToData(
    const ReqT *req, Data *data) {
    if (data->initialized)
        return;
    data->initialized = true;
    data->search_string = req->get_search_string();
}

//
// Initialize Data from ReqInterateT.
//
// Return false if there's a problem parsing the iterate_info string.
//
template <typename ReqT, typename ReqIterateT, typename RespT, typename ShowT>
bool BgpShowHandler<ReqT, ReqIterateT, RespT, ShowT>::ConvertReqIterateToData(
    const ReqIterateT *req_iterate, Data *data) {
    if (data->initialized)
        return true;
    data->initialized = true;

    // Format of iterate_info:
    // next_entry||search_string
    std::string iterate_info = req_iterate->get_iterate_info();
    size_t sep_size = strlen(kIterSeparator);

    size_t pos1 = iterate_info.find(kIterSeparator);
    if (pos1 == std::string::npos)
        return false;

    data->next_entry = iterate_info.substr(0, pos1);
    data->search_string = iterate_info.substr(pos1 + sep_size);
    return true;
}

//
// Save context into Data.
// The next_entry field gets used in the subsequent invocation of callback
// routine when the callback routine returns false.
// The next_batch string is used if the page is filled (i.e. done is true).
//
template <typename ReqT, typename ReqIterateT, typename RespT, typename ShowT>
void BgpShowHandler<ReqT, ReqIterateT, RespT, ShowT>::SaveContextToData(
    const std::string &next_entry, bool done, Data *data) {
    data->next_entry = next_entry;
    if (done)
        data->next_batch = next_entry + kIterSeparator + data->search_string;
}

//
// Fill show_list into RespT.
// Should be specialized to allow each RespT to have a potentially different
// name for the list of entries.
//
template <typename ReqT, typename ReqIterateT, typename RespT, typename ShowT>
void BgpShowHandler<ReqT, ReqIterateT, RespT, ShowT>::FillShowList(
    RespT *resp, const std::vector<ShowT> &show_list) {
}

//
// Common routine for regular and iterate requests.
// Assumes that Data has been initialized properly by caller.
// Examine specified maximum entries starting at data->next_entry.
//
// Should be specialized to handle specifics of the show command being
// handled.
//
// Return true if we're examined all entries or reached the page limit.
//
template <typename ReqT, typename ReqIterateT, typename RespT, typename ShowT>
bool BgpShowHandler<ReqT, ReqIterateT, RespT, ShowT>::CallbackCommon(
    const BgpSandeshContext *bsc, Data *data) {
    return true;
}

//
// Callback for ReqT. This gets called for initial request and subsequently in
// cases where the iteration count is reached.
//
// Return false if the iteration count is reached before page gets filled.
// Return true if the page gets filled or we've examined all entries.
//
template <typename ReqT, typename ReqIterateT, typename RespT, typename ShowT>
bool BgpShowHandler<ReqT, ReqIterateT, RespT, ShowT>::Callback(
    const Sandesh *sr, const RequestPipeline::PipeSpec ps,
    int stage, int instNum, RequestPipeline::InstData *data) {
    Data *mydata = static_cast<Data *>(data);
    const ReqT *req = static_cast<const ReqT *>(ps.snhRequest_.get());
    const BgpSandeshContext *bsc =
        static_cast<const BgpSandeshContext *>(req->client_context());

    // Parse request and save state in Data.
    ConvertReqToData(req, mydata);

    // Return false and reschedule ourselves if we've reached the limit of
    // the number of entries examined.
    if (!CallbackCommon(bsc, mydata))
        return false;

    // All done - ship the response.
    RespT *resp = new RespT;
    resp->set_context(req->context());
    if (!mydata->show_list.empty())
        FillShowList(resp, mydata->show_list);
    if (!mydata->next_batch.empty())
        resp->set_next_batch(mydata->next_batch);
    resp->Response();
    return true;
}

//
// Callback for ReqIterate. This is called for initial request and subsequently
// in cases where the iteration count is reached. Parse the iterate_info string
// to figure out the next entry to examine.
//
// Return false if the iteration limit is reached before page gets filled.
// Return true if the page gets filled or we've examined all entries.
//
template <typename ReqT, typename ReqIterateT, typename RespT, typename ShowT>
bool BgpShowHandler<ReqT, ReqIterateT, RespT, ShowT>::CallbackIterate(
    const Sandesh *sr, const RequestPipeline::PipeSpec ps,
    int stage, int instNum, RequestPipeline::InstData *data) {
    Data *mydata = static_cast<Data *>(data);
    const ReqIterateT *req_iterate =
        static_cast<const ReqIterateT *>(ps.snhRequest_.get());
    const BgpSandeshContext *bsc =
        static_cast<const BgpSandeshContext *>(req_iterate->client_context());

    // Parse request and save state in Data.
    if (!ConvertReqIterateToData(req_iterate, mydata)) {
        RespT *resp = new RespT;
        resp->set_context(req_iterate->context());
        resp->Response();
        return true;
    }

    // Return false and reschedule ourselves if we've reached the limit of
    // the number of entries examined.
    if (!CallbackCommon(bsc, mydata))
        return false;

    // All done - ship the response.
    RespT *resp = new RespT;
    resp->set_context(req_iterate->context());
    if (!mydata->show_list.empty())
        FillShowList(resp, mydata->show_list);
    if (!mydata->next_batch.empty())
        resp->set_next_batch(mydata->next_batch);
    resp->Response();
    return true;
}

#endif  // SRC_BGP_BGP_SHOW_HANDLER_H__
