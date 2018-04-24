/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#include <net/if.h>

#if defined(__linux__)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/if_ether.h>
#include <netinet/ether.h>
#endif

#include <io/event_manager.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <cmn/agent_cmn.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>
#include <ksync/ksync_sock_user.h>

#include "vrouter/ksync/interface_ksync.h"
#include "vrouter/ksync/mpls_ksync.h"
#include "vrouter/ksync/route_ksync.h"
#include "vrouter/ksync/flowtable_ksync.h"
#include "vrouter/ksync/mirror_ksync.h"
#include "vrouter/ksync/vrf_assign_ksync.h"
#include "vrouter/ksync/vxlan_ksync.h"
#include "vrouter/ksync/sandesh_ksync.h"
#include "vrouter/ksync/test/ksync_test.h"

KSyncTest::KSyncTest(Agent *agent)
    : KSync(agent) {
}

KSyncTest::~KSyncTest() {
}

void KSyncTest::Init(bool create_vhost) {
    interface_ksync_obj_.get()->InitTest();
    ksync_flow_memory_.get()->InitTest();
    NetlinkInitTest();
    ksync_bridge_memory_.get()->InitTest();
}

void KSyncTest::Shutdown() {
    ksync_flow_memory_.get()->Shutdown();
    ksync_bridge_memory_.get()->Shutdown();
    NetlinkShutdownTest();
}

void KSyncTest::RegisterDBClients(DB *db) {
    KSyncObjectManager::Init();
    interface_ksync_obj_.get()->RegisterDBClients();
    vrf_ksync_obj_.get()->RegisterDBClients();
    nh_ksync_obj_.get()->RegisterDBClients();
    mpls_ksync_obj_.get()->RegisterDBClients();
    mirror_ksync_obj_.get()->RegisterDBClients();
    vrf_assign_ksync_obj_.get()->RegisterDBClients();
    vxlan_ksync_obj_.get()->RegisterDBClients();
    agent_->set_router_id_configured(false);
}

void KSyncTest::GenericNetlinkInitTest() {
    LOG(DEBUG, "Vrouter family is 24");
    KSyncSock::SetNetlinkFamilyId(24);

    int len = 0;
    vrouter_ops encoder;
    encoder.set_h_op(sandesh_op::GET);
    uint8_t msg[KSYNC_DEFAULT_MSG_SIZE];
    len = Encode(encoder, msg, KSYNC_DEFAULT_MSG_SIZE);

    KSyncSockTypeMap::SetVRouterOps(encoder);
    //Get configured mpls, vmi, vni and nexthop parameters
    //from vrouter
    KSyncSock *sock = KSyncSock::Get(0);
    sock->BlockingSend((char *)msg, len);
    if (sock->BlockingRecv()) {
        LOG(ERROR, "Error getting configured parameter for vrouter");
    }
    return;
}

void KSyncTest::NetlinkInitTest() {
    EventManager *event_mgr;

    event_mgr = agent_->event_manager();
    boost::asio::io_service &io = *event_mgr->io_service();

    KSyncSockTypeMap::Init(io);
    for (int i = 0; i < KSyncSock::kRxWorkQueueCount; i++) {
        KSyncSock::SetAgentSandeshContext
            (new KSyncSandeshContext(this), i);
    }

    GenericNetlinkInitTest();
    KSyncSock::Start(agent_->ksync_sync_mode());
}

void KSyncTest::NetlinkShutdownTest() {
    KSyncSock::Shutdown();
    for (int i = 0; i < KSyncSock::kRxWorkQueueCount; i++) {
        delete KSyncSock::GetAgentSandeshContext(i);
        KSyncSock::SetAgentSandeshContext(NULL, i);
    }
}
