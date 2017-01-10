/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/timer.h"
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_trace.h>
#include "cmn/agent_cmn.h"
#include "xmpp/xmpp_init.h"
#include "pugixml/pugixml.hpp"
#include "oper/vrf.h"
#include "oper/peer.h"
#include "oper/mirror_table.h"
#include "oper/multicast.h"
#include "oper/operdb_init.h"
#include "oper/instance_manager.h"
#include "controller/controller_types.h"
#include "controller/controller_init.h"
#include "controller/controller_timer.h"
#include "controller/controller_peer.h"
#include "controller/controller_ifmap.h"

// Creates new timer
ControllerTimer::ControllerTimer(Agent *agent,
                                 const std::string &timer_name,
                                 uint32_t default_stale_timer_interval)
    : agent_(agent), last_restart_time_(0),
    xmpp_server_(""), timer_name_(timer_name),
    stale_timer_interval_(default_stale_timer_interval) {
    controller_timer_ = 
        TimerManager::CreateTimer(*(agent->event_manager()->
                                    io_service()), timer_name,
                                  TaskScheduler::GetInstance()->
                                  GetTaskId("Agent::ControllerXmpp"), 0);
}

ControllerTimer::~ControllerTimer() {
    //Delete timer
    if (controller_timer_) {
        TimerManager::DeleteTimer(controller_timer_);
    }
}

bool ControllerTimer::Cancel() {
    CONTROLLER_TRACE(Timer, "ControllerTimer", timer_name(), "");
    last_restart_time_ = 0;
    xmpp_server_.clear();

    if (controller_timer_ == NULL) {
        CONTROLLER_TRACE(Timer, "No controller timer", timer_name(), ""); 
        return true;
    }

    return controller_timer_->Cancel();
}

// Set the last_restart_time and update the owner of the timer with XmppServer
void ControllerTimer::Start(AgentXmppChannel *agent_xmpp_channel) {
    if (controller_timer_->running()) {
        if (controller_timer_->Cancel() == false) {
            CONTROLLER_TRACE(Timer, "Cancel during restart of timer failed",
                             timer_name(), agent_xmpp_channel->GetXmppServer());
        }
    } 

    // Start the timer fresh 
    controller_timer_->Start(GetTimerInterval(),
        boost::bind(&ControllerTimer::TimerExpiredCallback, this));
    CONTROLLER_TRACE(Timer, "Start", timer_name(), 
                     agent_xmpp_channel->GetXmppServer()); 

    last_restart_time_ = UTCTimestampUsec();
    xmpp_server_ = agent_xmpp_channel->GetXmppServer();
}

bool ControllerTimer::running() const {
    return controller_timer_->running();
}

// Start the timer again if extension interval is not zero.
// If extension interval is not present then execute the expiration
// handler.
bool ControllerTimer::TimerExpiredCallback() {

    bool ret = TimerExpirationDone();
    CONTROLLER_TRACE(Timer, "Called Timer Expiration Routine", timer_name(), 
                     xmpp_server_);

    //Reset all parameters
    xmpp_server_.clear();
    return ret;
}

// If timer is in running state 
uint64_t UnicastCleanupTimer::GetTimerExtensionValue(AgentXmppChannel *ch) {
    uint64_t ch_setup_time = agent_->controller_xmpp_channel_setup_time(ch->
                                                     GetXmppServerIdx());
    return (kUnicastStaleTimer - ((UTCTimestampUsec() - ch_setup_time) / 1000));
}

bool UnicastCleanupTimer::TimerExpirationDone() {
    agent_->controller()->UnicastCleanupTimerExpired();
    return false;
}

uint64_t MulticastCleanupTimer::GetTimerExtensionValue(AgentXmppChannel *ch) {
    return (kMulticastStaleTimer - ((UTCTimestampUsec() - 
                                     last_restart_time_) / 1000));
}

bool MulticastCleanupTimer::TimerExpirationDone() {
    agent_->controller()->MulticastCleanupTimerExpired(peer_sequence_);
    return false;
}

uint64_t ConfigCleanupTimer::GetTimerExtensionValue(AgentXmppChannel *ch) {
    return (timeout_ - ((UTCTimestampUsec() - last_restart_time_) / 1000));
}

