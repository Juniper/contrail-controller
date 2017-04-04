/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */
#include "bgp/bgp_xmpp_sandesh.h"

#include <string>
#include <vector>

#include <boost/regex.hpp>

#include "bgp/bgp_membership.h"
#include "bgp/bgp_peer.h"
#include "bgp/bgp_peer_internal_types.h"
#include "bgp/bgp_sandesh.h"
#include "bgp/bgp_server.h"
#include "bgp/bgp_xmpp_channel.h"
#include "xmpp/xmpp_connection.h"

using boost::regex;
using boost::regex_search;
using std::string;
using std::vector;

static bool ShowXmppNeighborMatch(const BgpXmppChannel *bx_channel,
    const string &search_string, const regex &search_expr) {
    if (regex_search(bx_channel->ToString(), search_expr))
        return true;
    string address_string = bx_channel->remote_endpoint().address().to_string();
    if (regex_search(address_string, search_expr))
        return true;
    if (search_string == "deleted" && bx_channel->peer_deleted())
        return true;
    return false;
}

static void FillXmppNeighborInfo(BgpNeighborResp *bnr,
    const BgpSandeshContext *bsc, const BgpXmppChannel *bx_channel,
    bool summary) {
    bnr->set_peer(bx_channel->ToString());
    bnr->set_peer_address(bx_channel->remote_endpoint().address().to_string());
    bnr->set_transport_address(bx_channel->transport_address_string());
    bnr->set_deleted(bx_channel->peer_deleted());
    bnr->set_closed_at(UTCUsecToString(bx_channel->peer_closed_at()));
    bnr->set_local_address(bx_channel->local_endpoint().address().to_string());
    bnr->set_peer_type("internal");
    bnr->set_encoding("XMPP");
    bnr->set_state(bx_channel->StateName());

    const XmppConnection *connection = bx_channel->channel()->connection();
    bnr->set_negotiated_hold_time(connection->GetNegotiatedHoldTime());
    bnr->set_primary_path_count(bx_channel->Peer()->GetPrimaryPathCount());
    bnr->set_task_instance(connection->GetTaskInstance());
    bnr->set_auth_type(connection->GetXmppAuthenticationType());
    bnr->set_send_ready(bx_channel->Peer()->send_ready());
    bnr->set_flap_count(bx_channel->Peer()->peer_stats()->num_flaps());
    bnr->set_flap_time(bx_channel->Peer()->peer_stats()->last_flap());

    const XmppSession *sess = bx_channel->GetSession();
    if (sess) {
        short int dscp = sess->GetDscpValue();
        bnr->set_dscp_value(dscp);
    }
    if (summary)
        return;

    bnr->set_configured_hold_time(connection->GetConfiguredHoldTime());
    bnr->set_configured_address_families(vector<string>());
    bnr->set_negotiated_address_families(vector<string>());
    const BgpMembershipManager *mgr = bsc->bgp_server->membership_mgr();
    mgr->FillPeerMembershipInfo(bx_channel->Peer(), bnr);
    bx_channel->FillTableMembershipInfo(bnr);
    bx_channel->FillInstanceMembershipInfo(bnr);
    bx_channel->FillCloseInfo(bnr);

    BgpPeer::FillBgpNeighborDebugState(bnr, bx_channel->Peer()->peer_stats());
}

static bool ShowXmppNeighbor(const BgpSandeshContext *bsc, bool summary,
    uint32_t page_limit, uint32_t iter_limit, const string &start_neighbor,
    const string &search_string, vector<BgpNeighborResp> *list,
    string *next_neighbor) {
    regex search_expr(search_string);
    const BgpXmppChannelManager *bxcm = bsc->xmpp_peer_manager;
    BgpXmppChannelManager::const_name_iterator it =
        bxcm->name_clower_bound(start_neighbor);
    for (uint32_t iter_count = 0; it != bxcm->name_cend(); ++it, ++iter_count) {
        const BgpXmppChannel *bx_channel = it->second;
        if (!ShowXmppNeighborMatch(bx_channel, search_string, search_expr))
            continue;
        BgpNeighborResp bnr;
        FillXmppNeighborInfo(&bnr, bsc, bx_channel, summary);
        list->push_back(bnr);
        if (list->size() >= page_limit)
            break;
        if (iter_count >= iter_limit)
            break;
    }

    // All done if we've looked at all channels.
    if (it == bxcm->name_cend() || ++it == bxcm->name_cend())
        return true;

    // Return true if we've reached the page limit, false if we've reached the
    // iteration limit.
    bool done = list->size() >= page_limit;
    *next_neighbor = it->second->ToString();
    return done;
}

static void ShowXmppNeighborStatisticsVisitor(
    size_t *count, const BgpServer *bgp_server, string domain,
    string up_or_down, const BgpXmppChannel *channel) {

    if (boost::iequals(up_or_down, "up") && !channel->Peer()->IsReady()) {
        return;
    }
    if (boost::iequals(up_or_down, "down") && channel->Peer()->IsReady()) {
        return;
    }

    if (!domain.empty()) {
        const RoutingInstanceMgr *rim = bgp_server->routing_instance_mgr();
        const RoutingInstance *ri = rim->GetRoutingInstance(domain);
        if (!ri)
            return;
        const BgpTable *table = ri->GetTable(Address::INET);
        if (!table)
            return;

        const BgpMembershipManager *mgr = bgp_server->membership_mgr();
        if (!mgr->GetRegistrationInfo(channel->Peer(), table)) {
            return;
        }
    }

    ++*count;
}

static void ShowXmppNeighborStatistics(
    size_t *count, const BgpSandeshContext *bsc,
    const ShowNeighborStatisticsReq *req) {
    bsc->xmpp_peer_manager->VisitChannels(
        boost::bind(ShowXmppNeighborStatisticsVisitor, count,
                    bsc->bgp_server, req->get_domain(),
                    req->get_up_or_down(), _1));
}

void RegisterSandeshShowXmppExtensions(BgpSandeshContext *bsc) {
    bsc->SetNeighborShowExtensions(
        &ShowXmppNeighbor,
        &ShowXmppNeighborStatistics);
}
