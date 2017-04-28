/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <base/string_util.h>
#include <cmn/agent.h>
#include <ksync/ksync_entry.h>
#include <ksync/ksync_object.h>
#include <ksync/ksync_netlink.h>
#include <vrouter/ksync/ksync_init.h>
#include <vrouter/ksync/bridge_route_ksync.h>

BridgeRouteKSyncEntry::BridgeRouteKSyncEntry(BridgeRouteKSyncObject *obj,
                                             uint32_t vrf_id,
                                             const MacAddress &mac)
    : ksync_obj_(obj), vrf_id_(vrf_id), mac_(mac) {
}

BridgeRouteKSyncEntry::BridgeRouteKSyncEntry(BridgeRouteKSyncObject* obj,
                                             const BridgeRouteKSyncEntry *entry)
    : ksync_obj_(obj), vrf_id_(entry->vrf_id_), mac_(entry->mac_) {
}

BridgeRouteKSyncEntry::~BridgeRouteKSyncEntry() {
}

bool BridgeRouteKSyncEntry::Sync() {
    return false;
}

KSyncEntry *BridgeRouteKSyncEntry::UnresolvedReference() {
    return NULL;
}

std::string BridgeRouteKSyncEntry::ToString() const {
    std::stringstream s;

    const VrfEntry* vrf =
        ksync_obj_->ksync()->agent()->vrf_table()->FindVrfFromId(vrf_id_);
    string vrf_info = vrf? vrf->GetName() : integerToString(vrf_id_);
    s << "Route Vrf : " << vrf_info << " Mac: " << mac_.ToString();
    return s.str();
}

bool BridgeRouteKSyncEntry::IsLess(const KSyncEntry &rhs) const {
    const BridgeRouteKSyncEntry &entry = static_cast
        <const BridgeRouteKSyncEntry &>(rhs);

    if (vrf_id_ != entry.vrf_id_) {
        return vrf_id_ < entry.vrf_id_;
    }

    return mac_ < entry.mac_;
}

KSyncObject *BridgeRouteKSyncEntry::GetObject() const {
    return ksync_obj_;
}

void BridgeRouteKSyncEntry::FillObjectLog(sandesh_op::type type,
                                          KSyncRouteInfo &info) const {
    info.set_operation("DELETE");
    info.set_plen(0);
    info.set_vrf(vrf_id_);
    info.set_mac(mac_.ToString());
    info.set_type("BRIDGE");
}

int BridgeRouteKSyncEntry::AddMsg(char *buf, int buf_len) {
    /* No message has to be sent to vrouter. Add is taken care in
     * RouteKSyncEntry */
    return 0;
}

int BridgeRouteKSyncEntry::ChangeMsg(char *buf, int buf_len){
    /* No message has to be sent to vrouter. Change is taken care in
     * RouteKSyncEntry */
    return 0;
}

int BridgeRouteKSyncEntry::EncodeDelete(char *buf, int buf_len) {
    vr_route_req encoder;
    int encode_len;

    encoder.set_h_op(sandesh_op::DELETE);
    encoder.set_rtr_rid(0);
    encoder.set_rtr_vrf_id(vrf_id_);
    encoder.set_rtr_family(AF_BRIDGE);

    std::vector<int8_t> mac((int8_t *)mac_,
                            (int8_t *)mac_ + mac_.size());
    encoder.set_rtr_mac(mac);
    encoder.set_rtr_replace_plen(0);

    int error = 0;
    encode_len = encoder.WriteBinary((uint8_t *)buf, buf_len, &error);
    assert(error == 0);
    assert(encode_len <= buf_len);
    return encode_len;
}

int BridgeRouteKSyncEntry::DeleteMsg(char *buf, int buf_len) {
    KSyncRouteInfo info;
    FillObjectLog(sandesh_op::DELETE, info);
    KSYNC_TRACE(Route, GetObject(), info);

    return EncodeDelete(buf, buf_len);
}

///////////////////////////////////////////////////////////////////////////////
//                 BridgeRouteKSyncObject routines
///////////////////////////////////////////////////////////////////////////////

BridgeRouteKSyncObject::BridgeRouteKSyncObject(KSync *ksync) :
    KSyncObject("KSync BridgeRouteTable"), ksync_(ksync) {
}

BridgeRouteKSyncObject::~BridgeRouteKSyncObject() {
}

KSyncEntry *BridgeRouteKSyncObject::Alloc(const KSyncEntry *key, uint32_t idx) {
    const BridgeRouteKSyncEntry *route =
        static_cast<const BridgeRouteKSyncEntry *>(key);
    BridgeRouteKSyncEntry *ksync = new BridgeRouteKSyncEntry(this, route);
    return static_cast<KSyncEntry *>(ksync);
}
