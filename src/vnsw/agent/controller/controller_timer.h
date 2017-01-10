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
    virtual bool TimerExpirationDone() {return false;}
    virtual uint32_t GetTimerInterval() const = 0;
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch) = 0;
    virtual uint32_t timer_interval() {
        return timer_interval_;
    }
    virtual void set_timer_interval(uint32_t timer_interval) {
        timer_interval_ = timer_interval;
    }

    bool Cancel();
    bool TimerExpiredCallback();
    const std::string& timer_name() const {return timer_name_;}
    bool running() const;

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

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const;
    virtual bool TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);

    uint64_t sequence_number_;
};

struct EndOfConfigTimer : public ControllerTimer {
    EndOfConfigTimer(Agent *agent, AgentIfMapXmppChannel *channel);
    virtual ~EndOfConfigTimer() { }

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const;
    virtual bool TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);

    uint32_t GetFallbackInterval() const;
    uint32_t GetInactivityInterval() const;
    void Reset();

    AgentIfMapXmppChannel *config_channel_;
    uint64_t last_config_receive_time_;
    uint64_t inactivity_detected_time_;
    uint64_t end_of_config_processed_time_;
    bool fallback_;
};

struct EndOfRibTxTimer : public ControllerTimer {
    EndOfRibTxTimer(Agent *agent);
    virtual ~EndOfRibTxTimer() { }

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const;
    virtual bool TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);

    uint32_t GetFallbackInterval() const;
    uint32_t GetInactivityInterval() const;
    void Reset();

    AgentXmppChannel *agent_xmpp_channel_;
    uint64_t end_of_rib_tx_time_;
    uint64_t last_route_published_time_;
    bool fallback_;
};

struct EndOfRibRxTimer : public ControllerTimer {
    EndOfRibRxTimer(Agent *agent);
    virtual ~EndOfRibRxTimer() { }

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const;
    virtual bool TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);
    void Reset();

    AgentXmppChannel *agent_xmpp_channel_;
    uint64_t end_of_rib_rx_time_;
    bool fallback_;
};
#endif
