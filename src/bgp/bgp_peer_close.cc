/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/task_annotations.h"
#include "net/bgp_af.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_log.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_close.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/peer_close_manager.h"

using boost::assign::list_of;
using std::back_inserter;
using std::includes;
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
    return "bgp::Config";
}

int BgpPeerClose::GetTaskInstance() const {
    return 0;
}

void BgpPeerClose::MembershipRequestCallbackComplete() {
    CHECK_CONCURRENCY(GetTaskName());
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

void BgpPeerClose::UpdateRouteStats(Address::Family family,
                                    const BgpPath *old_path,
                                    uint32_t path_flags) const {
    GetManager()->UpdateRouteStats(family, old_path, path_flags);
}

void BgpPeerClose::Close(bool graceful) {
    // Abort GR-Closure if this request is for non-graceful closure.
    // Reset GR-Closure if previous closure is still in progress or if
    // this is a flip (from established state).
    if (!graceful || flap_count_ != peer_->total_flap_count()) {
        if (flap_count_ != peer_->total_flap_count()) {
            flap_count_++;
            assert(peer_->total_flap_count() == flap_count_);
        }
        GetManager()->Close(graceful);
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
    // Check if GR Helper mode is disabled.
    if (!IsGRHelperModeEnabled()) {
        BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_IN, "GR Helper mode is not enabled because it is not"
            " configured");
        return false;
    }

    // Check if GR is supported by the peer.
    if (gr_params_.families.empty()) {
        BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_IN, "GR Helper mode is not enabled because received "
            "GR address families list is empty");
        return false;
    }

    // LLGR should be in effect to enable GR Helper mode with 0 restart time.
    if (!gr_params_.time && !IsCloseLongLivedGracefulInternal()) {
        BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_IN, "GR Helper mode is not enabled because received "
            "GR restart time value is 0 seconds and (there is no applicable "
            "LLGR as well");
        return false;
    }

    // Abort GR if currently negotiated families differ from already
    // staled address families.
    if (!negotiated_families().empty() &&
            PeerNegotiatedFamilies() != negotiated_families()) {
        BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_IN, "GR Helper mode is aborted because received "
            "GR families list differs from the list received last time");
        return false;
    }

    // If GR is not supported for any of the negotiated address family,
    // then consider GR as not supported
    if (PeerNegotiatedFamilies() != gr_families_) {
        BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_IN, "GR Helper mode is not enabled because GR address "
            "families differs from negotiated address families");
        return false;
    }

    // Make sure that forwarding state is preserved for all families in
    // the restarting speaker. (Except for EVPN and ERMVPN)
    BOOST_FOREACH(BgpProto::OpenMessage::Capability::GR::Family family,
                  gr_params_.families) {
        // Check if forwarding state was preserved during restart.
        if (family.forwarding_state_preserved())
            continue;

        // Ignore forwarding-state preservation check for certain families.
        Address::Family addr_family =
            BgpAf::AfiSafiToFamily(family.afi, family.safi);
        if (addr_family == Address::EVPN || addr_family == Address::ERMVPN)
            continue;

        string family_str = Address::FamilyToString(addr_family);
        BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG,
            BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN, "GR Helper mode is not  "
            "enabled because after restart, GR forwarding state is not "
            "preserved for address family " << family_str);
        return false;
    }
    return true;
}

// If the peer is deleted or administratively held down, do not attempt
// graceful restart
bool BgpPeerClose::IsCloseGraceful() const {
    if (IsPeerDeleted()) {
        BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_IN, "GR Helper mode is not enabled because BgpPeer has"
            " been deleted");
        return false;
    }

    if (IsPeerAdminDown()) {
        BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_IN, "GR Helper mode is not enabled because BgpPeer has"
            " been held administratively down");
        return false;
    }

    if (IsServerDeleted()) {
        BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_IN, "GR Helper mode is not enabled because BgpServer "
            "has been deletd");
        return false;
    }

    if (IsServerAdminDown()) {
        BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_IN, "GR Helper mode is not enabled because BgpServer "
            "has been held administratively down");
        return false;
    }

    if (!IsGRReady())
        return false;
    return true;
}

