/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_drop_stats_kstate_h
#define vnsw_agent_drop_stats_kstate_h

class DropStatsKState: public KState {
public:
    DropStatsKState(KDropStatsResp *obj, const std::string &resp_ctx,
                    vr_drop_stats_req &encoder);
    void SendResponse() {}
    void Handler();
    void SendNextRequest() {}
};
#endif //vnsw_agent_drop_stats_kstate_h
