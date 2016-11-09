/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <cfg/cfg_init.h>
#include <cfg/cfg_types.h>
#include <port_ipc/port_ipc_types.h>
#include "config_stale_cleaner.h"
#include "port_ipc_handler.h"
#include "port_subscribe_table.h"

ConfigStaleCleaner::ConfigStaleCleaner(Agent *agent, TimerCallback callback) :
    agent_(agent),
    timeout_(kConfigStaleTimeout), audit_callback_(callback) {
}

ConfigStaleCleaner::~ConfigStaleCleaner() {
    // clean up the running timers
    for (std::set<Timer *>::iterator it = running_timer_list_.begin();
         it != running_timer_list_.end(); ++it) {
        TimerManager::DeleteTimer(*it);
    }
}

void ConfigStaleCleaner::StartStaleCleanTimer(int32_t version) {
    // create timer, to be deleted on completion
    Timer *timer =
        TimerManager::CreateTimer(
            *(agent_->event_manager())->io_service(), "Stale cleanup timer",
            TaskScheduler::GetInstance()->GetTaskId("db::DBTable"), 0, true);
    running_timer_list_.insert(timer);
    timer->Start(timeout_,
                 boost::bind(&ConfigStaleCleaner::StaleEntryTimeout, this,
                             version, timer));
}

bool ConfigStaleCleaner::StaleEntryTimeout(int32_t version, Timer *timer) {
    if (!audit_callback_.empty()) {
        audit_callback_(version);
    }
    running_timer_list_.erase(timer);
    return false;
}

////////////////////////////////////////////////////////////////////////////////

InterfaceConfigStaleCleaner::InterfaceConfigStaleCleaner(Agent *agent) :
    ConfigStaleCleaner(agent, NULL) {
}

InterfaceConfigStaleCleaner::~InterfaceConfigStaleCleaner() {
}

bool
InterfaceConfigStaleCleaner::OnInterfaceConfigStaleTimeout(int32_t version) {
    PortSubscribeTable *table =
        agent_->port_ipc_handler()->port_subscribe_table();
    if (!table) {
        return false;
    }

    CONFIG_TRACE(PortInfo, "InterfaceConfigStaleCleaner Walk invoked.");
    table->StaleWalk(version);
    return false;
}
