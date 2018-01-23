/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#ifndef SRC_BGP_BGP_SHOW_ROUTE_H__
#define SRC_BGP_BGP_SHOW_ROUTE_H__

#include "base/regex.h"
#include "bgp/bgp_peer_types.h"
#include "sandesh/request_pipeline.h"

class BgpRoute;
class BgpTable;
class ShowRouteReq;
class ShowRouteReqIterate;

class ShowRouteHandler {
public:
    static char kIterSeparator[];

    // kMaxCount can be a function of 'count' field in ShowRouteReq
    static const uint32_t kUnitTestMaxCount = 100;
    static const uint32_t kMaxCount = 1000;
    static uint32_t GetMaxCount(bool test_mode);

    struct ShowRouteData : public RequestPipeline::InstData {
        std::vector<ShowRouteTable> route_table_list;
    };

    ShowRouteHandler(const ShowRouteReq *req, int inst_id);

    // Search for interesting prefixes in a given table for given partition
    void BuildShowRouteTable(BgpTable *table,
                             std::vector<ShowRoute> *route_list, int count);

    bool MatchPrefix(const std::string &expected_prefix, BgpRoute *route,
                     bool longer_match, bool shorter_match);
    bool match(const std::string &expected, const std::string &actual);
    static RequestPipeline::InstData *CreateData(int stage);
    static bool CallbackS1Common(const ShowRouteReq *req, int inst_id,
                                 ShowRouteData *mydata);
    static void CallbackS2Common(const ShowRouteReq *req,
                                 const RequestPipeline::PipeSpec ps,
                                 ShowRouteResp *resp);

    static bool CallbackS1(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps,
            int stage, int instNum, RequestPipeline::InstData *data);
    static bool CallbackS2(const Sandesh *sr,
            const RequestPipeline::PipeSpec &ps,
            int stage, int instNum, RequestPipeline::InstData *data);

    static bool CallbackS1Iterate(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps, int stage, int instNum,
            RequestPipeline::InstData *data);
    static bool CallbackS2Iterate(const Sandesh *sr,
            const RequestPipeline::PipeSpec ps, int stage, int instNum,
            RequestPipeline::InstData *data);

    static std::string SaveContextAndPopLast(const ShowRouteReq *req,
            std::vector<ShowRouteTable> *route_table_list);
    static bool ConvertReqIterateToReq(const ShowRouteReqIterate *req_iterate,
                                       ShowRouteReq *req);
    static uint32_t GetMaxRouteCount(const ShowRouteReq *req);

private:
    const ShowRouteReq *req_;
    int inst_id_;
    contrail::regex prefix_expr_;
};

#endif  // SRC_BGP_BGP_SHOW_HANDLER_H__
