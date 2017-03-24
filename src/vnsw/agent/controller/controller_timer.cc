/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "base/logging.h"
#include "base/timer.h"
#include <sandesh/sandesh.h>
#include <sandesh/sandesh_types.h>
#include <sandesh/sandesh_trace.h>
#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
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

#define SECS_TO_USECS(t) t * 1000 * 1000
#define SECS_TO_MSECS(t) t * 1000

// Creates new timer
ControllerTimer::ControllerTimer(Agent *agent,
                                 const std::string &timer_name,
                                 uint32_t timer_interval)
    : agent_(agent), last_restart_time_(0),
    xmpp_server_(""), timer_name_(timer_name),
    timer_interval_(timer_interval) {
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

void ControllerTimer::Fire() {
    controller_timer_->Fire();
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

ConfigCleanupTimer::ConfigCleanupTimer(Agent *agent) :
    ControllerTimer(agent, "Agent Config Stale cleanup timer",
                    SECS_TO_MSECS(agent->params()->
                                  llgr_params().stale_config_cleanup_time())),
    sequence_number_(0) {
}

void ConfigCleanupTimer::Start(AgentXmppChannel *channel) {
    sequence_number_ =
        agent_->ifmap_xmpp_channel(agent_->ifmap_active_xmpp_server_index())->
        GetSeqNumber();

    ControllerTimer::Start(channel);
}

bool ConfigCleanupTimer::TimerExpirationDone() {
    agent_->ifmap_stale_cleaner()->StaleTimeout(sequence_number_);
    agent_->oper_db()->instance_manager()->StaleTimeout();
    return false;
}

uint32_t ConfigCleanupTimer::GetTimerInterval() const {
    return SECS_TO_MSECS(agent_->params()->
                         llgr_params().stale_config_cleanup_time());
}

EndOfConfigTimer::EndOfConfigTimer(Agent *agent,
                                   AgentIfMapXmppChannel *config_channel) :
    ControllerTimer(agent, "End of config identification timer",
                    SECS_TO_MSECS(agent->params()->
                                  llgr_params().config_poll_time())),
    config_channel_(config_channel), last_config_receive_time_(0),
    inactivity_detected_time_(0), end_of_config_processed_time_(0),
    fallback_(false) {
}

void EndOfConfigTimer::Reset() {
    last_config_receive_time_ = 0;
    inactivity_detected_time_ = 0;
    end_of_config_processed_time_ = 0;
    fallback_ = false;
}

void EndOfConfigTimer::Start(AgentXmppChannel *agent_xmpp_channel) {
    Reset();
    agent_->controller()->StopEndOfRibTx();
    ControllerTimer::Start(agent_xmpp_channel);
}

//Identify silence of 30 seconds on config channel
bool EndOfConfigTimer::TimerExpirationDone() {
    uint64_t current_time = UTCTimestampUsec();
    //Config has been seen within inactivity time
    bool config_seen = false;

    if (last_config_receive_time_ != 0) {
        if ((current_time - last_config_receive_time_) <
            GetInactivityInterval()) {
            config_seen = true;
        }
    }

    //When config is regularly seen on channel, put stale timeout to worst.
    if ((current_time - last_restart_time_) >= GetFallbackInterval()) {
        fallback_ = true;
    }

    // End of config is enqueued in VNController work queue for processing.
    if (fallback_ || !config_seen) {
        inactivity_detected_time_ = UTCTimestampUsec();
        config_channel_->EnqueueEndOfConfig();
        return false;
    }
    return true;
}

uint32_t EndOfConfigTimer::GetTimerInterval() const {
    return SECS_TO_MSECS(agent_->params()->
                         llgr_params().config_poll_time());
}

uint32_t EndOfConfigTimer::GetFallbackInterval() const {
    return SECS_TO_USECS(agent_->params()->
                         llgr_params().config_fallback_time());
}

uint32_t EndOfConfigTimer::GetInactivityInterval() const {
    return SECS_TO_USECS(agent_->params()->
                         llgr_params().config_inactivity_time());
}

EndOfRibTxTimer::EndOfRibTxTimer(Agent *agent) :
    ControllerTimer(agent, "End of rib Tx identification timer",
                    SECS_TO_MSECS(agent->params()->
                                  llgr_params().end_of_rib_tx_poll_time())),
    agent_xmpp_channel_(NULL), end_of_rib_tx_time_(0),
    last_route_published_time_(0), fallback_(false) {
}

void EndOfRibTxTimer::Reset() {
    end_of_rib_tx_time_ = 0;
    last_route_published_time_ = 0;
    fallback_ = false;
}

void EndOfRibTxTimer::Start(AgentXmppChannel *agent_xmpp_channel) {
    Reset();
    agent_xmpp_channel_ = agent_xmpp_channel;
    ControllerTimer::Start(agent_xmpp_channel);
}

bool EndOfRibTxTimer::TimerExpirationDone() {
    uint64_t current_time = UTCTimestampUsec();
    uint8_t index = agent_xmpp_channel_->GetXmppServerIdx();
    bool end_of_rib = false;

    // Check for fallback
    if (last_route_published_time_ == 0) {
        if ((current_time - agent_->controller_xmpp_channel_setup_time(index)) >
            GetFallbackInterval()) {
            end_of_rib = true;
        }  else {
            return true;
        }
    }

    // Check for inactivity
    if ((current_time - last_route_published_time_) > GetInactivityInterval()) {
        end_of_rib = true;
    }

    if (end_of_rib) {
        // Start walk to delete stale routes.
        agent_xmpp_channel_->StartEndOfRibTxWalker();
        //Notify
        agent_->event_notifier()->
            Notify(new EventNotifyKey(EventNotifyKey::END_OF_RIB));
        return false;
    }

    return true;
}

uint32_t EndOfRibTxTimer::GetTimerInterval() const {
    return SECS_TO_MSECS(agent_->params()->
                         llgr_params().end_of_rib_tx_poll_time());
}

uint32_t EndOfRibTxTimer::GetFallbackInterval() const {
    return SECS_TO_USECS(agent_->params()->
                         llgr_params().end_of_rib_tx_fallback_time());
}

uint32_t EndOfRibTxTimer::GetInactivityInterval() const {
    return SECS_TO_USECS(agent_->params()->
                         llgr_params().end_of_rib_tx_inactivity_time());
}

EndOfRibRxTimer::EndOfRibRxTimer(Agent *agent) :
    ControllerTimer(agent, "End of rib Rx path timer",
              SECS_TO_MSECS(agent->params()->
                            llgr_params().end_of_rib_rx_fallback_time())),
              agent_xmpp_channel_(NULL), end_of_rib_rx_time_(0),
              fallback_(false) {
}

void EndOfRibRxTimer::Start(AgentXmppChannel *agent_xmpp_channel) {
    agent_xmpp_channel_ = agent_xmpp_channel;
    ControllerTimer::Start(agent_xmpp_channel);
}

uint32_t EndOfRibRxTimer::GetTimerInterval() const {
    return SECS_TO_MSECS(agent_->params()->
                         llgr_params().end_of_rib_rx_fallback_time());
}

bool EndOfRibRxTimer::TimerExpirationDone() {
    end_of_rib_rx_time_ = UTCTimestampUsec();
    fallback_ = true;
    agent_xmpp_channel_->EndOfRibRx();
    return false;
}

void EndOfRibRxTimer::Reset() {
    end_of_rib_rx_time_ = 0;
    fallback_ = false;
}
