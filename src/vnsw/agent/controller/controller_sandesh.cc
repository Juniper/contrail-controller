/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <cmn/agent_db.h>
#include <controller/controller_sandesh.h>
#include <controller/controller_types.h>
#include <controller/controller_peer.h>
#include <controller/controller_dns.h>

void AgentXmppConnectionStatusReq::HandleRequest() const {
    uint8_t count = 0;
    AgentXmppConnectionStatus *resp = new AgentXmppConnectionStatus();
    while (count < MAX_XMPP_SERVERS) {
        if (!Agent::GetInstance()->controller_ifmap_xmpp_server(count).empty()) {

            AgentXmppData data;
            data.set_controller_ip(Agent::GetInstance()->controller_ifmap_xmpp_server(count));
            AgentXmppChannel *ch = Agent::GetInstance()->controller_xmpp_channel(count);
            if (ch) {
		XmppChannel *xc = ch->GetXmppChannel(); 
		if (xc->GetPeerState() == xmps::READY) {
		    data.set_state("Established");
		} else {
		    data.set_state("Down");
		}

		if (Agent::GetInstance()->mulitcast_builder() == ch) {
		    data.set_mcast_controller("Yes");
		} else {
		    data.set_mcast_controller("No");
		}

		if (Agent::GetInstance()->ifmap_active_xmpp_server().compare
                    (Agent::GetInstance()->controller_ifmap_xmpp_server(count)) == 0) {
		    data.set_cfg_controller("Yes");
		} else {
		    data.set_cfg_controller("No");
		}

		data.set_last_state(xc->LastStateName());
		data.set_last_event(xc->LastEvent());
		data.set_last_state_at(xc->LastStateChangeAt());
		data.set_flap_count(xc->FlapCount());
		data.set_flap_time(xc->LastFlap());

		ControllerProtoStats rx_proto_stats;
		rx_proto_stats.open = xc->rx_open();
		rx_proto_stats.keepalive = xc->rx_keepalive();
		rx_proto_stats.update = xc->rx_update();
		rx_proto_stats.close = xc->rx_close();

		ControllerProtoStats tx_proto_stats;
		tx_proto_stats.open = xc->tx_open();
		tx_proto_stats.keepalive = xc->tx_keepalive();
		tx_proto_stats.update = xc->tx_update();
		tx_proto_stats.close = xc->tx_close();

		data.set_rx_proto_stats(rx_proto_stats); 
                data.set_tx_proto_stats(tx_proto_stats); 
            }

	    std::vector<AgentXmppData> &list =
	        const_cast<std::vector<AgentXmppData>&>(resp->get_peer());
	    list.push_back(data);
        }
        count++;
    }
    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}

void AgentDnsXmppConnectionStatusReq::HandleRequest() const {
    uint8_t dns_count = 0;

    AgentDnsXmppConnectionStatus *resp = new AgentDnsXmppConnectionStatus();
    while (dns_count < MAX_XMPP_SERVERS) {
        if (!Agent::GetInstance()->dns_server(dns_count).empty()) {

            AgentXmppDnsData data;
            data.set_dns_controller_ip(Agent::GetInstance()->dns_server(dns_count));

            AgentDnsXmppChannel *ch = Agent::GetInstance()->dns_xmpp_channel(dns_count);
            if (ch) {
		XmppChannel *xc = ch->GetXmppChannel();
		if (xc->GetPeerState() == xmps::READY) {
		    data.set_state("Established");
		} else {
		    data.set_state("Down");
		}

		data.set_last_state(xc->LastStateName());
		data.set_last_event(xc->LastEvent());
		data.set_last_state_at(xc->LastStateChangeAt());
		data.set_flap_count(xc->FlapCount());
		data.set_flap_time(xc->LastFlap());

		ControllerProtoStats rx_proto_stats;
		rx_proto_stats.open = xc->rx_open();
		rx_proto_stats.keepalive = xc->rx_keepalive();
		rx_proto_stats.update = xc->rx_update();
		rx_proto_stats.close = xc->rx_close();

		ControllerProtoStats tx_proto_stats;
		tx_proto_stats.open = xc->tx_open();
		tx_proto_stats.keepalive = xc->tx_keepalive();
		tx_proto_stats.update = xc->tx_update();
		tx_proto_stats.close = xc->tx_close();

		data.set_rx_proto_stats(rx_proto_stats);
                data.set_tx_proto_stats(tx_proto_stats);
            }

	    std::vector<AgentXmppDnsData> &list =
	        const_cast<std::vector<AgentXmppDnsData>&>(resp->get_peer());
	    list.push_back(data);
        }
        dns_count++;
    }
    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}

