#include <cmn/agent_cmn.h>
#include <net/address_util.h>
#include <pkt/pkt_types.h>
#include "flow_proto.h"
#include "flow_trace_filter.h"

//////////////////////////////////////////////////////////////////////////////
// Flow trace filter routines
//////////////////////////////////////////////////////////////////////////////
FlowTraceFilter::FlowTraceFilter() :
    enabled_(), src_addr_(), src_mask_(), dst_addr_(), dst_mask_(),
    proto_start_(), proto_end_(), src_port_start_(), src_port_end_(),
    dst_port_start_(), dst_port_end_() {
    count_ = 0;
}

void FlowTraceFilter::Reset(bool enable, Address::Family family) {
    boost::system::error_code ec;
    enabled_ = enable;
    family_ = family;
    if (family == Address::INET) {
        src_addr_ = dst_addr_ = Ip4Address::from_string("0.0.0.0", ec);
        src_mask_ = dst_mask_ = Ip4Address::from_string("0.0.0.0", ec);
    } else {
        src_addr_ = dst_addr_ = Ip6Address::from_string("::0", ec);
        src_mask_ = dst_mask_ = Ip6Address::from_string("::0", ec);
    }
    proto_start_ = 0;
    proto_end_ = 0xFF;

    src_port_start_ = dst_port_start_ = 0;
    src_port_end_ = dst_port_end_ = 0xFFFF;
    count_ = 0;
}

void FlowTraceFilter::Init(bool enable, Address::Family family) {
    Reset(enable, family);
}

void FlowTraceFilter::SetFilter(bool enable, Address::Family family,
                                const std::string &src_addr, uint8_t src_plen,
                                const std::string &dst_addr, uint8_t dst_plen,
                                uint8_t proto_start, uint8_t proto_end,
                                uint16_t src_port_start, uint16_t src_port_end,
                                uint16_t dst_port_start, uint16_t dst_port_end){
    enabled_ = enable;
    if (enabled_ == false) {
        Reset(enable, family);
        return;
    }

    boost::system::error_code ec;
    src_addr_ = Ip4Address::from_string(src_addr, ec);
    if (src_addr_.is_v4()) {
        src_mask_ = PrefixToIpNetmask(src_plen);
        uint32_t addr = (src_addr_.to_v4().to_ulong() &
                         src_mask_.to_v4().to_ulong());
        src_addr_ = Ip4Address(addr);
    } else {
        src_mask_ = PrefixToIp6Netmask(src_plen);
        boost::array<uint8_t, 16> addr_bytes = src_addr_.to_v6().to_bytes();
        boost::array<uint8_t, 16> mask_bytes = src_mask_.to_v6().to_bytes();
        for (int i = 0; i < 16; i++) {
            addr_bytes[i] = addr_bytes[i] & mask_bytes[i];
        }
        src_addr_ = Ip6Address(addr_bytes);

    }
    dst_addr_ = Ip4Address::from_string(dst_addr, ec);
    if (dst_addr_.is_v4()) {
        dst_mask_ = PrefixToIpNetmask(dst_plen);
        uint32_t addr = (dst_addr_.to_v4().to_ulong() &
                         dst_mask_.to_v4().to_ulong());
        dst_addr_ = Ip4Address(addr);
    } else {
        dst_mask_ = PrefixToIp6Netmask(dst_plen);
        boost::array<uint8_t, 16> addr_bytes = dst_addr_.to_v6().to_bytes();
        boost::array<uint8_t, 16> mask_bytes = dst_mask_.to_v6().to_bytes();
        for (int i = 0; i < 16; i++) {
            addr_bytes[i] = addr_bytes[i] & mask_bytes[i];
        }
        dst_addr_ = Ip6Address(addr_bytes);
    }
    proto_start_ = proto_start;
    proto_end_ = proto_end;
    src_port_start_ = src_port_start;
    src_port_end_ = src_port_end;
    dst_port_start_ = dst_port_start;
    dst_port_end_ = dst_port_end;
    count_ = 0;
}

