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
#include "controller/controller_cleanup_timer.h"
#include "controller/controller_peer.h"
#include "controller/controller_ifmap.h"

// Creates new timer
CleanupTimer::CleanupTimer(Agent *agent, const std::string &timer_name,
                           uint32_t default_stale_timer_interval)
    : agent_(agent), last_restart_time_(0),
    xmpp_server_(""), timer_name_(timer_name),
    stale_timer_interval_(default_stale_timer_interval) {
    cleanup_timer_ = 
        TimerManager::CreateTimer(*(agent->event_manager()->
                                    io_service()), timer_name,
                                  TaskScheduler::GetInstance()->
                                  GetTaskId("Agent::ControllerXmpp"), 0);
}

CleanupTimer::~CleanupTimer() {
    //Delete timer
    if (cleanup_timer_) {
        TimerManager::DeleteTimer(cleanup_timer_);
    }
}

bool CleanupTimer::Cancel() {
    CONTROLLER_TRACE(Timer, "Cleanup ", timer_name(), "");
    last_restart_time_ = 0;
    xmpp_server_.clear();

    if (cleanup_timer_ == NULL) {
        CONTROLLER_TRACE(Timer, "No Cleanup timer", timer_name(), ""); 
        return true;
    }

    return cleanup_timer_->Cancel();
}

// Set the last_restart_time and update the owner of the timer with XmppServer
void CleanupTimer::Start(AgentXmppChannel *agent_xmpp_channel) {
    if (cleanup_timer_->running()) {
        if (cleanup_timer_->Cancel() == false) {
            CONTROLLER_TRACE(Timer, "Cancel during restart of timer failed",
                             timer_name(), agent_xmpp_channel->GetXmppServer());
        }
    } 

    // Start the timer fresh 
    cleanup_timer_->Start(GetTimerInterval(),
        boost::bind(&CleanupTimer::TimerExpiredCallback, this));
    CONTROLLER_TRACE(Timer, "Start", timer_name(), 
                     agent_xmpp_channel->GetXmppServer()); 

    last_restart_time_ = UTCTimestampUsec();
    xmpp_server_ = agent_xmpp_channel->GetXmppServer();
}

// Start the timer again if extension interval is not zero.
// If extension interval is not present then execute the expiration
// handler.
bool CleanupTimer::TimerExpiredCallback() {

    TimerExpirationDone();
    CONTROLLER_TRACE(Timer, "Called Timer Expiration Routine", timer_name(), 
                     xmpp_server_);

    //Reset all parameters
    xmpp_server_.clear();
    return false;
}

// If timer is in running state 
uint64_t UnicastCleanupTimer::GetTimerExtensionValue(AgentXmppChannel *ch) {
    uint64_t ch_setup_time = agent_->controller_xmpp_channel_setup_time(ch->
                                                     GetXmppServerIdx());
    return (kUnicastStaleTimer - ((UTCTimestampUsec() - ch_setup_time) / 1000));
}

void UnicastCleanupTimer::TimerExpirationDone() {
    agent_->controller()->UnicastCleanupTimerExpired();
}

uint64_t MulticastCleanupTimer::GetTimerExtensionValue(AgentXmppChannel *ch) {
    return (kMulticastStaleTimer - ((UTCTimestampUsec() - 
                                     last_restart_time_) / 1000));
}

void MulticastCleanupTimer::TimerExpirationDone() {
    agent_->controller()->MulticastCleanupTimerExpired(peer_sequence_);
}

uint64_t ConfigCleanupTimer::GetTimerExtensionValue(AgentXmppChannel *ch) {
    return (timeout_ - ((UTCTimestampUsec() - last_restart_time_) / 1000));
}

void ConfigCleanupTimer::TimerExpirationDone() {
    uint64_t seq =  agent_->ifmap_xmpp_channel(agent_->
            ifmap_active_xmpp_server_index())->GetSeqNumber();

    agent_->ifmap_stale_cleaner()->StaleTimeout(seq);
    agent_->oper_db()->instance_manager()->StaleTimeout();
}
