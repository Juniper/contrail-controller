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
#include <ksync/ksync_sock_user.h>

#include "ksync_init.h"
#include "ksync/interface_ksync.h"
#include "ksync/mpls_ksync.h"
#include "ksync/route_ksync.h"
#include "ksync/flowtable_ksync.h"
#include "ksync/mirror_ksync.h"
#include "ksync/vrf_assign_ksync.h"
#include "ksync/vxlan_ksync.h"
#include "vnswif_listener.h"
#include "ksync/sandesh_ksync.h"

void KSync::InitTest() {
    interface_ksync_obj_.get()->InitTest();
    flowtable_ksync_obj_.get()->InitTest();
}

void GenericNetlinkInitTest() {
    LOG(DEBUG, "Vrouter family is 24");
    KSyncSock::SetNetlinkFamilyId(24);
    return;
}

void KSync::RegisterDBClientsTest(DB *db) {
    KSyncObjectManager::Init();
    interface_ksync_obj_.get()->RegisterDBClients();
    vrf_ksync_obj_.get()->RegisterDBClients();
    nh_ksync_obj_.get()->RegisterDBClients();
    mpls_ksync_obj_.get()->RegisterDBClients();
    mirror_ksync_obj_.get()->RegisterDBClients();
    vrf_assign_ksync_obj_.get()->RegisterDBClients();
    vxlan_ksync_obj_.get()->RegisterDBClients();
    agent_->SetRouterIdConfigured(false);
}

void KSync::NetlinkInitTest(bool sync_mode) {
    EventManager *event_mgr;

    event_mgr = agent_->GetEventManager();
    boost::asio::io_service &io = *event_mgr->io_service();

    KSyncSockTypeMap::Init(io, 1);
    KSyncSock::SetAgentSandeshContext(new KSyncSandeshContext
                                                (flowtable_ksync_obj_.get()));

    GenericNetlinkInitTest();
    KSyncSock::Start(sync_mode);
}

void KSync::NetlinkShutdownTest() {
    KSyncSock::Shutdown();
    delete KSyncSock::GetAgentSandeshContext();
    KSyncSock::SetAgentSandeshContext(NULL);
}
