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
 * Controller Timer - abstract class.
 * Used to manage timers required for removing stale entries for config and
 * routes received from control nodes and end of config
 * Functionalities provided are - start, cancel,
 *
 */
struct ControllerTimer {
    ControllerTimer(Agent *agent, const std::string &timer_name,
                    uint32_t timer_interval);
    virtual ~ControllerTimer();

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual bool TimerExpirationDone() {return false;}
    virtual uint32_t GetTimerInterval() const = 0;
    virtual uint32_t timer_interval() {
        return timer_interval_;
    }
    virtual void set_timer_interval(uint32_t timer_interval) {
        timer_interval_ = timer_interval;
    }

    bool Cancel();
    void Fire();
    bool TimerExpiredCallback();
    const std::string& timer_name() const {return timer_name_;}
    bool running() const;

    Agent *agent_;
    // Timer
    Timer *controller_timer_;
    // Last time when timer was started
    uint64_t last_restart_time_;
    std::string xmpp_server_;
    std::string timer_name_;
    uint32_t timer_interval_;
};

/*
 * ConfigCleanupTimer
 *
 * Using the sequence number clean all stale configs(All config with lesser
 * sequence number are deleted)
 * Timer is started when end of config is determined.
 * sequence_number_ - picked from agent ifmap xmpp channel.
 */
struct ConfigCleanupTimer : public ControllerTimer {
    ConfigCleanupTimer(Agent *agent);
    virtual ~ConfigCleanupTimer() { }

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const;
    virtual bool TimerExpirationDone();

    uint64_t sequence_number_;
};

/*
 * EndOfConfigTimer
 *
 * On config channel becoming ready, this timer is started.
 * Its determined by identifying inactivity(not receiving config) on config channel.
 * Fallback time - If config is continuously being received or no config is
 * sent, then there is a fallback for identifying end of config.
 *
 * On end of config start end of rib tx timer, walker to notify routes and
 * config cleanup.
 */
struct EndOfConfigTimer : public ControllerTimer {
    EndOfConfigTimer(Agent *agent, AgentIfMapXmppChannel *channel);
    virtual ~EndOfConfigTimer() { }

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const;
    virtual bool TimerExpirationDone();

    uint32_t GetFallbackInterval() const;
    uint32_t GetInactivityInterval() const;
    void Reset();
    void GresEnabled(bool enable);

    AgentIfMapXmppChannel *config_channel_;
    uint64_t last_config_receive_time_;
    uint64_t inactivity_detected_time_;
    uint64_t end_of_config_processed_time_;
    bool fallback_;
    uint64_t config_inactivity_time_;
    uint64_t fallback_interval_;
};

/*
 * EndOfRibTxTimer
 *
 * Started on identifying end of config.
 * End of config starts a walk which will export routes. Each export is time
 * stamped so that inactivity of route export can be determined. If there is no
 * export seen for end-of-rib-tx time then end-of-rib-tx is concluded.
 * Fallback is also present in case route updates never stop for some reason.
 *
 * At the end of rib walk EOR is sent to control node.
 */
struct EndOfRibTxTimer : public ControllerTimer {
    EndOfRibTxTimer(Agent *agent);
    virtual ~EndOfRibTxTimer() { }

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const;
    virtual bool TimerExpirationDone();

    uint32_t GetFallbackInterval() const;
    uint32_t GetInactivityInterval() const;
    void Reset();
    void GresEnabled(bool enable);

    AgentXmppChannel *agent_xmpp_channel_;
    uint64_t end_of_rib_tx_time_;
    uint64_t last_route_published_time_;
    bool fallback_;
    uint64_t fallback_interval_;
};

/*
 * EndOfRibRxTimer
 *
 * This timer is used as fallback only.
 * It observes for EOR from control node. In case it is not seen then on
 * fallback assume that EOR is received. If EOR is received from CN then this
 * timer is cancelled.
 * On expiration stale walk to remove stale route paths for this parent peer.
 */
struct EndOfRibRxTimer : public ControllerTimer {
    EndOfRibRxTimer(Agent *agent);
    virtual ~EndOfRibRxTimer() { }

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const;
    virtual bool TimerExpirationDone();
    void Reset();
    void GresEnabled(bool enable);

    AgentXmppChannel *agent_xmpp_channel_;
    uint64_t end_of_rib_rx_time_;
    uint64_t end_of_rib_rx_fallback_time_;
    bool fallback_;
};

/*
 * LlgrStaleTimer
 *
 * When CN is down and this timer expires then stales will be cleaned.
 * It is the maximum time for which stale will be retained after CN is not
 * ready. This timer has no meaning when CN is ready as end-of-rib rx timer is
 * responsible for flushing out routes.
 */
struct LlgrStaleTimer : public ControllerTimer {
    LlgrStaleTimer(Agent *agent);
    virtual ~LlgrStaleTimer() { }

    virtual void Start(AgentXmppChannel *agent_xmpp_channel);
    virtual uint32_t GetTimerInterval() const;
    virtual bool TimerExpirationDone();
    void Reset();
    void GresEnabled(bool enable);

    AgentXmppChannel *agent_xmpp_channel_;
    uint64_t llgr_stale_time_;
};
#endif
