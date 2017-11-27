/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/foreach.hpp>
#include <boost/scoped_ptr.hpp>

#include "base/task_annotations.h"
#include "bgp/bgp_factory.h"
#include "bgp/bgp_path.h"
#include "bgp/bgp_xmpp_peer_close.h"
#include "bgp/bgp_xmpp_rtarget_manager.h"
#include "bgp/peer_close_manager.h"
#include "bgp/bgp_xmpp_channel.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_server.h"

using std::string;
using std::vector;

BgpXmppPeerClose::BgpXmppPeerClose(BgpXmppChannel *channel) :
        channel_(channel) {
}

BgpXmppPeerClose::~BgpXmppPeerClose() {
}

PeerCloseManager *BgpXmppPeerClose::GetManager() const {
    return channel_->close_manager();
}

void BgpXmppPeerClose::UpdateRouteStats(Address::Family family,
                                        const BgpPath *old_path,
                                        uint32_t path_flags) const {
    GetManager()->UpdateRouteStats(family, old_path, path_flags);
}

bool BgpXmppPeerClose::IsReady() const {
    return channel_->Peer()->IsReady();
}

IPeer *BgpXmppPeerClose::peer() const {
    return channel_->Peer();
}

int BgpXmppPeerClose::GetGracefulRestartTime() const {
    return channel_->manager()->xmpp_server()->GetGracefulRestartTime();
}

int BgpXmppPeerClose::GetLongLivedGracefulRestartTime() const {
    return channel_->manager()->xmpp_server()->
        GetLongLivedGracefulRestartTime();
}

// Mark all current subscription as 'stale'
// Concurrency: Protected with a mutex from peer close manager
void BgpXmppPeerClose::GracefulRestartStale() {
    channel_->StaleCurrentSubscriptions();
}

// Mark all current subscriptions as 'llgr_stale'
// Concurrency: Protected with a mutex from peer close manager
void BgpXmppPeerClose::LongLivedGracefulRestartStale() {
    channel_->LlgrStaleCurrentSubscriptions();
}

// Delete all current subscriptions which are still stale.
// Concurrency: Protected with a mutex from peer close manager
void BgpXmppPeerClose::GracefulRestartSweep() {
    channel_->SweepCurrentSubscriptions();
}

bool BgpXmppPeerClose::IsCloseGraceful() const {
    // Do not close gracefully if connection is already marked for deletion.
    if (channel_->channel()->connection()->IsDeleted())
        return false;
    return channel_->manager()->xmpp_server()->IsPeerCloseGraceful();
}

bool BgpXmppPeerClose::IsCloseLongLivedGraceful() const {
    return IsCloseGraceful() && GetLongLivedGracefulRestartTime() != 0;
}

// EoR from xmpp is afi independent at the moment.
void BgpXmppPeerClose::GetGracefulRestartFamilies(Families *families) const {
    families->insert(Address::UNSPEC);
}

void BgpXmppPeerClose::ReceiveEndOfRIB(Address::Family family) {
    channel_->ReceiveEndOfRIB(family);
}

// Process any pending subscriptions as close manager is now no longer
// using membership manager.
void BgpXmppPeerClose::MembershipRequestCallbackComplete() {
    CHECK_CONCURRENCY(GetTaskName());
    if (channel_) {
        assert(!GetManager()->IsMembershipInUse());
        channel_->ProcessPendingSubscriptions();
    }
}

const char *BgpXmppPeerClose::GetTaskName() const {
    return "bgp::Config";
}

int BgpXmppPeerClose::GetTaskInstance() const {
    return 0;
}

void BgpXmppPeerClose::CustomClose() {
    channel_->rtarget_manager()->Close();
    channel_->ClearSubscriptions();
}

void BgpXmppPeerClose::CloseComplete() {
    assert(!channel_->channel()->connection()->IsDeleted());
    channel_->set_peer_closed(false);
    channel_->channel()->UnRegisterReceive(xmps::BGP);
}

void BgpXmppPeerClose::Delete() {
    channel_->set_delete_in_progress(true);
    channel_->set_peer_closed(true);
    channel_->manager()->increment_deleting_count();
    channel_->manager()->Enqueue(channel_);
    channel_ = NULL;
}

void BgpXmppPeerClose::Close(bool graceful) {
    if (channel_) {
        assert(channel_->peer_deleted());
        if (!IsCloseGraceful())
            graceful = false;
        GetManager()->Close(graceful);
    }
}
