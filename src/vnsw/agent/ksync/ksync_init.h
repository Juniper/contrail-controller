/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ksync_init_h
#define vnsw_agent_ksync_init_h

#include <ksync/flowtable_ksync.h>
#include <ksync/mpls_ksync.h>
#include <ksync/nexthop_ksync.h>
#include <ksync/mirror_ksync.h>
#include <ksync/route_ksync.h>
#include <ksync/vxlan_ksync.h>
#include <ksync/vrf_assign_ksync.h>
#include <ksync/interface_snapshot.h>

class KSync {
public:
    KSync(Agent *agent);
    virtual ~KSync();

    void Init();
    void InitTest();
    void RegisterDBClients(DB *db);
    void InitFlowMem();
    void NetlinkInit();
    void VRouterInterfaceSnapshot();
    void ResetVRouter();
    void VnswIfListenerInit();
    void CreateVhostIntf();
    void Shutdown();
    int Encode(Sandesh &encoder, uint8_t *buf, int buf_len);

    void RegisterDBClientsTest(DB *db);
    void NetlinkInitTest();
    void NetlinkShutdownTest();
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
    FlowTableKSyncObject *flowtable_ksync_obj() const {
        return flowtable_ksync_obj_.get();
    }
    InterfaceKSnap *interface_snapshot() const {
        return interface_snapshot_.get();
    }
private:
    Agent *agent_;
    boost::scoped_ptr<InterfaceKSyncObject> interface_ksync_obj_; 
    boost::scoped_ptr<FlowTableKSyncObject> flowtable_ksync_obj_; 
    boost::scoped_ptr<MplsKSyncObject> mpls_ksync_obj_; 
    boost::scoped_ptr<NHKSyncObject> nh_ksync_obj_; 
    boost::scoped_ptr<MirrorKSyncObject> mirror_ksync_obj_; 
    boost::scoped_ptr<VrfKSyncObject> vrf_ksync_obj_;
    boost::scoped_ptr<VxLanKSyncObject> vxlan_ksync_obj_;
    boost::scoped_ptr<VrfAssignKSyncObject> vrf_assign_ksync_obj_;
    boost::scoped_ptr<InterfaceKSnap> interface_snapshot_;
    DISALLOW_COPY_AND_ASSIGN(KSync);
};

int GenenericNetlinkFamily();
void GenericNetlinkInit();
void GenericNetlinkInitTest();

#endif //vnsw_agent_ksync_init_h
