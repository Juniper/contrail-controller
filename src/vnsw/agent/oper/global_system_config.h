/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __AGENT_OPER_GLOBAL_SYSTEM_CONFIG_H
#define __AGENT_OPER_GLOBAL_SYSTEM_CONFIG_H

#include <cmn/agent_cmn.h>
#include <oper/oper_db.h>

class IFMapNode;
namespace autogen {
    class GlobalSystemConfig;
}

struct BGPaaServiceParameters {
typedef std::pair<uint16_t, uint16_t> BGPaaServicePortRangePair;
    void Reset();

    int port_start;
    int port_end;
};

struct GracefulRestartParameters {
public:
    typedef boost::function<void()> Callback;
    typedef std::vector<Callback> CallbackList;
    void Reset();
    void Update(autogen::GlobalSystemConfig *cfg);
    void Register(GracefulRestartParameters::Callback cb);
    bool enable() const { return enable_; }
    bool xmpp_helper_enable() const { return xmpp_helper_enable_; }
    bool config_seen() const { return config_seen_; }
    uint32_t end_of_rib_time() { return end_of_rib_time_; }
    uint32_t long_lived_restart_time() { return long_lived_restart_time_; }
    uint64_t llgr_stale_time() { return (long_lived_restart_time_ +
                                         end_of_rib_time_); }
    bool IsEnabled() const {
        return (config_seen_ && enable_ && xmpp_helper_enable_);
    }

private:
    void Notify();

    bool enable_;
    uint64_t end_of_rib_time_;
    uint64_t long_lived_restart_time_;
    bool xmpp_helper_enable_;
    CallbackList callbacks_;
    bool config_seen_;
};

struct FastConvergenceParameters {
    void Reset();

    uint8_t xmpp_hold_time;
    bool enable;
};

class GlobalSystemConfig : public OperIFMapTable {
public:
    GlobalSystemConfig(Agent *agent);
    virtual ~GlobalSystemConfig();

    void ConfigDelete(IFMapNode *node);
    void ConfigAddChange(IFMapNode *node);
    void ConfigManagerEnqueue(IFMapNode *node);
    BGPaaServiceParameters::BGPaaServicePortRangePair bgpaas_port_range() const {
         return std::make_pair(bgpaas_parameters_.port_start,
                               bgpaas_parameters_.port_end);
    }
    void Reset();
    GracefulRestartParameters &gres_parameters();
    FastConvergenceParameters &fc_params() { return fc_params_; }
    bool cfg_igmp_enable() const;
    void FillSandeshInfo(GlobalSystemConfigResp *resp);

private:
    BGPaaServiceParameters bgpaas_parameters_;
    GracefulRestartParameters gres_parameters_;
    FastConvergenceParameters fc_params_;
    bool cfg_igmp_enable_;
    DISALLOW_COPY_AND_ASSIGN(GlobalSystemConfig);
};
#endif
