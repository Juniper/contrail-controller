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

TorAgentParam::TorAgentParam(Agent *agent) :
    AgentParam(agent, false, false, false, false) {
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
         "Identifier of the TOR");
    AgentParam::AddOptions(tor);
}

void TorAgentParam::InitFromConfig() {
    set_mode(MODE_TOR_AGENT);
    // Parse common config elements
    AgentParam::InitFromConfig();

    // Parse ToR specific arguments
    ParseIp("TOR.tor_ip", &tor_info_.ip_);
    ParseIp("TOR.tsn_ip", &tor_info_.tsn_ip_);
    GetValueFromTree<string>(tor_info_.id_, "TOR.tor_id");
    GetValueFromTree<string>(tor_info_.type_, "TOR.tor_type");
    GetValueFromTree<string>(tor_info_.protocol_, "TOR.tor_ovs_protocol");
    GetValueFromTree<int>(tor_info_.port_, "TOR.tor_ovs_port");
}

void TorAgentParam::InitFromArguments() {
    // Parse common arguments
    AgentParam::InitFromArguments();

    // Parse ToR specific arguments
    boost::program_options::variables_map vars = var_map();

    ParseIpArgument(vars, tor_info_.ip_, "TOR.tor_ip");
    ParseIpArgument(vars, tor_info_.tsn_ip_, "TOR.tsn_ip");
    GetOptValue<string>(vars, tor_info_.id_, "TOR.tor_id");
    GetOptValue<string>(vars, tor_info_.type_, "TOR.tor_type");
    GetOptValue<string>(vars, tor_info_.protocol_, "TOR.tor_ovs_protocol");
    GetOptValue<int>(vars, tor_info_.port_, "TOR.tor_ovs_port");
}

int TorAgentParam::Validate() {
    if (tor_info_.ip_ == Ip4Address::from_string("0.0.0.0")) {
        LOG(ERROR, "Configuration error. ToR IP address not specified");
        return (EINVAL);
    }

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

    if (tor_info_.protocol_ != "tcp") {
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
