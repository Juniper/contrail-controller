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
 * Cleanup Timer 
 * Used to manage timers required for removing stale entries for config and
 * routes received from control nodes.
 * Functionalities provided are - start, cancel, reschedule
 * Also derived structs need to implement the extension intervals for
 * rescheduling. 
 * Rescheduling is not done via cancel and start, but rather timer is restarted 
 * when it expires for the remaining timer interval.
 * Timer expired callback is done on expiration(as expected) and when
 * extension_interval is zero.
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
    void RescheduleTimer(AgentXmppChannel *agent_xmpp_channel);
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
    uint64_t extension_interval_;
    uint64_t last_restart_time_;
    AgentXmppChannel *agent_xmpp_channel_;
    bool running_;
    std::string timer_name_;
    uint32_t stale_timer_interval_;
};

struct UnicastCleanupTimer : public CleanupTimer {
    static const uint32_t kUnicastStaleTimer = (2 * 60 * 1000); 
    UnicastCleanupTimer(Agent *agent)
        : CleanupTimer(agent, "Agent Unicast Stale cleanup timer", 
                       kUnicastStaleTimer) { };
    virtual ~UnicastCleanupTimer() { }

    virtual uint32_t GetTimerInterval() const {
        return kUnicastStaleTimer;}
    virtual void TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);
};

struct MulticastCleanupTimer : public CleanupTimer {
    static const uint32_t kMulticastStaleTimer = (5 * 60 * 1000); 
    MulticastCleanupTimer(Agent *agent) 
        : CleanupTimer(agent, "Agent Multicast Stale cleanup timer",
                       kMulticastStaleTimer) { }
    virtual ~MulticastCleanupTimer() { }

    virtual uint32_t GetTimerInterval() const {return kMulticastStaleTimer;}
    virtual void TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);

    uint32_t peer_sequence_;
};

struct ConfigCleanupTimer : public CleanupTimer {
    static const int timeout_ = (15 * 60 * 1000); // In milli seconds5
    ConfigCleanupTimer(Agent *agent)
        : CleanupTimer(agent, "Agent Stale cleanup timer",
                       timeout_) { }
    virtual ~ConfigCleanupTimer() { }

    virtual uint32_t GetTimerInterval() const {return timeout_;}
    virtual void TimerExpirationDone();
    virtual uint64_t GetTimerExtensionValue(AgentXmppChannel *ch);
};

#endif
