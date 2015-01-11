/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#include "bgp/bgp_xmpp_sandesh.h"

#include <sandesh/sandesh.h>

#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_types.h"
#include "bgp/bgp_peer_membership.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_xmpp_channel.h"
#include "xmpp/xmpp_connection.h"
#include "xmpp/xmpp_server.h"

using namespace std;

static void ShowXmppNeighborVisitor(
    vector<BgpNeighborResp> *nbr_list, BgpServer *bgp_server,
    const string &neighbor, BgpXmppChannel *bx_channel) {
    if (!neighbor.empty() && bx_channel->ToString() != neighbor &&
        bx_channel->remote_endpoint().address().to_string() != neighbor) {
        return;
    }
    BgpNeighborResp resp;
    resp.set_peer(bx_channel->ToString());
    resp.set_peer_address(bx_channel->remote_endpoint().address().to_string());
    resp.set_deleted(bx_channel->peer_deleted());
    resp.set_local_address(bx_channel->local_endpoint().address().to_string());
    resp.set_peer_type("internal");
    resp.set_encoding("XMPP");
    resp.set_state(bx_channel->StateName());

    const XmppConnection *connection = bx_channel->channel()->connection();
    resp.set_configured_hold_time(connection->GetConfiguredHoldTime());
    resp.set_negotiated_hold_time(connection->GetNegotiatedHoldTime());

    PeerRibMembershipManager *mgr =
        bx_channel->Peer()->server()->membership_mgr();
    mgr->FillPeerMembershipInfo(bx_channel->Peer(), &resp);
    bx_channel->FillTableMembershipInfo(&resp);
    bx_channel->FillInstanceMembershipInfo(&resp);

    BgpPeer::FillBgpNeighborDebugState(resp, bx_channel->Peer()->peer_stats());
    nbr_list->push_back(resp);
}

static void ShowXmppNeighbor(
    vector<BgpNeighborResp> *list, BgpSandeshContext *bsc,
    const BgpNeighborReq *req) {
    bsc->xmpp_peer_manager->VisitChannels(
        boost::bind(ShowXmppNeighborVisitor,
                    list, bsc->bgp_server, req->get_neighbor(), _1));
}

static void ShowXmppNeighborSummaryVisitor(
    vector<BgpNeighborResp> *nbr_list, BgpXmppChannel *bx_channel) {
    BgpNeighborResp resp;
    resp.set_peer(bx_channel->ToString());
    resp.set_deleted(bx_channel->peer_deleted());
    resp.set_peer_address(bx_channel->remote_endpoint().address().to_string());
    resp.set_peer_type("internal");
    resp.set_encoding("XMPP");
    resp.set_state(bx_channel->StateName());
    resp.set_local_address(bx_channel->local_endpoint().address().to_string());
    const XmppConnection *connection = bx_channel->channel()->connection();
    resp.set_negotiated_hold_time(connection->GetNegotiatedHoldTime());
    nbr_list->push_back(resp);
}

static void ShowXmppNeighborSummary(
    vector<BgpNeighborResp> *list, BgpSandeshContext *bsc,
    const BgpNeighborReq *req) {
    bsc->xmpp_peer_manager->VisitChannels(
        boost::bind(ShowXmppNeighborSummaryVisitor, list, _1));
}

static void ShowXmppNeighborStatisticsVisitor(
    size_t *count, BgpServer *bgp_server, string domain,
    string up_or_down, BgpXmppChannel *channel) {

    if (boost::iequals(up_or_down, "up") && !channel->Peer()->IsReady()) {
        return;
    }
    if (boost::iequals(up_or_down, "down") && channel->Peer()->IsReady()) {
        return;
    }

    if (!domain.empty()) {
        RoutingInstanceMgr *rim = bgp_server->routing_instance_mgr();
        RoutingInstance *ri = rim->GetRoutingInstance(domain);
        if (!ri) return;
        BgpTable *table = ri->GetTable(Address::INET);
        if (!table) return;

        if (!bgp_server->membership_mgr()->IPeerRibFind(channel->Peer(),
                                                        table)) {
            return;
        }
    }

    ++*count;
}

static void ShowXmppNeighborStatistics(
    size_t *count, BgpSandeshContext *bsc,
    const ShowNeighborStatisticsReq *req) {
    bsc->xmpp_peer_manager->VisitChannels(
        boost::bind(ShowXmppNeighborStatisticsVisitor, count,
                    bsc->bgp_server, req->get_domain(),
                    req->get_up_or_down(), _1));

}

void RegisterSandeshShowXmppExtensions(BgpSandeshContext *ctx) {
    ctx->SetNeighborShowExtensions(
        &ShowXmppNeighbor,
        &ShowXmppNeighborSummary,
        &ShowXmppNeighborStatistics);
}
