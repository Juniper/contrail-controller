/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/asio.hpp>
#include <boost/bind.hpp>

#if defined(__linux__)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/if_ether.h>
#include <netinet/ether.h>
#elif defined(__FreeBSD__)
#include <sys/socket.h>
#include <sys/ioctl.h>
#include "vr_os.h"
#endif

#include <net/if.h>

#include <io/event_manager.h>
#include <db/db_entry.h>
#include <db/db_table.h>
#include <db/db_table_partition.h>
#include <cmn/agent_cmn.h>
#include <pkt/flow_proto.h>
#include <ksync/ksync_index.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <ksync/ksync_sock.h>

#include "ksync_init.h"
#include "vrouter/ksync/interface_ksync.h"
#include "vrouter/ksync/route_ksync.h"
#include "vrouter/ksync/mirror_ksync.h"
#include "vrouter/ksync/vrf_assign_ksync.h"
#include "vrouter/ksync/vxlan_ksync.h"
#include "vrouter/ksync/sandesh_ksync.h"
#include "vrouter/ksync/qos_queue_ksync.h"
#include "vrouter/ksync/forwarding_class_ksync.h"
#include "vrouter/ksync/qos_config_ksync.h"
#include "nl_util.h"
#include "vhost.h"
#include "vr_message.h"

#define	VNSW_GENETLINK_FAMILY_NAME  "vnsw"

KSync::KSync(Agent *agent)
    : agent_(agent), interface_ksync_obj_(new InterfaceKSyncObject(this)),
      flow_table_ksync_obj_list_(),
      mpls_ksync_obj_(new MplsKSyncObject(this)),
      nh_ksync_obj_(new NHKSyncObject(this)),
      mirror_ksync_obj_(new MirrorKSyncObject(this)),
      vrf_ksync_obj_(new VrfKSyncObject(this)),
      vxlan_ksync_obj_(new VxLanKSyncObject(this)),
      vrf_assign_ksync_obj_(new VrfAssignKSyncObject(this)),
      interface_scanner_(new InterfaceKScan(agent)),
      vnsw_interface_listner_(new VnswInterfaceListener(agent)),
      ksync_flow_memory_(new KSyncFlowMemory(this)),
      ksync_flow_index_manager_(new KSyncFlowIndexManager(this)),
      qos_queue_ksync_obj_(new QosQueueKSyncObject(this)),
      forwarding_class_ksync_obj_(new ForwardingClassKSyncObject(this)),
      qos_config_ksync_obj_(new QosConfigKSyncObject(this)) {
      for (uint16_t i = 0; i < agent->flow_thread_count(); i++) {
          FlowTableKSyncObject *obj = new FlowTableKSyncObject(this);
          flow_table_ksync_obj_list_.push_back(obj);
      }
}

KSync::~KSync() {
    STLDeleteValues(&flow_table_ksync_obj_list_);
}

void KSync::RegisterDBClients(DB *db) {
    KSyncObjectManager::Init();
    interface_ksync_obj_.get()->RegisterDBClients();
    vrf_ksync_obj_.get()->RegisterDBClients();
    nh_ksync_obj_.get()->RegisterDBClients();
    mpls_ksync_obj_.get()->RegisterDBClients();
    mirror_ksync_obj_.get()->RegisterDBClients();
    vrf_assign_ksync_obj_.get()->RegisterDBClients();
    vxlan_ksync_obj_.get()->RegisterDBClients();
    qos_queue_ksync_obj_.get()->RegisterDBClients();
    forwarding_class_ksync_obj_.get()->RegisterDBClients();
    qos_config_ksync_obj_.get()->RegisterDBClients();
    agent_->set_router_id_configured(false);
}

void KSync::Init(bool create_vhost) {
    NetlinkInit();
    VRouterInterfaceSnapshot();
    InitFlowMem();
    ResetVRouter(true);
    if (create_vhost) {
        CreateVhostIntf();
    }
    interface_ksync_obj_.get()->Init();
    for (uint16_t i = 0; i < flow_table_ksync_obj_list_.size(); i++) {
        FlowTable *flow_table = agent_->pkt()->get_flow_proto()->GetTable(i);
        flow_table->set_ksync_object(flow_table_ksync_obj_list_[i]);
        flow_table_ksync_obj_list_[i]->Init();
    }
    ksync_flow_memory_.get()->Init();
}

void KSync::InitDone() {
    for (uint16_t i = 0; i < flow_table_ksync_obj_list_.size(); i++) {
        FlowTable *flow_table = agent_->pkt()->get_flow_proto()->GetTable(i);
        flow_table_ksync_obj_list_[i]->set_flow_table(flow_table);
        flow_table->set_ksync_object(flow_table_ksync_obj_list_[i]);
    }
    uint32_t count = ksync_flow_memory_->flow_table_entries_count();
    ksync_flow_index_manager_->InitDone(count);
    AgentProfile *profile = agent_->oper_db()->agent_profile();
    profile->RegisterKSyncStatsCb(boost::bind(&KSync::SetProfileData,
                                              this, _1));
    KSyncSock::Get(0)->SetMeasureQueueDelay(agent_->MeasureQueueDelay());
}