// LLGR families should be identical to GR families.
bool BgpPeerClose::IsLlgrSupportedForFamilies() const {
    // Keep a sorted list of unsupported families.
    static vector<string> unsupported_families = list_of
        (Address::FamilyToString(Address::EVPN))
        (Address::FamilyToString(Address::ERMVPN));

    if (gr_families_ == llgr_families_)
        return true;

    // Ignore families mis-match for certain families.
    vector<string> differing_families;
    std::set_symmetric_difference(gr_families_.begin(), gr_families_.end(),
                                  llgr_families_.begin(), llgr_families_.end(),
                                  back_inserter(differing_families));

    // Ignore if differing families are only those which are unsupported.
    return includes(unsupported_families.begin(), unsupported_families.end(),
                    differing_families.begin(), differing_families.end());
}

// Check if we need to trigger Long Lived Graceful Restart. In addition to
// normal GR checks, we also need to check LLGR capability was negotiated
// and non-zero restart time was inferred.
bool BgpPeerClose::IsCloseLongLivedGraceful() const {
    if (!IsCloseGraceful())
        return false;
    return BgpPeerClose::IsCloseLongLivedGracefulInternal();
}

bool BgpPeerClose::IsCloseLongLivedGracefulInternal() const {
    if (llgr_params_.families.empty()) {
        BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_IN,
            "No LLGR support due to empty LLGR address families list");
        return false;
    }
    if (!llgr_params_.time) {
        BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_IN, "No LLGR support due to zero time value");
        return false;
    }

    if (!IsLlgrSupportedForFamilies()) {
        BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG, BGP_LOG_FLAG_ALL,
            BGP_PEER_DIR_IN, "No LLGR support due to dissimilar "
            "GR address families and LLGR address families");
        return false;
    }

    // Make sure that forwarding state is preserved for all families in
    // the restarting speaker.
    BOOST_FOREACH(BgpProto::OpenMessage::Capability::LLGR::Family family,
                  llgr_params_.families) {
        // Ignore forwarding-state preservation check for certain families.
        Address::Family addr_family =
            BgpAf::AfiSafiToFamily(family.afi, family.safi);
        if (addr_family == Address::EVPN || addr_family == Address::ERMVPN)
            continue;

        if (!family.forwarding_state_preserved()) {
            string family_str = Address::FamilyToString(addr_family);
            BGP_LOG_PEER(Message, peer_, SandeshLevel::SYS_DEBUG,
                BGP_LOG_FLAG_ALL, BGP_PEER_DIR_IN, "GR Helper mode is not "
                "enabled because after restart, LLGR forwarding state is not "
                "preserved for address family " << family_str);
            return false;
        }
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

    // Indicate to the Peer if we are restarting (equiv when the session is
    // being formed for the first time.
    bool restarted = peer_->flap_count() == 0;

    // Indicate EOR support by default.
    if (!peer_->server()->global_config()->gr_enable()) {
        BgpProto::OpenMessage::Capability *gr_cap =
            BgpProto::OpenMessage::Capability::GR::Encode(0, restarted, false,
                                                          afi_flags,
                                                          gr_families);
        opt_param->capabilities.push_back(gr_cap);
        return;
    }

    BOOST_FOREACH(Address::Family family, peer_->supported_families()) {
        if (!peer_->LookupFamily(family))
            continue;
        gr_families.push_back(family);
        afi_flags.push_back(BgpProto::OpenMessage::Capability::GR::
                                ForwardingStatePreservedFlag);
    }

    uint16_t time = peer_->server()->GetGracefulRestartTime();
    BgpProto::OpenMessage::Capability *gr_cap =
        BgpProto::OpenMessage::Capability::GR::Encode(time, restarted, true,
                                                      afi_flags, gr_families);
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
    if (!peer_->server()->global_config()->gr_enable() ||
            !peer_->server()->GetLongLivedGracefulRestartTime()) {
        return;
    }

    vector<Address::Family> llgr_families;
    BOOST_FOREACH(Address::Family family, peer_->supported_families()) {
        if (peer_->LookupFamily(family))
            llgr_families.push_back(family);
    }

    uint32_t time = peer_->server()->GetLongLivedGracefulRestartTime();
    uint8_t afi_flags =
        BgpProto::OpenMessage::Capability::LLGR::ForwardingStatePreservedFlag;
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
