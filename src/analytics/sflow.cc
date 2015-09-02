/*
 * Copyright (c) 2014 Juniper Networks, Inc. All rights reserved.
 */

#include "base/string_util.h"
#include "analytics/sflow.h"


bool SFlowHeader::operator==(const SFlowHeader& rhs) const {
    return (version == rhs.version &&
            agent_ip_address == rhs.agent_ip_address &&
            agent_subid == rhs.agent_subid &&
            seqno == rhs.seqno &&
            uptime == rhs.uptime &&
            nsamples == rhs.nsamples);
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

bool SFlowFlowSample::operator==(const SFlowFlowSample& rhs) const {
    if (!(seqno == rhs.seqno &&
          sourceid_type == rhs.sourceid_type &&
          sourceid_index == rhs.sourceid_index &&
          sample_rate == rhs.sample_rate &&
          sample_pool == rhs.sample_pool &&
          drops == rhs.drops &&
          input_port_format == rhs.input_port_format &&
          input_port == rhs.input_port &&
          output_port_format == rhs.output_port_format &&
          output_port == rhs.output_port &&
          nflow_records == rhs.nflow_records)) {
        return false;
    }
    if (flow_records.size() != rhs.flow_records.size()) {
        return false;
    }
    boost::ptr_vector<SFlowFlowRecord>::const_iterator it1 =
        flow_records.begin();
    boost::ptr_vector<SFlowFlowRecord>::const_iterator it2 =
        rhs.flow_records.begin();
    for (; it1 != flow_records.end(); ++it1, ++it2) {
        if (!(it1->type == it2->type && it1->length == it2->length)) {
            return false;
        }
        switch(it1->type) {
        case SFLOW_FLOW_HEADER: {
            SFlowFlowHeader& sflow_header1 = (SFlowFlowHeader&)*it1;
            SFlowFlowHeader& sflow_header2 = (SFlowFlowHeader&)*it2;
            if (!(sflow_header1 == sflow_header2)) {
                return false;
            }
            break;
        }
        default:
            assert(0);
        }
    }
    return true;
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
    boost::ptr_vector<SFlowFlowRecord>::const_iterator it =
        flow_sample.flow_records.begin();
    for (; it != flow_sample.flow_records.end(); ++it) {
        switch(it->type) {
        case SFLOW_FLOW_HEADER: {
            SFlowFlowHeader& sflow_header = (SFlowFlowHeader&)*it;
            out << sflow_header;
            break;
        }
        default:
            assert(0);
        }
    }
    return out;
}

bool SFlowFlowEthernetData::operator==(const SFlowFlowEthernetData& rhs) const {
    return (src_mac == rhs.src_mac &&
            dst_mac == rhs.dst_mac &&
            vlan_id == rhs.vlan_id &&
            ether_type == rhs.ether_type);
}

std::ostream &operator<<(std::ostream &out,
                         const SFlowFlowEthernetData &eth_data) {
    out << "== Ethernet Data ==" << std::endl;
    out << "Source Mac: " << eth_data.src_mac.ToString() << std::endl;
    out << "Destination Mac: " << eth_data.dst_mac.ToString() << std::endl;
    out << "Vlan Id: " << eth_data.vlan_id << std::endl;
    out << "Ether type: " << integerToHexString(eth_data.ether_type)
        << std::endl;
    return out;
}

bool SFlowFlowIpData::operator==(const SFlowFlowIpData& rhs) const {
    return (length == rhs.length &&
            protocol == rhs.protocol &&
            src_ip == rhs.src_ip &&
            dst_ip == rhs.dst_ip &&
            src_port == rhs.src_port &&
            dst_port == rhs.dst_port &&
            tcp_flags == rhs.tcp_flags &&
            tos == rhs.tos);
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

bool SFlowFlowHeader::operator==(const SFlowFlowHeader& rhs) const {
    if (!(protocol == rhs.protocol &&
          frame_length == rhs.frame_length &&
          stripped == rhs.stripped &&
          header_length == rhs.header_length)) {
        return false;
    }
    if (memcmp(header, rhs.header, header_length)) {
        return false;
    }
    if (is_eth_data_set == rhs.is_eth_data_set) {
        if (!(decoded_eth_data == rhs.decoded_eth_data)) {
            return false;
        }
    } else {
        return false;
    }
    if (is_ip_data_set == rhs.is_ip_data_set) {
        if (!(decoded_ip_data == rhs.decoded_ip_data)) {
            return false;
        }
    } else {
        return false;
    }
    return true;
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
