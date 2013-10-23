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
#include "ksync/vxlan_ksync.h"
#include "ksync/sandesh_ksync.h"
#include "nl_util.h"
#include "vhost.h"
#include "vr_message.h"
#include "vnswif_listener.h"

#define	VNSW_GENETLINK_FAMILY_NAME  "vnsw"

static int Encode(Sandesh &encoder, uint8_t *buf, int buf_len) {
    int len, error;
    len = encoder.WriteBinary(buf, buf_len, &error);
    return len;
}

void GenericNetlinkInit() {
    struct nl_client    *cl;
    int    family;
    
    assert((cl = nl_register_client()) != NULL);
    assert(nl_socket(cl, NETLINK_GENERIC) >= 0);

    family = vrouter_get_family_id(cl);
    LOG(DEBUG, "Vrouter family is " << family);
    KSyncSock::SetNetlinkFamilyId(family);
    nl_free_client(cl);
    return;
}

void KSync::RegisterDBClients(DB *db) {
    KSyncObjectManager::Init();
    IntfKSyncObject::Init(Agent::GetInstance()->GetInterfaceTable());
    VrfKSyncObject::Init(Agent::GetInstance()->GetVrfTable());
    NHKSyncObject::Init(Agent::GetInstance()->GetNextHopTable());
    MplsKSyncObject::Init(Agent::GetInstance()->GetMplsTable());
    MirrorKSyncObject::Init(Agent::GetInstance()->GetMirrorTable());
    VrfAssignKSyncObject::Init(Agent::GetInstance()->GetVrfAssignTable());
    VxLanKSyncObject::Init(Agent::GetInstance()->GetVxLanTable());
    FlowTableKSyncObject::Init();
    Agent::GetInstance()->SetRouterIdConfigured(false);
}

void KSync::NetlinkInit() {
    EventManager *event_mgr;

    event_mgr = Agent::GetInstance()->GetEventManager();
    boost::asio::io_service &io = *event_mgr->io_service();

    KSyncSockNetlink::Init(io, DB::PartitionCount(), NETLINK_GENERIC);
    KSyncSock::SetAgentSandeshContext(new KSyncSandeshContext);

    GenericNetlinkInit();
}

void KSync::VRouterInterfaceSnapshot() {
    InterfaceKSnap::Init();

    int len = 0;
    KSyncSandeshContext *ctxt = static_cast<KSyncSandeshContext *>
                                (KSyncSock::GetAgentSandeshContext());
    ctxt->Reset();
    KSyncSock *sock = KSyncSock::Get(0);
    do {
        vr_interface_req req;
        req.set_h_op(sandesh_op::DUMP);
        req.set_vifr_idx(0);
        req.set_vifr_marker(ctxt->GetContextMarker());
        uint8_t msg[KSYNC_DEFAULT_MSG_SIZE];
        len = Encode(req, msg, KSYNC_DEFAULT_MSG_SIZE);
        sock->BlockingSend((char *)msg, len);
        if (sock->BlockingRecv()) {
            LOG(ERROR, "Error getting interface dump from VROUTER");
            return;
        }
    } while (ctxt->GetResponseCode() & VR_MESSAGE_DUMP_INCOMPLETE);
    ctxt->Reset();
}

void KSync::ResetVRouter() {
    int len = 0;
    vrouter_ops encoder;
    encoder.set_h_op(sandesh_op::RESET);
    uint8_t msg[KSYNC_DEFAULT_MSG_SIZE];
    len = Encode(encoder, msg, KSYNC_DEFAULT_MSG_SIZE);

    KSyncSock *sock = KSyncSock::Get(0);
    sock->BlockingSend((char *)msg, len);
    if (sock->BlockingRecv()) {
        LOG(ERROR, "Error resetting VROUTER. Skipping KSync Start");
        return;
    }

    KSyncSock::Start();
}

void KSync::VnswIfListenerInit() {
    EventManager *event_mgr;

    event_mgr = Agent::GetInstance()->GetEventManager();
    boost::asio::io_service &io = *event_mgr->io_service();
    VnswIfListener::Init(io);
}

void KSync::CreateVhostIntf() {
    struct  nl_client *cl;

    assert((cl = nl_register_client()) != NULL);
    assert(nl_socket(cl, NETLINK_ROUTE) > 0);

    struct vn_if ifm;
    struct nl_response *resp;

    memset(&ifm, 0, sizeof(ifm));
    strncpy(ifm.if_name, Agent::GetInstance()->GetVirtualHostInterfaceName().c_str(),
	    IFNAMSIZ);
    ifm.if_name[IFNAMSIZ - 1] = '\0';
    strcpy(ifm.if_kind, VHOST_KIND);
    ifm.if_flags = IFF_UP;

    assert(nl_build_if_create_msg(cl, &ifm, 1) == 0);
    assert(nl_sendmsg(cl) > 0);
    assert(nl_recvmsg(cl) > 0);
    assert((resp = nl_parse_reply(cl)) != NULL);
    assert(resp->nl_type == NL_MSG_TYPE_ERROR);
    nl_free_client(cl);
}

void KSync::UpdateVhostMac() {
    struct  nl_client *cl;

    assert((cl = nl_register_client()) != NULL);
    assert(nl_socket(cl, NETLINK_ROUTE) > 0);

    struct vn_if ifm;
    struct nl_response *resp;

    memset(&ifm, 0, sizeof(ifm));
    strncpy(ifm.if_name, Agent::GetInstance()->GetVirtualHostInterfaceName().c_str(),
	    IFNAMSIZ);
    ifm.if_name[IFNAMSIZ - 1] = '\0';
    strcpy(ifm.if_kind, VHOST_KIND);
    ifm.if_flags = IFF_UP;
    GetPhyMac(Agent::GetInstance()->GetIpFabricItfName().c_str(), ifm.if_mac);
    assert(nl_build_if_create_msg(cl, &ifm, 1) == 0);
    assert(nl_sendmsg(cl) > 0);
    assert(nl_recvmsg(cl) > 0);
    assert((resp = nl_parse_reply(cl)) != NULL);
    assert(resp->nl_type == NL_MSG_TYPE_ERROR);
    nl_free_client(cl);
}

void KSync::Shutdown() {
    IntfKSyncObject::Shutdown();
    VrfKSyncObject::Shutdown();
    NHKSyncObject::Shutdown();
    MplsKSyncObject::Shutdown();
    KSyncSock::Shutdown();
    FlowTableKSyncObject::Shutdown();
    MirrorKSyncObject::Shutdown();
    VrfAssignKSyncObject::Shutdown();
    VxLanKSyncObject::Shutdown();
    KSyncObjectManager::Shutdown();
}
