/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <port_ipc/config_stale_cleaner.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_interface.h>
#include <cfg/cfg_types.h>
#include <port_ipc/port_ipc_handler.h>

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
    ConfigStaleCleaner(agent, NULL), walkid_(DBTableWalker::kInvalidWalkerId) {
}

InterfaceConfigStaleCleaner::~InterfaceConfigStaleCleaner() {
    if (walkid_ != DBTableWalker::kInvalidWalkerId) {
        CFG_TRACE(IntfInfo, "InterfaceConfigStaleCleaner Walk cancelled.");
        agent_->db()->GetWalker()->WalkCancel(walkid_);
    }
}

bool
InterfaceConfigStaleCleaner::OnInterfaceConfigStaleTimeout(int32_t version) {
    DBTableWalker *walker = agent_->db()->GetWalker();
    if (walkid_ != DBTableWalker::kInvalidWalkerId) {
        CFG_TRACE(IntfInfo, "InterfaceConfigStaleCleaner Walk cancelled.");
        walker->WalkCancel(walkid_);
    }

    walkid_ = walker->WalkTable(agent_->interface_config_table(), NULL,
              boost::bind(&InterfaceConfigStaleCleaner::CfgIntfWalk, this,
                          _1, _2, version),
              boost::bind(&InterfaceConfigStaleCleaner::CfgIntfWalkDone, this,
                          version));
    CFG_TRACE(IntfInfo, "InterfaceConfigStaleCleaner Walk invoked.");
    return false;
}

bool InterfaceConfigStaleCleaner::CfgIntfWalk(DBTablePartBase *partition,
                                              DBEntryBase *entry,
                                              int32_t version) {
    const CfgIntEntry *cfg_intf = static_cast<const CfgIntEntry *>(entry);

    if (cfg_intf->port_type() == CfgIntEntry::CfgIntNameSpacePort) {
        CFG_TRACE(IntfInfo, "CfgIntfStaleWalk Skipping the delete of "
                "port type  " +
                cfg_intf->CfgIntTypeToString(cfg_intf->port_type()));
        return true;
    }

    if (cfg_intf->GetVersion() < version) {
        PortIpcHandler *pih = agent_->port_ipc_handler();
        if (!pih) {
            return true;
        }
        std::string msg;
        pih->DeletePort(UuidToString(cfg_intf->GetUuid()), msg);
    }
    return true;
}

void InterfaceConfigStaleCleaner::CfgIntfWalkDone(int32_t version) {
    walkid_ = DBTableWalker::kInvalidWalkerId;
    CFG_TRACE(IntfWalkDone, version);
}