void KSync::InitFlowMem() {
    ksync_flow_memory_.get()->InitFlowMem();
}

void KSync::NetlinkInit() {
    EventManager *event_mgr;

    event_mgr = agent_->event_manager();
    boost::asio::io_service &io = *event_mgr->io_service();

    KSyncSockNetlink::Init(io, NETLINK_GENERIC);
    KSyncSock::SetAgentSandeshContext
        (new KSyncSandeshContext(ksync_flow_memory_.get()));
    GenericNetlinkInit();
}

int KSync::Encode(Sandesh &encoder, uint8_t *buf, int buf_len) {
    int len, error;
    len = encoder.WriteBinary(buf, buf_len, &error);
    return len;
}

void KSync::SetProfileData(ProfileData *data) {
    KSyncSock *sock = KSyncSock::Get(0);
    const KSyncTxQueue *tx_queue = sock->send_queue();

    ProfileData::WorkQueueStats *stats = &data->ksync_tx_queue_count_;
    stats->name_ = "KSync Send Queue";
    stats->queue_count_ = tx_queue->queue_len();
    stats->enqueue_count_ = tx_queue->enqueues();
    stats->dequeue_count_ = tx_queue->dequeues();
    stats->max_queue_count_ = tx_queue->max_queue_len();
    stats->start_count_ = tx_queue->read_events();
    stats->busy_time_ = tx_queue->busy_time();
    tx_queue->set_measure_busy_time(agent()->MeasureQueueDelay());
    if (agent()->MeasureQueueDelay()) {
        tx_queue->ClearStats();
    }

    const KSyncSock::KSyncReceiveQueue *rx_queue =
        sock->get_receive_work_queue(0);
    stats = &data->ksync_rx_queue_count_;
    stats->name_ = rx_queue->Description();
    stats->queue_count_ = rx_queue->Length();
    stats->enqueue_count_ = rx_queue->NumEnqueues();
    stats->dequeue_count_ = rx_queue->NumDequeues();
    stats->max_queue_count_ = rx_queue->max_queue_len();
    stats->start_count_ = rx_queue->task_starts();
    stats->busy_time_ = rx_queue->busy_time();
    rx_queue->set_measure_busy_time(agent()->MeasureQueueDelay());
    if (agent()->MeasureQueueDelay()) {
        rx_queue->ClearStats();
    }
}

void KSync::VRouterInterfaceSnapshot() {
    interface_scanner_.get()->Init();

    int len = 0;
    KSyncSandeshContext *ctxt = static_cast<KSyncSandeshContext *>
                                (KSyncSock::GetAgentSandeshContext());
    ctxt->Reset();
    KSyncSock *sock = KSyncSock::Get(0);
    do {
        vr_interface_req req;
        req.set_h_op(sandesh_op::DUMP);
        req.set_vifr_idx(0);
        req.set_vifr_marker(ctxt->context_marker());
        uint8_t msg[KSYNC_DEFAULT_MSG_SIZE];
        len = Encode(req, msg, KSYNC_DEFAULT_MSG_SIZE);
        sock->BlockingSend((char *)msg, len);
        if (sock->BlockingRecv()) {
            LOG(ERROR, "Error getting interface dump from VROUTER");
            return;
        }
    } while (ctxt->response_code() & VR_MESSAGE_DUMP_INCOMPLETE);
    ctxt->Reset();
}

void KSync::ResetVRouter(bool run_sync_mode) {
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

    //Get configured mpls, vmi, vni and nexthop parameters
    //from vrouter
    encoder.set_h_op(sandesh_op::GET);
    len = Encode(encoder, msg, KSYNC_DEFAULT_MSG_SIZE);
    sock->BlockingSend((char *)msg, len);
    if (sock->BlockingRecv()) {
        LOG(ERROR, "Error getting configured parameter for vrouter");
    }

    KSyncSock::Start(run_sync_mode);
}

void KSync::VnswInterfaceListenerInit() {
    vnsw_interface_listner_->Init();
}

