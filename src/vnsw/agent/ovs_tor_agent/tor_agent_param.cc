/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

/*
 * Parameters specific to ToR Agent
 */
#include <cmn/agent_cmn.h>
#include <ovs_tor_agent/tor_agent_param.h>

#include <string>

using std::string;

using boost::optional;
namespace boost_po = boost::program_options;

TorAgentParam::TorAgentParam() :
    AgentParam(false, false, false, false) {
}

TorAgentParam::~TorAgentParam() {
}

void TorAgentParam::AddOptions() {
    boost_po::options_description tor("ToR Agent options");
    tor.add_options()
        ("TOR.tor_ip", boost_po::value<string>()->default_value(""),
         "IP Address of the ToR being managed")
        ("TOR.tsn_ip", boost_po::value<string>()->default_value(""),
         "IP Address of the ToR Service Node")
        ("TOR.tor_id", boost_po::value<string>()->default_value(""),
         "Identifier of the TOR")
        ("TOR.tor_ovs_protocol", boost_po::value<string>()->default_value(""),
         "Protocol to be used for connection to TOR")
        ("TOR.ssl_cert", boost_po::value<string>()->default_value(""),
         "SSL Certificate file to be used")
        ("TOR.ssl_privkey", boost_po::value<string>()->default_value(""),
         "SSL Private Key file to be used")
        ("TOR.ssl_cacert", boost_po::value<string>()->default_value(""),
         "SSL CA certificate file to be used for peer validations")
        ("TOR.tor_keepalive_interval", boost_po::value<int>()->default_value(-1),
         "Keepalive interval for TOR in milli seconds")
        ("TOR.tor_ha_stale_route_interval",
         boost_po::value<int>()->default_value(-1),
         "Interval in millisecond for TOR Agent to hold unicast routes as HA Stale");
    AgentParam::AddOptions(tor);
}

void TorAgentParam::ProcessArguments() {
    // Parse common arguments
    AgentParam::ProcessArguments();
    ParseTorArguments();
}

void TorAgentParam::ParseTorArguments() {

    // Parse ToR specific arguments
    boost::program_options::variables_map vars = var_map();

    ParseIpArgument(vars, tor_info_.ip_, "TOR.tor_ip");
    ParseIpArgument(vars, tor_info_.tsn_ip_, "TOR.tsn_ip");
    GetOptValue<string>(vars, tor_info_.id_, "TOR.tor_id");
    GetOptValue<string>(vars, tor_info_.type_, "TOR.tor_type");
    GetOptValue<string>(vars, tor_info_.protocol_, "TOR.tor_ovs_protocol");
    GetOptValue<int>(vars, tor_info_.port_, "TOR.tor_ovs_port");
    GetOptValue<string>(vars, tor_info_.ssl_cert_,"TOR.ssl_cert");
    GetOptValue<string>(vars, tor_info_.ssl_privkey_,"TOR.ssl_privkey");
    GetOptValue<string>(vars, tor_info_.ssl_cacert_,"TOR.ssl_cacert");
    GetOptValue<int>(vars, tor_info_.keepalive_interval_,
                     "TOR.tor_keepalive_interval");
    GetOptValue<int>(vars, tor_info_.ha_stale_route_interval_,
                     "TOR.tor_ha_stale_route_interval");
}

int TorAgentParam::Validate() {
    if (tor_info_.tsn_ip_ == Ip4Address::from_string("0.0.0.0")) {
        LOG(ERROR, "Configuration error. TSN IP address not specified");
        return (EINVAL);
    }

    if (tor_info_.id_ == "") {
        LOG(ERROR, "Configuration error. ToR ID not specified");
        return (EINVAL);
    }

    if (tor_info_.type_ != "ovs") {
        LOG(ERROR, "Configuration error. Unsupported ToR type.");
        LOG(ERROR, "Supported ToR types : <ovs>");
        return (EINVAL);
    }

    if (tor_info_.protocol_ == "tcp") {
        if (tor_info_.ip_ == Ip4Address::from_string("0.0.0.0")) {
            LOG(ERROR, "Configuration error. ToR IP address not specified");
            return (EINVAL);
        }
    } else if (tor_info_.protocol_ == "pssl") {
        if (tor_info_.ssl_cert_ == "") {
            LOG(ERROR, "Configuration error. SSL Certificate not specified");
            return (EINVAL);
        }
        if (tor_info_.ssl_privkey_ == "") {
            LOG(ERROR, "Configuration error. SSL Private Key not specified");
            return (EINVAL);
        }
        if (tor_info_.ssl_cacert_ == "") {
            LOG(ERROR, "Configuration error. SSL CA certificate not specified");
            return (EINVAL);
        }
    } else {
        LOG(ERROR, "Configuration error. Unsupported ToR OVS Protocol.");
        LOG(ERROR, "Supported ToR Protocol : <tcp>");
        return (EINVAL);
    }

    if (tor_info_.port_ == 0) {
        LOG(ERROR, "Configuration error. ToR OVS Protocol Port not specified");
        return (EINVAL);
    }

    return 0;
}
