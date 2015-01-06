/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "analytics/sflow.h"

#include <iomanip>

#include "base/string_util.h"

std::ostream &operator<<(std::ostream &out, const SFlowMacaddress &sflow_mac) {
    std::stringstream mac_str;
    for (int i = 0; i < 6; ++i) {
        mac_str << std::setfill('0') << std::setw(2) << std::hex <<
                int(sflow_mac.addr[i]) << (i < 5 ? ":":"");
    }
    out << mac_str.str();
    return out;
}

std::ostream& operator<<(std::ostream& out,
                         const SFlowIpaddress &sflow_ipaddr) {
    out << sflow_ipaddr.ToString();
    return out;
}

std::ostream &operator<<(std::ostream &out, 
                         const SFlowHeader &sflow_header) {
    out << "== sFlow Header ==" << std::endl;
    out << "Version: " << sflow_header.version << std::endl;
    out << "Agent " << sflow_header.agent_ip_address 
        << std::endl;
    out << "Agent subid: " << sflow_header.agent_subid << std::endl;
    out << "Sequence no: " << sflow_header.seqno << std::endl;
    out << "Uptime: " << sflow_header.uptime << std::endl;
    out << "Num samples: " << sflow_header.nsamples << std::endl;
    return out;
}

std::ostream &operator<<(std::ostream &out, 
                         const SFlowFlowSample &flow_sample) {
    out << "== Flow Sample ==" << std::endl;
    out << "Seqno: " << flow_sample.seqno << std::endl;
    out << "Sourceid type: " << flow_sample.sourceid_type << std::endl;
    out << "Sourceid index: " << flow_sample.sourceid_index << std::endl;
    out << "Sample rate: " << flow_sample.sample_rate << std::endl;
    out << "Sample pool: " << flow_sample.sample_pool << std::endl;
    out << "Drops: " << flow_sample.drops << std::endl;
    out << "Input port format: " << flow_sample.input_port_format << std::endl;
    out << "Input port: " << flow_sample.input_port << std::endl;
    out << "Output port format: " << flow_sample.output_port_format
        << std::endl;
    out << "Output port: " << flow_sample.output_port << std::endl;
    out << "Num Flow records: " << flow_sample.nflow_records << std::endl;
    return out;
}

std::ostream &operator<<(std::ostream &out,
                         const SFlowFlowEthernetData &eth_data) {
    out << "== Ethernet Data ==" << std::endl;
    out << "Source Mac: " << eth_data.src_mac << std::endl;
    out << "Destination Mac: " << eth_data.dst_mac << std::endl;
    out << "Vlan Id: " << eth_data.vlan_id << std::endl;
    out << "Ether type: " << integerToHexString(eth_data.ether_type)
        << std::endl;
    return out;
}

std::ostream &operator<<(std::ostream &out, 
                         const SFlowFlowIpData &ip_data) { 
    out << "== IP Data ==" << std::endl;
    out << "Length: " << ip_data.length << std::endl;
    out << "Source " << ip_data.src_ip << std::endl;
    out << "Destination " << ip_data.dst_ip << std::endl;
    out << "Source port: " << ip_data.src_port << std::endl;
    out << "Destination port: " << ip_data.dst_port << std::endl;
    out << "Tos: " << ip_data.tos << std::endl;
    return out;
}

std::ostream &operator<<(std::ostream &out,
                         const SFlowFlowHeader &flow_header) {
    out << "== Flow Header ==" << std::endl;
    out << "Protocol: " << flow_header.protocol << std::endl;
    out << "Frame length: " << flow_header.frame_length << std::endl;
    out << "Stripped: " << flow_header.stripped << std::endl;
    out << "Header length: " << flow_header.header_length << std::endl;
    if (flow_header.is_eth_data_set) {
        out << flow_header.decoded_eth_data;
    }
    if (flow_header.is_ip_data_set) {
        out << flow_header.decoded_ip_data;
    }
    return out;
}