bool ConfigCleanupTimer::TimerExpirationDone() {
    uint64_t seq =  agent_->ifmap_xmpp_channel(agent_->
            ifmap_active_xmpp_server_index())->GetSeqNumber();

    agent_->ifmap_stale_cleaner()->StaleTimeout(seq);
    agent_->oper_db()->instance_manager()->StaleTimeout();
    return false;
}

EndOfConfigTimer::EndOfConfigTimer(Agent *agent) :
    ControllerTimer(agent, "End of config identification timer",
                    kEndOfConfigTimeOut) {
    start_time_ = UTCTimestampUsec();    
}

void EndOfConfigTimer::Start(AgentXmppChannel *agent_xmpp_channel) {
    uint8_t count = 0;
    start_time_ = UTCTimestampUsec();    

    while (count < MAX_XMPP_SERVERS) {
        if (agent_->controller_xmpp_channel(count)) {
            agent_->controller_xmpp_channel(count)->end_of_rib_timer()->Cancel();
            agent_->controller()->StopEndOfRibWalker(count);
        }
        count++;
    }
    AgentIfMapXmppChannel *ifmap_channel = agent_->
        ifmap_xmpp_channel(agent_->ifmap_active_xmpp_server_index());
    if (ifmap_channel)
        ifmap_channel->end_of_config_params().Reset();
    ControllerTimer::Start(agent_xmpp_channel);
}

uint64_t EndOfConfigTimer::GetTimerExtensionValue(AgentXmppChannel *ch) {
    return (kEndOfConfigTimeOut - 
            ((UTCTimestampUsec() - last_restart_time_) / 1000));
}

bool EndOfConfigTimer::TimerExpirationDone() {
    //Identify silence of 30 seconds on config channel
    AgentIfMapXmppChannel *ifmap_channel = agent_->
        ifmap_xmpp_channel(agent_->ifmap_active_xmpp_server_index());
    uint64_t current_time = UTCTimestampUsec();
    bool config_seen = false;
    bool stale_cleanup = false;

    //If no config channel cancel end of config processing
    if (ifmap_channel == NULL)
        return false;

    if ((current_time - start_time_) >= EndOfConfigTimer::kFallbackTimeOut) {
        //stale_cleanup = true;
    }

    if ((ifmap_channel->end_of_config_params().update_receive_time_ == 0) ||
        (ifmap_channel->end_of_config_params().config_enqueued_time_ == 0))
        config_seen = true;

    uint64_t update_receive_time =
        ifmap_channel->end_of_config_params().update_receive_time_;
    if ((current_time - update_receive_time) <
        EndOfConfigTimer::kInactivityTime)
        config_seen = true;

    uint64_t config_enqueued_time_ =
        ifmap_channel->end_of_config_params().config_enqueued_time_;
    if ((current_time - config_enqueued_time_) <
        EndOfConfigTimer::kInactivityTime)
        config_seen = true;

    //Start stale config cleanup timer
    //Either on fallout or config not seen
    if (!config_seen || stale_cleanup)
        agent_->controller()->config_cleanup_timer().
            Start(agent_->controller_xmpp_channel(agent_->
                                              ifmap_active_xmpp_server_index()));
    //Start End of Rib timer
    if (!config_seen)
        agent_->controller()->StartEndOfRibTimer();
    return config_seen;
}

EndOfRibTimer::EndOfRibTimer(Agent *agent) :
    ControllerTimer(agent, "End of rib identification timer",
                    kEndOfRibTimeOut), eor_cn_sent_(false),
                    agent_xmpp_channel_(NULL) {
}

void EndOfRibTimer::Start(AgentXmppChannel *agent_xmpp_channel) {
    agent_xmpp_channel_ = agent_xmpp_channel;
    ControllerTimer::Start(agent_xmpp_channel);
}

uint64_t EndOfRibTimer::GetTimerExtensionValue(AgentXmppChannel *ch) {
    return (kEndOfRibTimeOut -
            ((UTCTimestampUsec() - last_restart_time_) / 1000));
}

bool EndOfRibTimer::TimerExpirationDone() {
    uint64_t start_time = UTCTimestampUsec();    
    if (!eor_cn_sent_ &&
        ((start_time - agent_xmpp_channel_->
          route_published_time()) > EndOfRibTimer::kInactivityTime)) {
        agent_->controller()->StartEndOfRibWalker(agent_xmpp_channel_->
                                                  GetXmppServerIdx());
        eor_cn_sent_ = true;
    }
    return true;
}
