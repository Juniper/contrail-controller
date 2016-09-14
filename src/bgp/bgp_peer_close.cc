/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/task_annotations.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_close.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/peer_close_manager.h"

using std::string;
using std::vector;

BgpPeerClose::BgpPeerClose(BgpPeer *peer)
        : peer_(peer), flap_count_(0) {
}

BgpPeerClose::~BgpPeerClose() {
}

void BgpPeerClose::SetManager(PeerCloseManager *manager) {
    manager_ = manager;
}

string BgpPeerClose::ToString() const {
    return peer_->ToString();
}

void BgpPeerClose::CustomClose() {
    return peer_->CustomClose();
}

void BgpPeerClose::GracefulRestartStale() {
    negotiated_families_ = peer_->negotiated_families();
}

void BgpPeerClose::LongLivedGracefulRestartStale() {
}

void BgpPeerClose::GracefulRestartSweep() {
    negotiated_families_.clear();
}

bool BgpPeerClose::IsReady() const {
    return peer_->IsReady();
}

IPeer *BgpPeerClose::peer() const {
    return peer_;
}

void BgpPeerClose::Close(bool non_graceful) {
    // Abort GR-Closure if this request is for non-graceful closure.
    // Reset GR-Closure if previous closure is still in progress or if
    // this is a flip (from established state).
    if (non_graceful || flap_count_ != peer_->total_flap_count()) {
        if (flap_count_ != peer_->total_flap_count()) {
            flap_count_++;
            assert(peer_->total_flap_count() == flap_count_);
        }
        manager_->Close(non_graceful);
        return;
    }

    // Ignore if close is already in progress.
    if (manager_->IsCloseInProgress() && !manager_->IsInGRTimerWaitState())
        return;

    if (peer_->IsDeleted()) {
        peer_->RetryDelete();
    } else {
        CustomClose();
        CloseComplete();
    }
}

void BgpPeerClose::Delete() {
    gr_params_.Initialize();
    llgr_params_.Initialize();
    gr_families_.clear();
    llgr_families_.clear();
    negotiated_families_.clear();
    if (peer_->IsDeleted()) {
        peer_->RetryDelete();
    } else {
        CloseComplete();
    }
}

// Return the time to wait for, in seconds to exit GR_TIMER state.
int BgpPeerClose::GetGracefulRestartTime() const {
    return gr_params_.time;
}

// Return the time to wait for, in seconds to exit LLGR_TIMER state.
int BgpPeerClose::GetLongLivedGracefulRestartTime() const {
    return llgr_params_.time;
}

void BgpPeerClose::ReceiveEndOfRIB(Address::Family family) {
    peer_->ReceiveEndOfRIB(family, 0);
}

const char *BgpPeerClose::GetTaskName() const {
    return "bgp::StateMachine";
}

int BgpPeerClose::GetTaskInstance() const {
    return peer_->GetTaskInstance();
}

void BgpPeerClose::MembershipRequestCallbackComplete() {
    CHECK_CONCURRENCY("bgp::StateMachine");
}

bool BgpPeerClose::IsGRReady() const {
    // Check if GR helper mode is disabled.
    if (!peer_->server()->IsGRHelperModeEnabled())
        return false;

    // Check if GR is supported by the peer.
    if (gr_params_.families.empty())
        return false;

    // Restart time must be non-zero in order to enable GR helper mode.
    if (!GetGracefulRestartTime())
        return false;

    // Abort GR if currently negotiated families differ from already
    // staled address families.
    if (!negotiated_families_.empty() &&
            peer_->negotiated_families() != negotiated_families_)
        return false;

    // If GR is not supported for any of the negotiated address family,
    // then consider GR as not supported
    if (!equal(peer_->negotiated_families().begin(),
               peer_->negotiated_families().end(), gr_families_.begin())) {
        return false;
    }

    // Make sure that forwarding state is preserved for all families in
    // the restarting speaker.
    BOOST_FOREACH(BgpProto::OpenMessage::Capability::GR::Family family,
                  gr_params_.families) {
        if (!family.forwarding_state_preserved())
            return false;
    }

    // If LLGR is supported, make sure that it is identical to GR afis
    if (!llgr_params_.families.empty()) {
        if (gr_families_ != llgr_families_) {
            return false;
        }
    }
    return true;
}