void AgentDiscoveryXmppConnectionsRequest::HandleRequest() const {

    AgentDiscoveryXmppConnectionsResponse *resp =
        new AgentDiscoveryXmppConnectionsResponse();

    std::vector<DSResponse> ds_reponse =
        Agent::GetInstance()->GetDiscoveryServerResponseList();

    std::vector<DSResponse>::iterator iter;
    for (iter = ds_reponse.begin(); iter != ds_reponse.end(); iter++) {
        DSResponse dr = *iter;

        AgentDiscoveryXmppConnections data;
        data.set_discovery_controller_ip(dr.ep.address().to_string());
        data.set_discovery_controller_port(dr.ep.port());

        std::vector<AgentDiscoveryXmppConnections> &list =
            const_cast<std::vector<AgentDiscoveryXmppConnections>&>
            (resp->get_xmpp_discovery_list());
        list.push_back(data);
    }

    uint8_t count = 0;
    while (count < MAX_XMPP_SERVERS) {

        AgentXmppInUseConnections data;
        if (!Agent::GetInstance()->controller_ifmap_xmpp_server(count).empty()) {
            data.set_controller_ip(
                Agent::GetInstance()->controller_ifmap_xmpp_server(count));
            data.set_controller_port(
                Agent::GetInstance()->controller_ifmap_xmpp_port(count));
        }

        std::vector<AgentXmppInUseConnections> &list =
            const_cast<std::vector<AgentXmppInUseConnections>&>
            (resp->get_xmpp_inuse_connections());
        list.push_back(data);

        count++;
    }

    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}

void AgentDiscoveryDnsXmppConnectionsRequest::HandleRequest() const {

    AgentDiscoveryDnsXmppConnectionsResponse *resp =
        new AgentDiscoveryDnsXmppConnectionsResponse();

    std::vector<DSResponse> ds_reponse =
        Agent::GetInstance()->GetDiscoveryDnsServerResponseList();

    std::vector<DSResponse>::iterator iter;
    for (iter = ds_reponse.begin(); iter != ds_reponse.end(); iter++) {
        DSResponse dr = *iter;

        AgentDiscoveryXmppConnections data;
        data.set_discovery_controller_ip(dr.ep.address().to_string());
        data.set_discovery_controller_port(dr.ep.port());

        std::vector<AgentDiscoveryXmppConnections> &list =
            const_cast<std::vector<AgentDiscoveryXmppConnections>&>
            (resp->get_xmpp_discovery_list());
        list.push_back(data);
    }

    uint8_t dns_count = 0;
    while (dns_count < MAX_XMPP_SERVERS) {

        AgentXmppInUseConnections data;
        if (!Agent::GetInstance()->dns_server(dns_count).empty()) {
            data.set_controller_ip(
                Agent::GetInstance()->dns_server(dns_count));
            data.set_controller_port(
                Agent::GetInstance()->dns_server_port(dns_count));
        }

        std::vector<AgentXmppInUseConnections> &list =
            const_cast<std::vector<AgentXmppInUseConnections>&>
            (resp->get_xmpp_inuse_connections());
        list.push_back(data);

        dns_count++;
    }

    resp->set_context(context());
    resp->set_more(false);
    resp->Response();
}
