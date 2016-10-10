/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#include <port_ipc/config_stale_cleaner.h>
#include <cfg/cfg_init.h>
#include <cfg/cfg_types.h>
#include <cfg/cfg_interface.h>
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
              boost::bind(&InterfaceConfigStaleCleaner::WalkNotify, this,
                          _1, _2, version),
              boost::bind(&InterfaceConfigStaleCleaner::WalkDone, this,
                          version));
    CFG_TRACE(IntfInfo, "InterfaceConfigStaleCleaner Walk invoked.");
    return false;
}

bool InterfaceConfigStaleCleaner::WalkNotify(DBTablePartBase *partition,
                                             DBEntryBase *e,
                                             int32_t version) {
    const InterfaceConfigEntry *ipc = static_cast<const InterfaceConfigEntry *>(e);
    const InterfaceConfigVmiEntry *entry =
        dynamic_cast<const InterfaceConfigVmiEntry *>(e);
    if (entry == NULL) {
        CFG_TRACE(IntfInfo, "CfgIntfStaleWalk Skipping the delete of "
                "key type " + ipc->TypeToString(ipc->key_type()))
    }

    if (entry->vmi_type() == InterfaceConfigVmiEntry::NAMESPACE_PORT) {
        CFG_TRACE(IntfInfo, "CfgIntfStaleWalk Skipping the delete of "
                "port type  " + entry->VmiTypeToString(entry->vmi_type()));
        return true;
    }

    if (entry->version() < version) {
        PortIpcHandler *pih = agent_->port_ipc_handler();
        if (!pih) {
            return true;
        }
        std::string msg;
        pih->DeleteVmiUuidEntry(entry->vmi_uuid(), msg);
    }
    return true;
}

void InterfaceConfigStaleCleaner::WalkDone(int32_t version) {
    walkid_ = DBTableWalker::kInvalidWalkerId;
    CFG_TRACE(IntfWalkDone, version);
}
