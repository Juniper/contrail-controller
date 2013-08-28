/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/if_ether.h>

#include <net/if.h>
#include <netinet/ether.h>

#include <io/event_manager.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <cmn/agent_cmn.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_sock.h>

#include "ksync_init.h"
#include "ksync/interface_ksync.h"
#include "ksync/nexthop_ksync.h"
#include "ksync/mpls_ksync.h"
#include "ksync/route_ksync.h"
#include "ksync/flowtable_ksync.h"
#include "ksync/mirror_ksync.h"
#include "ksync/vrf_assign_ksync.h"
#include "nl_util.h"
#include "vnswif_listener.h"
#include "ksync/sandesh_ksync.h"

void GenericNetlinkInitTest() {
    LOG(DEBUG, "Vrouter family is 24");
    KSyncSock::SetNetlinkFamilyId(24);
    return;
}

void KSync::RegisterDBClientsTest(DB *db) {
    KSyncObjectManager::Init();
    IntfKSyncObject::InitTest(Agent::GetInterfaceTable());
    RouteKSyncObject::Init(Agent::GetVrfTable());
    NHKSyncObject::Init(Agent::GetNextHopTable());
    MplsKSyncObject::Init(Agent::GetMplsTable());
    MirrorKSyncObject::Init(Agent::GetMirrorTable());
    FlowTableKSyncObject::InitTest();
    VrfAssignKSyncObject::Init(Agent::GetVrfAssignTable());
    Agent::SetRouterIdConfigured(false);
}

void KSync::NetlinkInitTest() {
    EventManager *event_mgr;

    event_mgr = Agent::GetEventManager();
    boost::asio::io_service &io = *event_mgr->io_service();

    KSyncSock::Init(io, 1, 0);
    KSyncSock::SetAgentSandeshContext(new KSyncSandeshContext);

    GenericNetlinkInitTest();
    KSyncSock::Start();
}

void KSync::NetlinkShutdownTest() {
    KSyncSock::Shutdown();
    delete KSyncSock::GetAgentSandeshContext();
    KSyncSock::SetAgentSandeshContext(NULL);
}
