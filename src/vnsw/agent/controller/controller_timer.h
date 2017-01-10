/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __VNSW_CONTROLLER_CLEANUP_TIMER_HPP__
#define __VNSW_CONTROLLER_CLEANUP_TIMER_HPP__

#include <sandesh/sandesh_trace.h>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

class AgentXmppChannel;

/*
 * Controller Timer 
 * Used to manage timers required for removing stale entries for config and
 * routes received from control nodes and end of config
 * Functionalities provided are - start, cancel, 
 *
 *
 */
struct ControllerTimer {
    ControllerTimer(Agent *agent, const std::string &timer_name, 
                    uint32_t default_stale_timer_interval);
    virtual ~ControllerTimer();

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    bool Cancel();
    bool TimerExpiredCallback();
    const std::string& timer_name() const {return timer_name_;}
    bool running() const;

    virtual bool TimerExpirationDone() {return false;}
    virtual uint32_t GetTimerInterval() const = 0;
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch) = 0;
    virtual uint32_t stale_timer_interval() {
        return stale_timer_interval_;
    }
    virtual void set_stale_timer_interval(uint32_t stale_timer_interval) {
        stale_timer_interval_ = stale_timer_interval;
    }

    Agent *agent_;
    Timer *controller_timer_;
    uint64_t last_restart_time_;
    std::string xmpp_server_;
    std::string timer_name_;
    uint32_t stale_timer_interval_;
};

struct ConfigCleanupTimer : public ControllerTimer {
    static const int timeout_ = (100 * 1000); // In milli seconds5
    ConfigCleanupTimer(Agent *agent)
        : ControllerTimer(agent, "Agent Config Stale cleanup timer",
                          timeout_) { }
    virtual ~ConfigCleanupTimer() { }

    virtual uint32_t GetTimerInterval() const {return timeout_;}
    virtual bool TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);
};

struct EndOfConfigTimer : public ControllerTimer {
    static const int kFallbackTimeOut = (15 * 60 * 1000); // In milli seconds5
    static const int kEndOfConfigTimeOut = (5 * 1000); // In milli seconds5
    static const int kInactivityTime = (30 * 1000); // In milli seconds5

    EndOfConfigTimer(Agent *agent);
    virtual ~EndOfConfigTimer() { }
    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const {return kEndOfConfigTimeOut;}
    virtual bool TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);

    uint64_t start_time_;
};

struct EndOfRibTimer : public ControllerTimer {
    static const int kEndOfRibTimeOut = (5 * 1000); // In milli seconds5
    static const int kInactivityTime = (30 * 1000); // In milli seconds5

    EndOfRibTimer(Agent *agent);
    virtual ~EndOfRibTimer() { }

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const {return kEndOfRibTimeOut;}
    virtual bool TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);

    bool eor_cn_sent_;
    AgentXmppChannel *agent_xmpp_channel_;
};
#endif
