/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_init_h
#define vnsw_agent_ksync_init_h

#include <vrouter/ksync/flowtable_ksync.h>
#include <vrouter/ksync/mpls_ksync.h>
#include <vrouter/ksync/nexthop_ksync.h>
#include <vrouter/ksync/mirror_ksync.h>
#include <vrouter/ksync/route_ksync.h>
#include <vrouter/ksync/vxlan_ksync.h>
#include <vrouter/ksync/vrf_assign_ksync.h>
#include <vrouter/ksync/ksync_flow_index_manager.h>
#include <oper/agent_profile.h>
#include <vrouter/ksync/qos_queue_ksync.h>
#include <vrouter/ksync/forwarding_class_ksync.h>
#include <vrouter/ksync/qos_config_ksync.h>
#include <vrouter/ksync/ksync_bridge_table.h>
#include "vnswif_listener.h"

class KSyncFlowMemory;
class FlowTableKSyncObject;
class BridgeRouteAuditKSyncObject;

class KSync {
public:
    KSync(Agent *agent);
    virtual ~KSync();

    virtual void Init(bool create_vhost);
    virtual void InitDone();
    virtual void RegisterDBClients(DB *db);
    void VnswInterfaceListenerInit();
    void Shutdown();

    void UpdateVhostMac();
    Agent *agent() const  { return agent_; }
    MirrorKSyncObject *mirror_ksync_obj() const {
        return mirror_ksync_obj_.get();
    }
    NHKSyncObject *nh_ksync_obj() const {
        return nh_ksync_obj_.get();
    }
    InterfaceKSyncObject *interface_ksync_obj() const {
        return interface_ksync_obj_.get();
    }
    VrfKSyncObject *vrf_ksync_obj() const {
        return vrf_ksync_obj_.get();
    }
    FlowTableKSyncObject *flow_table_ksync_obj(uint16_t index) const {
        return flow_table_ksync_obj_list_[index];
    }
    VnswInterfaceListener *vnsw_interface_listner() const  {
        return vnsw_interface_listner_.get();
    }
    KSyncFlowMemory *ksync_flow_memory() const  {
        return ksync_flow_memory_.get();
    }
    KSyncFlowIndexManager *ksync_flow_index_manager() const  {
        return ksync_flow_index_manager_.get();
    }

    void SetProfileData(ProfileData *data);
    QosQueueKSyncObject *qos_queue_ksync_obj() const {
        return qos_queue_ksync_obj_.get();
    }

    ForwardingClassKSyncObject*  forwarding_class_ksync_obj() const {
        return forwarding_class_ksync_obj_.get();
    }

    QosConfigKSyncObject* qos_config_ksync_obj() const {
        return qos_config_ksync_obj_.get();
    }

    BridgeRouteAuditKSyncObject* bridge_route_audit_ksync_obj() const {
        return bridge_route_audit_ksync_obj_.get();
    }

    KSyncBridgeMemory* ksync_bridge_memory() const {
        return ksync_bridge_memory_.get();
    }
protected:
    Agent *agent_;
    boost::scoped_ptr<InterfaceKSyncObject> interface_ksync_obj_;
    std::vector<FlowTableKSyncObject *> flow_table_ksync_obj_list_;
    boost::scoped_ptr<MplsKSyncObject> mpls_ksync_obj_;
    boost::scoped_ptr<NHKSyncObject> nh_ksync_obj_;
    boost::scoped_ptr<MirrorKSyncObject> mirror_ksync_obj_;
    boost::scoped_ptr<VrfKSyncObject> vrf_ksync_obj_;
    boost::scoped_ptr<VxLanKSyncObject> vxlan_ksync_obj_;
    boost::scoped_ptr<VrfAssignKSyncObject> vrf_assign_ksync_obj_;
    boost::scoped_ptr<VnswInterfaceListener> vnsw_interface_listner_;
    boost::scoped_ptr<KSyncFlowMemory> ksync_flow_memory_;
    boost::scoped_ptr<KSyncFlowIndexManager> ksync_flow_index_manager_;
    boost::scoped_ptr<QosQueueKSyncObject> qos_queue_ksync_obj_;
    boost::scoped_ptr<ForwardingClassKSyncObject> forwarding_class_ksync_obj_;
    boost::scoped_ptr<QosConfigKSyncObject> qos_config_ksync_obj_;
    boost::scoped_ptr<BridgeRouteAuditKSyncObject>
        bridge_route_audit_ksync_obj_;
    boost::scoped_ptr<KSyncBridgeMemory> ksync_bridge_memory_;
    virtual void InitFlowMem();
    void SetHugePages();
    void ResetVRouter(bool run_sync_mode);
    int Encode(Sandesh &encoder, uint8_t *buf, int buf_len);
private:
    void InitVrouterOps(vrouter_ops *v);
    void NetlinkInit();
    void CreateVhostIntf();

    static const int kHugePageFiles = 4;
    int huge_fd_[kHugePageFiles];
    void *huge_pages_[kHugePageFiles];

    DISALLOW_COPY_AND_ASSIGN(KSync);
};

class KSyncTcp : public KSync {
public:
    KSyncTcp(Agent *agent);
    virtual ~KSyncTcp();
    virtual void Init(bool create_vhost);
    void TcpInit();
protected:
    virtual void InitFlowMem();
};

class KSyncUds : public KSync {
public:
    KSyncUds(Agent *agent);
    virtual ~KSyncUds();
    virtual void Init(bool create_vhost);
    void UdsInit();
protected:
    virtual void InitFlowMem();
};

int GenenericNetlinkFamily();
void GenericNetlinkInit();

#endif //vnsw_agent_ksync_init_h