// If the peer is deleted or administratively held down, do not attempt
// graceful restart
bool BgpPeerClose::IsCloseGraceful() const {
    if (peer_->IsDeleted() || peer_->IsAdminDown())
        return false;
    if (peer_->server()->IsDeleted() || peer_->server()->admin_down())
        return false;
    if (!IsGRReady())
        return false;
    return true;
}

// Check if we need to trigger Long Lived Graceful Restart. In addition to
// normal GR checks, we also need to check LLGR capability was negotiated
// and non-zero restart time was inferred.
bool BgpPeerClose::IsCloseLongLivedGraceful() const {
    if (!IsCloseGraceful())
        return false;
    if (llgr_params_.families.empty())
        return false;
    if (!GetLongLivedGracefulRestartTime())
        return false;
    return true;
}

// Close process for this peer is complete. Restart the state machine and
// attempt to bring up session with the neighbor
void BgpPeerClose::CloseComplete() {
    if (!peer_->IsDeleted() && !peer_->IsAdminDown())
        peer_->state_machine()->Initialize();
}

void BgpPeerClose::GetGracefulRestartFamilies(Families *families) const {
    families->clear();
    BOOST_FOREACH(const string family, gr_families_) {
        families->insert(Address::FamilyFromString(family));
    }
}

void BgpPeerClose::AddGRCapabilities(
        BgpProto::OpenMessage::OptParam *opt_param) {
    vector<Address::Family> gr_families;
    uint16_t time = peer_->server()->GetGracefulRestartTime();

    // Indicate EOR support by default.
    if (!time) {
        BgpProto::OpenMessage::Capability *gr_cap =
            BgpProto::OpenMessage::Capability::GR::Encode(0, 0, 0,
                                                          gr_families);
        opt_param->capabilities.push_back(gr_cap);
        return;
    }

    BOOST_FOREACH(Address::Family family, peer_->supported_families()) {
        if (peer_->LookupFamily(family))
            gr_families.push_back(family);
    }

    uint8_t flags = 0;
    uint8_t afi_flags =
        BgpProto::OpenMessage::Capability::GR::ForwardingStatePreserved;
    BgpProto::OpenMessage::Capability *gr_cap =
        BgpProto::OpenMessage::Capability::GR::Encode(time, flags, afi_flags,
                                                      gr_families);
    opt_param->capabilities.push_back(gr_cap);
}

bool BgpPeerClose::SetGRCapabilities(BgpPeerInfoData *peer_info) {
    BgpProto::OpenMessage::Capability::GR::Decode(&gr_params_,
                                                  peer_->capabilities());
    BgpProto::OpenMessage::Capability::GR::GetFamilies(gr_params_,
                                                       &gr_families_);
    peer_info->set_graceful_restart_families(gr_families_);

    BgpProto::OpenMessage::Capability::LLGR::Decode(&llgr_params_,
                                                    peer_->capabilities());
    BgpProto::OpenMessage::Capability::LLGR::GetFamilies(llgr_params_,
                                                         &llgr_families_);
    peer_info->set_long_lived_graceful_restart_families(llgr_families_);
    peer_info->set_graceful_restart_time(GetGracefulRestartTime());
    peer_info->set_long_lived_graceful_restart_time(
            GetLongLivedGracefulRestartTime());
    BGPPeerInfo::Send(*peer_info);

    // If GR is no longer supported, terminate GR right away. This can happen
    // due to mis-match between gr and llgr afis. For now, we expect an
    // identical set.
    if (manager_->IsInGRTimerWaitState() &&
        (!IsCloseGraceful() || !IsCloseLongLivedGraceful())) {
        return false;
    }
    return true;
}

void BgpPeerClose::FillNeighborInfo(BgpNeighborResp *bnr) const {
    bnr->set_graceful_restart_address_families(gr_families_);
    bnr->set_long_lived_graceful_restart_address_families(llgr_families_);
    bnr->set_graceful_restart_time(GetGracefulRestartTime());
    bnr->set_long_lived_graceful_restart_time(
            GetLongLivedGracefulRestartTime());
}