void KSync::CreateVhostIntf() {
#if defined(__linux__)
    struct  nl_client *cl;

    assert((cl = nl_register_client()) != NULL);
    assert(nl_socket(cl, AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE) > 0);
    assert(nl_connect(cl, 0, 0) == 0);

    struct vn_if ifm;
    struct nl_response *resp;

    memset(&ifm, 0, sizeof(ifm));
    strncpy(ifm.if_name, agent_->vhost_interface_name().c_str(),
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
#elif defined(__FreeBSD__)
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_flags = IFF_UP;

    int s = socket(PF_LOCAL, SOCK_DGRAM, 0);
    assert(s > 0);

    strncpy(ifr.ifr_name, agent_->vhost_interface_name().c_str(),
        sizeof(ifr.ifr_name));

    assert(ioctl(s, SIOCSIFFLAGS, &ifr) != -1);
    close(s);
#endif
}

void KSync::UpdateVhostMac() {
#if defined(__linux__)
    struct  nl_client *cl;

    assert((cl = nl_register_client()) != NULL);
    assert(nl_socket(cl,AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE) > 0);
    assert(nl_connect(cl, 0, 0) == 0);

    struct vn_if ifm;
    struct nl_response *resp;

    memset(&ifm, 0, sizeof(ifm));
    strncpy(ifm.if_name, agent_->vhost_interface_name().c_str(),
            IFNAMSIZ);
    ifm.if_name[IFNAMSIZ - 1] = '\0';
    strcpy(ifm.if_kind, VHOST_KIND);
    ifm.if_flags = IFF_UP;

    PhysicalInterfaceKey key(agent_->fabric_interface_name());
    Interface *eth = static_cast<Interface *>
        (agent_->interface_table()->FindActiveEntry(&key));
    eth->mac().ToArray((u_int8_t *)ifm.if_mac, sizeof(ifm.if_mac));
    assert(nl_build_if_create_msg(cl, &ifm, 1) == 0);
    assert(nl_sendmsg(cl) > 0);
    assert(nl_recvmsg(cl) > 0);
    assert((resp = nl_parse_reply(cl)) != NULL);
    assert(resp->nl_type == NL_MSG_TYPE_ERROR);
    nl_free_client(cl);
#elif defined(__FreeBSD__)
    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));

    int s = socket(PF_LOCAL, SOCK_DGRAM, 0);
    assert(s >= 0);

    strncpy(ifr.ifr_name, agent_->vhost_interface_name().c_str(),
            sizeof(ifr.ifr_name));

    PhysicalInterfaceKey key(agent_->fabric_interface_name());
    Interface *eth = static_cast<Interface *>
        (agent_->interface_table()->FindActiveEntry(&key));
    ifr.ifr_addr = eth->mac();

    ifr.ifr_addr.sa_len = eth->mac().size();

    assert(ioctl(s, SIOCSIFLLADDR, &ifr) != -1);

    close(s);
#endif
}

void KSync::Shutdown() {
    vnsw_interface_listner_->Shutdown();
    vnsw_interface_listner_.reset(NULL);
    interface_ksync_obj_.reset(NULL);
    vrf_ksync_obj_.get()->Shutdown();
    vrf_ksync_obj_.reset(NULL);
    nh_ksync_obj_.reset(NULL);
    mpls_ksync_obj_.reset(NULL);
    ksync_flow_memory_.reset(NULL);
    mirror_ksync_obj_.reset(NULL);
    vrf_assign_ksync_obj_.reset(NULL);
    vxlan_ksync_obj_.reset(NULL);
    qos_queue_ksync_obj_.reset(NULL);
    forwarding_class_ksync_obj_.reset(NULL);
    qos_config_ksync_obj_.reset(NULL);
    STLDeleteValues(&flow_table_ksync_obj_list_);
    KSyncSock::Shutdown();
    KSyncObjectManager::Shutdown();
}

void GenericNetlinkInit() {
    struct nl_client    *cl;
    int    family;

    assert((cl = nl_register_client()) != NULL);
    assert(nl_socket(cl, AF_NETLINK, SOCK_DGRAM, NETLINK_GENERIC) >= 0);
    assert(nl_connect(cl, 0, 0) == 0);

    family = vrouter_get_family_id(cl);
    LOG(DEBUG, "Vrouter family is " << family);
    KSyncSock::SetNetlinkFamilyId(family);
    nl_free_client(cl);
    return;
}

KSyncTcp::KSyncTcp(Agent *agent): KSync(agent) {
}

void KSyncTcp::InitFlowMem() {
    ksync_flow_memory_.get()->MapSharedMemory();
}

void KSyncTcp::TcpInit() {
    EventManager *event_mgr;
    event_mgr = agent_->event_manager();
    boost::system::error_code ec;
    boost::asio::ip::address ip;
    ip = agent_->vrouter_server_ip();
    uint32_t port = agent_->vrouter_server_port();
    KSyncSockTcp::Init(event_mgr, ip, port);
    KSyncSock::SetNetlinkFamilyId(24);

    KSyncSock::SetAgentSandeshContext
        (new KSyncSandeshContext(ksync_flow_memory_.get()));
    KSyncSockTcp *sock = static_cast<KSyncSockTcp *>(KSyncSock::Get(0));
    while (sock->connect_complete() == false) {
        sleep(1);
    }
}

KSyncTcp::~KSyncTcp() { }

void KSyncTcp::Init(bool create_vhost) {
    TcpInit();
    VRouterInterfaceSnapshot();
    InitFlowMem();
    ResetVRouter(false);
    //Start async read of socket
    KSyncSockTcp *sock = static_cast<KSyncSockTcp *>(KSyncSock::Get(0));
    sock->AsyncReadStart();
    interface_ksync_obj_.get()->Init();
    for (uint16_t i = 0; i < flow_table_ksync_obj_list_.size(); i++) {
        flow_table_ksync_obj_list_[i]->Init();
    }
    ksync_flow_memory_.get()->Init();
}
