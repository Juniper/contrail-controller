/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_CONTROLLER_CLEANUP_TIMER_HPP__
#define __VNSW_CONTROLLER_CLEANUP_TIMER_HPP__

#include <sandesh/sandesh_trace.h>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>
#include "init/agent_param.h"

class AgentXmppChannel;

/*
 * Cleanup Timer 
 * Used to manage timers required for removing stale entries for config and
 * routes received from control nodes.
 * Functionalities provided are - start, cancel, 
 *
 * Current derivatives of cleanup_timer are - unicast, multicast and config.
 *
 */
struct CleanupTimer {
    CleanupTimer(Agent *agent, const std::string &timer_name, 
                 uint32_t default_stale_timer_interval);
    virtual ~CleanupTimer();

    void Start(AgentXmppChannel *agent_xmpp_channel);
    bool Cancel();
    bool TimerExpiredCallback();
    const std::string& timer_name() const {return timer_name_;}

    virtual void TimerExpirationDone() { }
    virtual uint32_t GetTimerInterval() const = 0;
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch) = 0;
    virtual uint32_t stale_timer_interval() {
        return stale_timer_interval_;
    }
    virtual void set_stale_timer_interval(uint32_t stale_timer_interval) {
        stale_timer_interval_ = stale_timer_interval;
    }

    Agent *agent_;
    Timer *cleanup_timer_;
    uint64_t last_restart_time_;
    std::string xmpp_server_;
    std::string timer_name_;
    uint32_t stale_timer_interval_;
};

struct UnicastCleanupTimer : public CleanupTimer {
    UnicastCleanupTimer(Agent *agent)
        : CleanupTimer(agent, "Agent Unicast Stale cleanup timer", 
                       agent->params()->unicast_stale_timer_msecs()) { };
    virtual ~UnicastCleanupTimer() { }

    virtual uint32_t GetTimerInterval() const {
        return stale_timer_interval_;}
    virtual void TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);
};

struct MulticastCleanupTimer : public CleanupTimer {
    MulticastCleanupTimer(Agent *agent) 
        : CleanupTimer(agent, "Agent Multicast Stale cleanup timer",
                       agent->params()->multicast_stale_timer_msecs()) { }
    virtual ~MulticastCleanupTimer() { }

    virtual uint32_t GetTimerInterval() const {return stale_timer_interval_;}
    virtual void TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);

    uint32_t peer_sequence_;
};

struct ConfigCleanupTimer : public CleanupTimer {
    ConfigCleanupTimer(Agent *agent)
        : CleanupTimer(agent, "Agent Config Stale cleanup timer",
                       agent->params()->config_cleanup_timeout_msecs()) { }
    virtual ~ConfigCleanupTimer() { }

    virtual uint32_t GetTimerInterval() const {return stale_timer_interval_;}
    virtual void TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);
};

#endif
