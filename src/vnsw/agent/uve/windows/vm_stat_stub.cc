/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <uve/vm_stat_kvm.h>
#include <uve/vm_stat_docker.h>

VmStat::VmStat(Agent *agent, const boost::uuids::uuid &vm_uuid) {
}

VmStat::~VmStat() {
}

void VmStat::Start() {
}

void VmStat::Stop() {
    delete this;
}

void VmStat::ProcessData() {
}

bool VmStat::TimerExpiry() {
    return false;
}

VmStatKvm::VmStatKvm(Agent *agent, const boost::uuids::uuid &vm_uuid)
    : VmStat(agent, vm_uuid) {
}

VmStatKvm::~VmStatKvm() {
}

void VmStatKvm::Start() {
}

bool VmStatKvm::TimerExpiry() {
    return false;
}

VmStatDocker::VmStatDocker(Agent *agent, const boost::uuids::uuid &vm_uuid)
    : VmStat(agent, vm_uuid) {
}

VmStatDocker::~VmStatDocker() {
}

void VmStatDocker::Start() {
}

bool VmStatDocker::TimerExpiry() {
    return false;
}
