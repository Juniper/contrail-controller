/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <cmn/agent_cmn.h>
#include <cmn/agent_db.h>
#include <controller/controller_sandesh.h>
#include <controller/controller_types.h>
#include <controller/controller_peer.h>
#include <controller/controller_timer.h>
#include <controller/controller_init.h>
#include <controller/controller_ifmap.h>
#include <controller/controller_dns.h>

void AgentXmppConnectionStatusReq::HandleRequest() const {
    uint8_t count = 0;
    AgentXmppConnectionStatus *resp = new AgentXmppConnectionStatus();
    while (count < MAX_XMPP_SERVERS) {
        if (!Agent::GetInstance()->controller_ifmap_xmpp_server(count).empty()) {

            AgentXmppData data;
            data.set_controller_ip(Agent::GetInstance()->controller_ifmap_xmpp_server(count));
            AgentXmppChannel *ch = Agent::GetInstance()->controller_xmpp_channel(count);
        AgentIfMapXmppChannel *ifmap_ch =
            Agent::GetInstance()->ifmap_xmpp_channel(count);    

            if (ch) {
		XmppChannel *xc = ch->GetXmppChannel(); 
		if (xc->GetPeerState() == xmps::READY) {
		    data.set_state("Established");
		} else {
		    data.set_state("Down");
		}

        data.set_last_ready_time(integerToString(UTCUsecToPTime
             (Agent::GetInstance()->controller_xmpp_channel_setup_time
              (count))));

        //End of config
        ConfigStats config_stats;
        ControllerEndOfConfigStats eoc_stats = config_stats.end_of_config_stats;
        if (ifmap_ch) {
            EndOfConfigTimer *eoc_timer = ifmap_ch->end_of_config_timer();
            eoc_stats.set_last_config_receive_time(integerToString(UTCUsecToPTime
                               (eoc_timer->last_config_receive_time_)));
            eoc_stats.set_inactivity_detected_time(integerToString(UTCUsecToPTime
                               (eoc_timer->inactivity_detected_time_)));
            eoc_stats.set_end_of_config_processed_time(integerToString(UTCUsecToPTime
                               (eoc_timer->end_of_config_processed_time_)));
            if (eoc_timer->fallback_) {
                eoc_stats.set_end_of_config_reason("fallback");
            } else {
                eoc_stats.set_end_of_config_reason("inactivity");
            }
            eoc_stats.set_last_start_time(integerToString(UTCUsecToPTime
                               (eoc_timer->last_restart_time_)));
            eoc_stats.set_running(eoc_timer->running());
        } else {
            eoc_stats.set_last_config_receive_time("");
            eoc_stats.set_inactivity_detected_time("");
            eoc_stats.set_end_of_config_processed_time("");
            eoc_stats.set_end_of_config_reason("");
            eoc_stats.set_last_start_time("");
            eoc_stats.set_running(false);
        }
        config_stats.set_end_of_config_stats(eoc_stats);

        ConfigCleanupStats config_cleanup_stats =
            config_stats.config_cleanup_stats;
        if (ifmap_ch) {
            ConfigCleanupTimer *cleanup_timer = ifmap_ch->config_cleanup_timer();
            config_cleanup_stats.set_last_start_time(integerToString(UTCUsecToPTime
                               (cleanup_timer->last_restart_time_)));
            config_cleanup_stats.set_cleanup_sequence_number
                (cleanup_timer->sequence_number_);
            config_cleanup_stats.set_running(cleanup_timer->running());
        } else {
            config_cleanup_stats.set_last_start_time("");
            config_cleanup_stats.set_cleanup_sequence_number(0);
            config_cleanup_stats.set_running(false);
        }
        config_stats.set_config_cleanup_stats(config_cleanup_stats);
        data.set_config_stats(config_stats);

        //End of rib
        ControllerEndOfRibStats eor_stats;
        ControllerEndOfRibTxStats eor_tx;
        EndOfRibTxTimer *eor_tx_timer = ch->end_of_rib_tx_timer();
        eor_tx.set_end_of_rib_tx_time(integerToString(UTCUsecToPTime
                                     (eor_tx_timer->end_of_rib_tx_time_)));
        eor_tx.set_last_route_published_time(integerToString(UTCUsecToPTime
                                     (eor_tx_timer->last_route_published_time_)));
        if (eor_tx_timer->fallback_) {
            eor_tx.set_end_of_rib_reason("fallback");
        } else {
            eor_tx.set_end_of_rib_reason("inactivity");
        }
        eor_tx.set_last_start_time(integerToString(UTCUsecToPTime
                                     (eor_tx_timer->last_restart_time_)));
        eor_tx.set_running(eor_tx_timer->running());
        eor_stats.set_tx(eor_tx);

        ControllerEndOfRibRxStats eor_rx;
        EndOfRibRxTimer *eor_rx_timer = ch->end_of_rib_rx_timer();
        eor_rx.set_end_of_rib_rx_time(integerToString(UTCUsecToPTime
                                     (eor_rx_timer->end_of_rib_rx_time_)));
        eor_rx.set_last_start_time(integerToString(UTCUsecToPTime
                                  (eor_tx_timer->last_restart_time_)));
        if (eor_rx_timer->fallback_) {
            eor_rx.set_end_of_rib_reason("fallback");
        } else {
            eor_rx.set_end_of_rib_reason("inactivity");
        }
        eor_rx.set_running(eor_rx_timer->running());
        eor_stats.set_rx(eor_rx);
        data.set_end_of_rib_stats(eor_stats);

        data.set_sequence_number(ch->sequence_number());
        data.set_peer_name(xc->ToString());
        data.set_peer_address(xc->PeerAddress());
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

                data.set_xmpp_auth_type(xc->AuthType());
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

                data.set_peer_name(xc->ToString());
                data.set_peer_address(xc->PeerAddress());
                data.set_xmpp_auth_type(xc->AuthType());
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