static bool Ip4Match(const Ip4Address &ip1, const Ip4Address &ip2,
                     const Ip4Address &mask) {
    return ((ip1.to_ulong() & mask.to_ulong()) == ip2.to_ulong());
}

static bool Ip6Match(const Ip6Address &ip1, const Ip6Address &ip2,
                     const Ip6Address &mask) {
    boost::array<uint8_t, 16> ip1_bytes = ip1.to_bytes();
    boost::array<uint8_t, 16> ip2_bytes = ip2.to_bytes();
    boost::array<uint8_t, 16> mask_bytes = mask.to_bytes();

    for (int i = 0; i < 16; i++) {
        if ((ip1_bytes[i] & mask_bytes[i]) != ip2_bytes[i])
            return false;
    }
    return true;
}

bool FlowTraceFilter::Match(const FlowKey *key) {
    if (enabled_ == false)
        return false;

    if (key->family == Address::INET) {
        if (Ip4Match(key->src_addr.to_v4(), src_addr_.to_v4(),
                     src_mask_.to_v4()) == false)
            return false;
    } else {
        if (Ip6Match(key->src_addr.to_v6(), src_addr_.to_v6(),
                     src_mask_.to_v6()) == false)
            return false;
    }

    if (key->family == Address::INET) {
        if (Ip4Match(key->dst_addr.to_v4(), dst_addr_.to_v4(),
                     dst_mask_.to_v4()) == false)
            return false;
    } else {
        if (Ip6Match(key->dst_addr.to_v6(), dst_addr_.to_v6(),
                     dst_mask_.to_v6()) == false)
            return false;
    }

    if (key->protocol < proto_start_ || key->protocol > proto_end_)
        return false;

    if (key->src_port < src_port_start_ || key->src_port > src_port_end_)
        return false;

    if (key->dst_port < dst_port_start_ || key->dst_port > dst_port_end_)
        return false;

    count_++;
    return true;
}

void FlowTraceFilter::ToSandesh(SandeshFlowFilterInfo *info) const {
    info->set_enabled(enabled_);
    info->set_src_address(src_addr_.to_string());
    info->set_src_mask(src_mask_.to_string());
    info->set_dst_address(dst_addr_.to_string());
    info->set_dst_mask(dst_mask_.to_string());
    info->set_proto_start(proto_start_);
    info->set_proto_end(proto_end_);
    info->set_src_port_start(src_port_start_);
    info->set_src_port_end(src_port_end_);
    info->set_dst_port_start(dst_port_start_);
    info->set_dst_port_end(dst_port_end_);
    info->set_flow_hit(count_);
}

static bool ValidateIPv4(const std::string &addr, uint8_t plen) {
    std::string msg;
    if (ValidateIPAddressString(addr, &msg) == false) {
        return false;
    }

    boost::system::error_code ec;
    IpAddress ip = IpAddress::from_string(addr, ec);
    if (ec) {
        return false;
    }

    if (ip.is_v4() == false)
        return false;

    if (plen > 32) {
        return false;
    }

    return true;
}

static bool ValidateIPv6(const std::string &addr, uint8_t plen) {
    std::string msg;
    if (ValidateIPAddressString(addr, &msg) == false) {
        return false;
    }

    boost::system::error_code ec;
    IpAddress ip = IpAddress::from_string(addr, ec);
    if (ec) {
        return false;
    }

    if (ip.is_v6() == false)
        return false;

    if (plen > 128) {
        return false;
    }

    return true;
}

static void ErrorResponse(const std::string &msg, const std::string &context) {
    FlowErrorResp *resp = new FlowErrorResp();
    resp->set_resp(msg);
    resp->set_context(context);
    resp->set_more(false);
    resp->Response();
    return;
}

