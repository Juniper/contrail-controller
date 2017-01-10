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
                    uint32_t timer_interval);
    virtual ~ControllerTimer();

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    bool Cancel();
    bool TimerExpiredCallback();
    const std::string& timer_name() const {return timer_name_;}
    bool running() const;

    virtual bool TimerExpirationDone() {return false;}
    virtual uint32_t GetTimerInterval() const = 0;
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch) = 0;
    virtual uint32_t timer_interval() {
        return timer_interval_;
    }
    virtual void set_timer_interval(uint32_t timer_interval) {
        timer_interval_ = timer_interval;
    }

    Agent *agent_;
    Timer *controller_timer_;
    uint64_t last_restart_time_;
    std::string xmpp_server_;
    std::string timer_name_;
    uint32_t timer_interval_;
};

struct ConfigCleanupTimer : public ControllerTimer {
    ConfigCleanupTimer(Agent *agent);
    virtual ~ConfigCleanupTimer() { }

    virtual uint32_t GetTimerInterval() const;
    virtual bool TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);
};

struct EndOfConfigTimer : public ControllerTimer {
    EndOfConfigTimer(Agent *agent);
    virtual ~EndOfConfigTimer() { }
    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const;
    uint32_t GetFallbackInterval() const;
    uint32_t GetInactivityInterval() const;
    virtual bool TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);

    uint64_t start_time_;
};

struct EndOfRibTimer : public ControllerTimer {
    EndOfRibTimer(Agent *agent);
    virtual ~EndOfRibTimer() { }

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const;
    uint32_t GetFallbackInterval() const;
    uint32_t GetInactivityInterval() const;
    virtual bool TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);

    AgentXmppChannel *agent_xmpp_channel_;
};

struct StalePathTimer : public ControllerTimer {
    StalePathTimer(Agent *agent);
    virtual ~StalePathTimer() { }

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const;
    virtual bool TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);

    AgentXmppChannel *agent_xmpp_channel_;
};
#endif
