/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/uuid/uuid_io.hpp>
#include <vnc_cfg_types.h>
#include <base/util.h>

#include <cmn/agent_cmn.h>
#include <db/db_partition.h>
#include <cfg/cfg_init.h>
#include <oper/operdb_init.h>
#include <oper/agent_profile.h>

#include <oper/vn.h>
#include <oper/physical_device_vn.h>
#include <oper/interface_common.h>
#include <oper/sg.h>
#include <oper/vrf.h>
using namespace std;

AgentProfile::AgentProfile(Agent *agent, bool enable) :
    agent_(agent), timer_(NULL), enable_(enable) {

    TaskScheduler *task = TaskScheduler::GetInstance();
    timer_ = TimerManager::CreateTimer
        (*(agent_->event_manager())->io_service(), "Agent Profile",
         task->GetTaskId("agent_profile"), 0);
    if (enable) {
        timer_->Start(kProfileTimeout, boost::bind(&AgentProfile::TimerRun,
                                                   this));
    }
    time(&start_time_);
}

AgentProfile::~AgentProfile() {
    TimerManager::DeleteTimer(timer_);
}

bool AgentProfile::TimerRun() {
    Log();
    return true;
}

string GetProfileString(DBTable *table, const char *name) {
    stringstream str;
    str << setw(16) << name
        << " Size " << setw(6) << table->Size()
        << " Enqueue " << setw(6) << table->enqueue_count()
        << " Input " << setw(6) << table->input_count()
        << " Notify " << setw(6) << table->notify_count();
    return str.str();
}

string GetInterfaceProfileString(InterfaceTable *table, const char *name) {
    stringstream str;
    str << setw(4) << " LI " << setw(6) << table->li_count()
        << " VMI " << table->vmi_count() << " / "
        << setw(6) << table->active_vmi_count() << " / "
        << setw(6) << table->vmi_ifnode_to_req() << " / "
        << setw(6) << table->li_ifnode_to_req() << " / "
        << setw(6) << table->pi_ifnode_to_req();
    return GetProfileString(table, name) + str.str();
}

void AgentProfile::Log() {
    time_t now;
    time(&now);

    DBPartition *partition = agent_->db()->GetPartition(0);
    cout << "Time : " << setw(4) << (now - start_time_)
        << " #Request-Q " << partition->request_queue_len()
        << " #Total Request " << partition->total_request_count()
        << " #Max Request " << partition->max_request_queue_len()
        << endl;
    cout << "    " << GetInterfaceProfileString(agent_->interface_table(),
                                                "Interface") << endl;
    cout << "    " << GetProfileString(agent_->vn_table(), "VN") << endl;
    cout << "    " << GetProfileString(agent_->sg_table(), "SG") << endl;
    cout << "    " << GetProfileString(agent_->physical_device_vn_table(),
                                       "Dev-Vn") << endl;
    cout << "    " << GetProfileString(agent_->vrf_table(), "VRF") << endl;
    DBTable *link_db = static_cast<DBTable *>(agent_->db()->FindTable(IFMAP_AGENT_LINK_DB_NAME));
    cout << "    " << GetProfileString(link_db, "Cfg-Link") << endl;
    cout << endl;
}