static bool Validate(const std::string &context,
                     uint8_t proto_start, uint8_t proto_end,
                     uint16_t src_port_start, uint16_t src_port_end,
                     uint16_t dst_port_start, uint16_t dst_port_end) {
    if (proto_start > proto_end) {
        ErrorResponse("Invalid protocol range", context);
        return false;
    }

    if (src_port_start > src_port_end) {
        ErrorResponse("Invalid src-port range", context);
        return false;
    }

    if (dst_port_start > dst_port_end) {
        ErrorResponse("Invalid dst-port range", context);
        return false;
    }

    return true;
}

static void SandeshResponse(FlowTraceFilter *ipv4, FlowTraceFilter *ipv6,
                            const std::string &context) {
    SandeshFlowFilterResponse *resp = new SandeshFlowFilterResponse();

    SandeshFlowFilterInfo info;
    ipv4->ToSandesh(&info);
    resp->set_ipv4_filter(info);

    ipv6->ToSandesh(&info);
    resp->set_ipv6_filter(info);

    resp->set_context(context);
    resp->set_more(false);
    resp->Response();
}

void SandeshIPv4FlowFilterRequest::HandleRequest() const {
    if (get_enabled()) {
        if (ValidateIPv4(get_src_address(), get_src_prefix_len()) == false) {
            ErrorResponse("Invalid src-address or src-prefix-len ", context());
            return;
        }

        if (ValidateIPv4(get_dst_address(), get_dst_prefix_len()) == false) {
            ErrorResponse("Invalid dst-address or dst-prefix-len ", context());
            return;
        }

        if (Validate(context(), proto_start, proto_end, src_port_start,
                     src_port_end, dst_port_start, dst_port_end) == false) {
            return;
        }
    }

    Agent *agent = Agent::GetInstance();
    FlowProto *proto = agent->pkt()->get_flow_proto();
    FlowTraceFilter *filter = proto->ipv4_trace_filter();
    filter->SetFilter(get_enabled(), Address::INET,
                      get_src_address(), get_src_prefix_len(),
                      get_dst_address(), get_dst_prefix_len(),
                      get_proto_start(), get_proto_end(),
                      get_src_port_start(), get_src_port_end(),
                      get_dst_port_start(), get_dst_port_end());
    SandeshResponse(proto->ipv4_trace_filter(), proto->ipv6_trace_filter(),
                    context());
}

void SandeshIPv6FlowFilterRequest::HandleRequest() const {
    if (get_enabled()) {
        if (ValidateIPv6(get_src_address(), get_src_prefix_len()) == false) {
            ErrorResponse("Invalid src-address or src-prefix-len ", context());
            return;
        }

        if (ValidateIPv6(get_dst_address(), get_dst_prefix_len()) == false) {
            ErrorResponse("Invalid dst-address or dst-prefix-len ", context());
            return;
        }

        if (Validate(context(), proto_start, proto_end, src_port_start,
                     src_port_end, dst_port_start, dst_port_end) == false) {
            return;
        }
    }

    Agent *agent = Agent::GetInstance();
    FlowProto *proto = agent->pkt()->get_flow_proto();
    FlowTraceFilter *filter = proto->ipv6_trace_filter();
    filter->SetFilter(get_enabled(), Address::INET6,
                      get_src_address(), get_src_prefix_len(),
                      get_dst_address(), get_dst_prefix_len(),
                      get_proto_start(), get_proto_end(),
                      get_src_port_start(), get_src_port_end(),
                      get_dst_port_start(), get_dst_port_end());
    SandeshResponse(proto->ipv4_trace_filter(), proto->ipv6_trace_filter(),
                    context());
    return;
}

void SandeshShowFlowFilterRequest::HandleRequest() const {
    Agent *agent = Agent::GetInstance();
    FlowProto *proto = agent->pkt()->get_flow_proto();
    SandeshResponse(proto->ipv4_trace_filter(), proto->ipv6_trace_filter(),
                    context());
    return;
}
