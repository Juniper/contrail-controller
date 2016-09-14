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

PeerCloseManager *BgpPeerClose::GetManager() const {
    return peer_->close_manager();
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

bool BgpPeerClose::IsGRHelperModeEnabled() const {
    return peer_->server()->IsGRHelperModeEnabled();
}

const std::vector<std::string> &BgpPeerClose::PeerNegotiatedFamilies() const {
    return peer_->negotiated_families();
}

bool BgpPeerClose::IsPeerDeleted() const {
    return peer_->IsDeleted();
}

bool BgpPeerClose::IsPeerAdminDown() const {
    return peer_->IsAdminDown();
}

bool BgpPeerClose::IsServerDeleted() const {
    return peer_->server()->IsDeleted();
}

bool BgpPeerClose::IsServerAdminDown() const {
    return peer_->server()->admin_down();
}

bool BgpPeerClose::IsInGRTimerWaitState() const {
    return GetManager()->IsInGRTimerWaitState();
}

bool BgpPeerClose::IsInLlgrTimerWaitState() const {
    return GetManager()->IsInLlgrTimerWaitState();
}

const std::vector<std::string> &BgpPeerClose::negotiated_families() const {
    return negotiated_families_;
}

const std::vector<BgpProto::OpenMessage::Capability *> &
BgpPeerClose::capabilities() const {
    return peer_->capabilities();
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
        GetManager()->Close(non_graceful);
        return;
    }

    // Ignore if close is already in progress.
    if (GetManager()->IsCloseInProgress() && !IsInGRTimerWaitState())
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

bool BgpPeerClose::IsGRReady() const {
    // Check if GR helper mode is disabled.
    if (!IsGRHelperModeEnabled())
        return false;

    // Check if GR is supported by the peer.
    if (gr_params_.families.empty())
        return false;

    // Restart time must be non-zero in order to enable GR helper mode.
    if (!gr_params_.time)
        return false;

    // Abort GR if currently negotiated families differ from already
    // staled address families.
    if (!negotiated_families().empty() &&
            PeerNegotiatedFamilies() != negotiated_families())
        return false;

    // If GR is not supported for any of the negotiated address family,
    // then consider GR as not supported
    if (PeerNegotiatedFamilies() != gr_families_)
        return false;

    // Make sure that forwarding state is preserved for all families in
    // the restarting speaker.
    BOOST_FOREACH(BgpProto::OpenMessage::Capability::GR::Family family,
                  gr_params_.families) {
        if (!family.forwarding_state_preserved())
            return false;
    }
    return true;
}

bool BgpPeerClose::IsDisabled() const {
    if (IsPeerDeleted() || IsPeerAdminDown())
        return true;
    if (IsServerDeleted() || IsServerAdminDown())
        return true;
    return false;
}

// If the peer is deleted or administratively held down, do not attempt
// graceful restart
bool BgpPeerClose::IsCloseGraceful() const {
    if (IsDisabled())
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
    if (!llgr_params_.time)
        return false;

    // LLGR families should be identical to GR families.
    if (gr_families_ != llgr_families_)
        return false;

    // Make sure that forwarding state is preserved for all families in
    // the restarting speaker.
    BOOST_FOREACH(BgpProto::OpenMessage::Capability::LLGR::Family family,
                  llgr_params_.families) {
        if (!family.forwarding_state_preserved())
            return false;
    }
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
    vector<uint8_t> afi_flags;
    uint16_t time = peer_->server()->GetGracefulRestartTime();

    // Indicate EOR support by default.
    if (!time) {
        BgpProto::OpenMessage::Capability *gr_cap =
            BgpProto::OpenMessage::Capability::GR::Encode(0, 0, afi_flags,
                                                          gr_families);
        opt_param->capabilities.push_back(gr_cap);
        return;
    }

    BOOST_FOREACH(Address::Family family, peer_->supported_families()) {
        if (!peer_->LookupFamily(family))
            continue;
        gr_families.push_back(family);
        afi_flags.push_back(
            BgpProto::OpenMessage::Capability::GR::ForwardingStatePreserved);
    }

    uint8_t flags = 0;
    BgpProto::OpenMessage::Capability *gr_cap =
        BgpProto::OpenMessage::Capability::GR::Encode(time, flags, afi_flags,
                                                      gr_families);
    opt_param->capabilities.push_back(gr_cap);
}

// Process received GR and LLGR Capabilities. Return true if the values are sane
// to proceed with further processing. Return false if not to abort any ongoing
// GR and instead trigger non-graceful closure.
bool BgpPeerClose::SetGRCapabilities(BgpPeerInfoData *peer_info) {
    BgpProto::OpenMessage::Capability::GR::Decode(&gr_params_, capabilities());
    BgpProto::OpenMessage::Capability::GR::GetFamilies(gr_params_,
                                                       &gr_families_);

    BgpProto::OpenMessage::Capability::LLGR::Decode(&llgr_params_,
                                                    capabilities());
    BgpProto::OpenMessage::Capability::LLGR::GetFamilies(llgr_params_,
                                                         &llgr_families_);

    if (peer_info) {
        peer_info->set_graceful_restart_families(gr_families_);
        peer_info->set_long_lived_graceful_restart_families(llgr_families_);
        peer_info->set_graceful_restart_time(GetGracefulRestartTime());
        peer_info->set_long_lived_graceful_restart_time(
                GetLongLivedGracefulRestartTime());
        BGPPeerInfo::Send(*peer_info);
    }

    // If we are not in GR Timer waiting state, then there is no case to abort
    // GR when new session is coming up.
    if (!IsInGRTimerWaitState())
        return true;

    // If LLGR is no longer supported, terminate GR right away. This can happen
    // due to mis-match between gr and llgr afis. For now, we expect an
    // identical set.
    if (IsInLlgrTimerWaitState())
        return IsCloseLongLivedGraceful();
    return IsCloseGraceful();
}

void BgpPeerClose::AddLLGRCapabilities(
        BgpProto::OpenMessage::OptParam *opt_param) {
    if (!peer_->server()->GetGracefulRestartTime() ||
            !peer_->server()->GetLongLivedGracefulRestartTime())
        return;

    vector<Address::Family> llgr_families;
    BOOST_FOREACH(Address::Family family, peer_->supported_families()) {
        if (peer_->LookupFamily(family))
            llgr_families.push_back(family);
    }

    uint32_t time = peer_->server()->GetLongLivedGracefulRestartTime();
    uint8_t afi_flags =
        BgpProto::OpenMessage::Capability::LLGR::ForwardingStatePreserved;
    BgpProto::OpenMessage::Capability *llgr_cap =
        BgpProto::OpenMessage::Capability::LLGR::Encode(time, afi_flags,
                                                        llgr_families);
    opt_param->capabilities.push_back(llgr_cap);
}

void BgpPeerClose::FillNeighborInfo(BgpNeighborResp *bnr) const {
    bnr->set_graceful_restart_address_families(gr_families_);
    bnr->set_long_lived_graceful_restart_address_families(llgr_families_);
    bnr->set_graceful_restart_time(GetGracefulRestartTime());
    bnr->set_long_lived_graceful_restart_time(
            GetLongLivedGracefulRestartTime());
}
