/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include "bgp/evpn/evpn_route.h"
#include "bgp/evpn/evpn_table.h"

#include "bgp/bgp_server.h"

using namespace std;

const EvpnPrefix EvpnPrefix::kNullPrefix;

const uint32_t EvpnPrefix::kInvalidLabel = 0x100000;
const uint32_t EvpnPrefix::kNullTag = 0;
const uint32_t EvpnPrefix::kMaxTag = 0xFFFFFFFF;

const size_t EvpnPrefix::kRdSize = RouteDistinguisher::kSize;
const size_t EvpnPrefix::kEsiSize = EthernetSegmentId::kSize;
const size_t EvpnPrefix::kTagSize = 4;
const size_t EvpnPrefix::kMacSize = MacAddress::size();
const size_t EvpnPrefix::kLabelSize = BgpProtoPrefix::kLabelSize;

const size_t EvpnPrefix::kMinAutoDiscoveryRouteSize =
    kRdSize + kEsiSize + kTagSize;
const size_t EvpnPrefix::kMinMacAdvertisementRouteSize =
    kRdSize + kEsiSize + kTagSize + 1 + kMacSize + 1;
const size_t EvpnPrefix::kMinInclusiveMulticastRouteSize =
    kRdSize + kTagSize + 1;
const size_t EvpnPrefix::kMinSegmentRouteSize =
    kRdSize + kEsiSize + 1;

EvpnPrefix::EvpnPrefix()
    : type_(Unspecified), tag_(EvpnPrefix::kNullTag), family_(Address::UNSPEC) {
}

EvpnPrefix::EvpnPrefix(const RouteDistinguisher &rd,
    const EthernetSegmentId &esi, uint32_t tag)
    : type_(AutoDiscoveryRoute),
      rd_(rd), esi_(esi), tag_(tag), family_(Address::UNSPEC) {
}

EvpnPrefix::EvpnPrefix(const RouteDistinguisher &rd,
    const MacAddress &mac_addr, const IpAddress &ip_address)
    : type_(MacAdvertisementRoute),
      rd_(rd), tag_(EvpnPrefix::kNullTag),
      mac_addr_(mac_addr), family_(Address::UNSPEC), ip_address_(ip_address) {
    if (ip_address_.is_v4() && !ip_address_.is_unspecified()) {
        family_ = Address::INET;
    } else if (ip_address_.is_v6() && !ip_address_.is_unspecified()) {
        family_ = Address::INET6;
    }
}

EvpnPrefix::EvpnPrefix(const RouteDistinguisher &rd, uint32_t tag,
    const MacAddress &mac_addr, const IpAddress &ip_address)
    : type_(MacAdvertisementRoute),
      rd_(rd), tag_(tag),
      mac_addr_(mac_addr), family_(Address::UNSPEC), ip_address_(ip_address) {
    if (ip_address_.is_v4() && !ip_address_.is_unspecified()) {
        family_ = Address::INET;
    } else if (ip_address_.is_v6() && !ip_address_.is_unspecified()) {
        family_ = Address::INET6;
    }
}

EvpnPrefix::EvpnPrefix(const RouteDistinguisher &rd, uint32_t tag,
    const IpAddress &ip_address)
    : type_(InclusiveMulticastRoute), rd_(rd), tag_(tag),
      family_(Address::UNSPEC), ip_address_(ip_address) {
    if (ip_address_.is_v4() && !ip_address_.is_unspecified()) {
        family_ = Address::INET;
    } else if (ip_address_.is_v6() && !ip_address_.is_unspecified()) {
        family_ = Address::INET6;
    }
}

EvpnPrefix::EvpnPrefix(const RouteDistinguisher &rd,
    const EthernetSegmentId &esi, const IpAddress &ip_address)
    : type_(SegmentRoute),
      rd_(rd), esi_(esi), tag_(EvpnPrefix::kNullTag),
      family_(Address::UNSPEC), ip_address_(ip_address) {
    if (ip_address_.is_v4() && !ip_address_.is_unspecified()) {
        family_ = Address::INET;
    } else if (ip_address_.is_v6() && !ip_address_.is_unspecified()) {
        family_ = Address::INET6;
    }
}

int EvpnPrefix::FromProtoPrefix(BgpServer *server,
    const BgpProtoPrefix &proto_prefix, const BgpAttr *attr,
    EvpnPrefix *prefix, BgpAttrPtr *new_attr, uint32_t *label) {
    *new_attr = attr;
    *label = 0;
    prefix->type_ = proto_prefix.type;
    size_t nlri_size = proto_prefix.prefix.size();

    switch (prefix->type_) {
    case AutoDiscoveryRoute: {
        size_t expected_min_nlri_size =
            kMinAutoDiscoveryRouteSize + (attr ? kLabelSize : 0);
        if (nlri_size < expected_min_nlri_size)
            return -1;
        size_t rd_offset = 0;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t esi_offset = rd_offset + kRdSize;
        prefix->esi_ = EthernetSegmentId(&proto_prefix.prefix[esi_offset]);
        size_t tag_offset = esi_offset + kEsiSize;
        prefix->tag_ = get_value(&proto_prefix.prefix[tag_offset], kTagSize);
        if (attr) {
            size_t label_offset = tag_offset + kTagSize;
            *label = proto_prefix.ReadLabel(label_offset);
        }
        break;
    }
    case MacAdvertisementRoute: {
        size_t expected_min_nlri_size =
            kMinMacAdvertisementRouteSize + (attr ? kLabelSize : 0);
        if (nlri_size < expected_min_nlri_size)
            return -1;
        size_t rd_offset = 0;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t esi_offset = rd_offset + kRdSize;
        if (attr) {
            EthernetSegmentId esi(&proto_prefix.prefix[esi_offset]);
            *new_attr = server->attr_db()->ReplaceEsiAndLocate(attr, esi);
        }
        size_t tag_offset = esi_offset + kEsiSize;
        prefix->tag_ = get_value(&proto_prefix.prefix[tag_offset], kTagSize);
        size_t mac_len_offset = tag_offset + kTagSize;
        size_t mac_len = proto_prefix.prefix[mac_len_offset];
        if (mac_len != 48)
            return -1;
        size_t mac_offset = mac_len_offset + 1;
        prefix->mac_addr_ = MacAddress(&proto_prefix.prefix[mac_offset]);
        size_t ip_len_offset = mac_offset + kMacSize;
        size_t ip_len = proto_prefix.prefix[ip_len_offset];
        if (ip_len != 0 && ip_len != 32 && ip_len != 128)
            return -1;
        size_t ip_size = ip_len / 8;
        if (nlri_size < expected_min_nlri_size + ip_size)
            return -1;
        size_t ip_offset = ip_len_offset + 1;
        prefix->ReadIpAddress(proto_prefix, ip_offset, ip_size);
        if (attr) {
            size_t label_offset = ip_offset + ip_size;
            *label = proto_prefix.ReadLabel(label_offset);
        }
        break;
    }
    case InclusiveMulticastRoute: {
        if (nlri_size < kMinInclusiveMulticastRouteSize)
            return -1;
        size_t rd_offset = 0;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t tag_offset = rd_offset + kRdSize;
        prefix->tag_ = get_value(&proto_prefix.prefix[tag_offset], kTagSize);
        size_t ip_len_offset = tag_offset + kTagSize;
        size_t ip_len = proto_prefix.prefix[ip_len_offset];
        if (ip_len != 32 && ip_len != 128)
            return -1;
        size_t ip_size = ip_len / 8;
        if (nlri_size < kMinInclusiveMulticastRouteSize + ip_size)
            return -1;
        size_t ip_offset = ip_len_offset + 1;
        prefix->ReadIpAddress(proto_prefix, ip_offset, ip_size);
        const PmsiTunnel *pmsi_tunnel = attr ? attr->pmsi_tunnel() : NULL;
        if (pmsi_tunnel &&
            pmsi_tunnel->tunnel_type == PmsiTunnelSpec::IngressReplication) {
            *label = pmsi_tunnel->label;
        }
        break;
    }
    case SegmentRoute: {
        if (nlri_size < kMinSegmentRouteSize)
            return -1;
        size_t rd_offset = 0;
        prefix->rd_ = RouteDistinguisher(&proto_prefix.prefix[rd_offset]);
        size_t esi_offset = rd_offset + kRdSize;
        prefix->esi_ = EthernetSegmentId(&proto_prefix.prefix[esi_offset]);
        size_t ip_len_offset = esi_offset + kEsiSize;
        size_t ip_len = proto_prefix.prefix[ip_len_offset];
        if (ip_len != 32 && ip_len != 128)
            return -1;
        size_t ip_size = ip_len / 8;
        if (nlri_size < kMinSegmentRouteSize + ip_size)
            return -1;
        size_t ip_offset = ip_len_offset + 1;
        prefix->ReadIpAddress(proto_prefix, ip_offset, ip_size);
        break;
    }
    default: {
        return -1;
        break;
    }
    }
    return 0;
}

//
// Build the BgpProtoPrefix for this EvpnPrefix.
//
// The ESI for MacAdvertisementRoute is not part of the key and hence
// must be obtained from the BgpAttr.The BgpAttr is NULL and label is
// 0 when withdrawing the route.
//
void EvpnPrefix::BuildProtoPrefix(const BgpAttr *attr, uint32_t label,
    BgpProtoPrefix *proto_prefix) const {
    assert(attr != NULL || label == 0);
    proto_prefix->type = type_;
    proto_prefix->prefix.clear();

    switch (type_) {
    case AutoDiscoveryRoute: {
        size_t nlri_size = kMinAutoDiscoveryRouteSize + kLabelSize;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + kRdSize,
            proto_prefix->prefix.begin() + rd_offset);
        size_t esi_offset = rd_offset + kRdSize;
        copy(esi_.GetData(), esi_.GetData() + kEsiSize,
            proto_prefix->prefix.begin() + esi_offset);
        size_t tag_offset = esi_offset + kEsiSize;
        put_value(&proto_prefix->prefix[tag_offset], kTagSize, tag_);
        size_t label_offset = tag_offset + kTagSize;
        proto_prefix->WriteLabel(label_offset, label);
        break;
    }
    case MacAdvertisementRoute: {
        size_t ip_size = GetIpAddressSize();
        size_t nlri_size = kMinMacAdvertisementRouteSize + kLabelSize + ip_size;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + kRdSize,
            proto_prefix->prefix.begin() + rd_offset);
        size_t esi_offset = rd_offset + kRdSize;
        if (attr) {
            copy(attr->esi().GetData(), attr->esi().GetData() + kEsiSize,
                proto_prefix->prefix.begin() + esi_offset);
        }
        size_t tag_offset = esi_offset + kEsiSize;
        put_value(&proto_prefix->prefix[tag_offset], kTagSize, tag_);
        size_t mac_len_offset = tag_offset + kTagSize;
        proto_prefix->prefix[mac_len_offset] = 48;
        size_t mac_offset = mac_len_offset + 1;
        copy(mac_addr_.GetData(), mac_addr_.GetData() + kMacSize,
            proto_prefix->prefix.begin() + mac_offset);
        size_t ip_len_offset = mac_offset + kMacSize;
        proto_prefix->prefix[ip_len_offset] = ip_size * 8;
        size_t ip_offset = ip_len_offset + 1;
        WriteIpAddress(proto_prefix, ip_offset);
        size_t label_offset = ip_offset + ip_size;
        proto_prefix->WriteLabel(label_offset, label);
        break;
    }
    case InclusiveMulticastRoute: {
        size_t ip_size = GetIpAddressSize();
        size_t nlri_size = kMinInclusiveMulticastRouteSize + ip_size;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + kRdSize,
            proto_prefix->prefix.begin() + rd_offset);
        size_t tag_offset = rd_offset + kRdSize;
        put_value(&proto_prefix->prefix[tag_offset], kTagSize, tag_);
        size_t ip_len_offset = tag_offset + kTagSize;
        proto_prefix->prefix[ip_len_offset] = ip_size * 8;
        size_t ip_offset = ip_len_offset + 1;
        WriteIpAddress(proto_prefix, ip_offset);
        break;
    }
    case SegmentRoute: {
        size_t ip_size = GetIpAddressSize();
        size_t nlri_size = kMinSegmentRouteSize + ip_size;
        proto_prefix->prefixlen = nlri_size * 8;
        proto_prefix->prefix.resize(nlri_size, 0);

        size_t rd_offset = 0;
        copy(rd_.GetData(), rd_.GetData() + kRdSize,
            proto_prefix->prefix.begin() + rd_offset);
        size_t esi_offset = rd_offset + kRdSize;
        copy(esi_.GetData(), esi_.GetData() + kEsiSize,
            proto_prefix->prefix.begin() + esi_offset);
        size_t ip_len_offset = esi_offset + kEsiSize;
        proto_prefix->prefix[ip_len_offset] = ip_size * 8;
        size_t ip_offset = ip_len_offset + 1;
        WriteIpAddress(proto_prefix, ip_offset);
        break;
    }
    default: {
        assert(false);
        break;
    }
    }
}

int EvpnPrefix::CompareTo(const EvpnPrefix &rhs) const {
    KEY_COMPARE(type_, rhs.type_);

    switch (type_) {
    case AutoDiscoveryRoute:
        KEY_COMPARE(rd_, rhs.rd_);
        KEY_COMPARE(esi_, rhs.esi_);
        KEY_COMPARE(tag_, rhs.tag_);
        break;
    case MacAdvertisementRoute:
        KEY_COMPARE(rd_, rhs.rd_);
        KEY_COMPARE(tag_, rhs.tag_);
        KEY_COMPARE(mac_addr_, rhs.mac_addr_);
        KEY_COMPARE(ip_address_, rhs.ip_address_);
        break;
    case InclusiveMulticastRoute:
        KEY_COMPARE(rd_, rhs.rd_);
        KEY_COMPARE(tag_, rhs.tag_);
        KEY_COMPARE(ip_address_, rhs.ip_address_);
        break;
    case SegmentRoute:
        KEY_COMPARE(rd_, rhs.rd_);
        KEY_COMPARE(esi_, rhs.esi_);
        KEY_COMPARE(ip_address_, rhs.ip_address_);
        break;
    default:
        break;
    }

    return 0;
}

EvpnPrefix EvpnPrefix::FromString(const string &str,
    boost::system::error_code *errorp) {
    EvpnPrefix prefix;

    // Parse type.
    size_t pos1 = str.find('-');
    if (pos1 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return EvpnPrefix::kNullPrefix;
    }

    string type_str = str.substr(0, pos1);
    bool ret = stringToInteger(type_str, prefix.type_);
    if (!ret) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return EvpnPrefix::kNullPrefix;
    }

    if (prefix.type_ < EvpnPrefix::AutoDiscoveryRoute ||
        prefix.type_ > EvpnPrefix::SegmentRoute) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return EvpnPrefix::kNullPrefix;
    }

    // Parse RD.
    size_t pos2 = str.find('-', pos1 + 1);
    if (pos2 == string::npos) {
        if (errorp != NULL) {
            *errorp = make_error_code(boost::system::errc::invalid_argument);
        }
        return EvpnPrefix::kNullPrefix;
    }
    string rd_str = str.substr(pos1 + 1, pos2 - pos1 - 1);
    boost::system::error_code rd_err;
    prefix.rd_ = RouteDistinguisher::FromString(rd_str, &rd_err);
    if (rd_err != 0) {
        if (errorp != NULL) {
            *errorp = rd_err;
        }
        return EvpnPrefix::kNullPrefix;
    }

    switch (prefix.type_) {
    case AutoDiscoveryRoute: {

        // Parse ESI.
        size_t pos3 = str.find('-', pos2 + 1);
        if (pos3 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return EvpnPrefix::kNullPrefix;
        }
        string esi_str = str.substr(pos2 + 1, pos3 - pos2 - 1);
        boost::system::error_code esi_err;
        prefix.esi_ = EthernetSegmentId::FromString(esi_str, &esi_err);
        if (esi_err != 0) {
            if (errorp != NULL) {
                *errorp = esi_err;
            }
            return EvpnPrefix::kNullPrefix;
        }

        // Parse tag.
        string tag_str = str.substr(pos3 + 1, string::npos);
        bool ret = stringToInteger(tag_str, prefix.tag_);
        if (!ret) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return EvpnPrefix::kNullPrefix;
        }

        break;
    }

    case MacAdvertisementRoute: {

        // Parse tag.
        size_t pos3 = str.find('-', pos2 + 1);
        if (pos3 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return EvpnPrefix::kNullPrefix;
        }
        string tag_str = str.substr(pos2 + 1, pos3 - pos2 - 1);
        bool ret = stringToInteger(tag_str, prefix.tag_);
        if (!ret) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return EvpnPrefix::kNullPrefix;
        }

        // Parse MAC.
        size_t pos4 = str.rfind(',');
        string mac_str = str.substr(pos3 + 1, pos4 - pos3 -1);
        boost::system::error_code mac_err;
        prefix.mac_addr_ = MacAddress::FromString(mac_str, &mac_err);
        if (mac_err != 0) {
            if (errorp != NULL) {
                *errorp = mac_err;
            }
            return EvpnPrefix::kNullPrefix;
        }

        // Parse IP - treat all 0s as unspecified.
        string ip_str = str.substr(pos4 + 1, string::npos);
        boost::system::error_code ip_err;
        prefix.ip_address_ = IpAddress::from_string(ip_str, ip_err);
        if (ip_err != 0) {
            if (errorp != NULL) {
                *errorp = ip_err;
            }
            return EvpnPrefix::kNullPrefix;
        }
        if (prefix.ip_address_.is_v4() && !prefix.ip_address_.is_unspecified())
            prefix.family_ = Address::INET;
        if (prefix.ip_address_.is_v6() && !prefix.ip_address_.is_unspecified())
            prefix.family_ = Address::INET6;

        break;
    }

    case InclusiveMulticastRoute: {

        // Parse tag.
        size_t pos3 = str.find('-', pos2 + 1);
        if (pos3 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return EvpnPrefix::kNullPrefix;
        }
        string tag_str = str.substr(pos2 + 1, pos3 - pos2 - 1);
        bool ret = stringToInteger(tag_str, prefix.tag_);
        if (!ret) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return EvpnPrefix::kNullPrefix;
        }

        // Parse IP.
        string ip_str = str.substr(pos3 + 1, string::npos);
        boost::system::error_code ip_err;
        prefix.ip_address_ = IpAddress::from_string(ip_str, ip_err);
        if (ip_err != 0) {
            if (errorp != NULL) {
                *errorp = ip_err;
            }
            return EvpnPrefix::kNullPrefix;
        }
        prefix.family_ =
            prefix.ip_address_.is_v4() ? Address::INET : Address::INET6;

        break;
    }

    case SegmentRoute: {

        // Parse ESI.
        size_t pos3 = str.find('-', pos2 + 1);
        if (pos3 == string::npos) {
            if (errorp != NULL) {
                *errorp = make_error_code(boost::system::errc::invalid_argument);
            }
            return EvpnPrefix::kNullPrefix;
        }
        string esi_str = str.substr(pos2 + 1, pos3 - pos2 - 1);
        boost::system::error_code esi_err;
        prefix.esi_ = EthernetSegmentId::FromString(esi_str, &esi_err);
        if (esi_err != 0) {
            if (errorp != NULL) {
                *errorp = esi_err;
            }
            return EvpnPrefix::kNullPrefix;
        }

        // Parse IP.
        string ip_str = str.substr(pos3 + 1, string::npos);
        boost::system::error_code ip_err;
        prefix.ip_address_ = IpAddress::from_string(ip_str, ip_err);
        if (ip_err != 0) {
            if (errorp != NULL) {
                *errorp = ip_err;
            }
            return EvpnPrefix::kNullPrefix;
        }
        prefix.family_ =
            prefix.ip_address_.is_v4() ? Address::INET : Address::INET6;

        break;
    }
    }

    return prefix;
}

string EvpnPrefix::ToString() const {
    string str = integerToString(type_);
    str += "-" + rd_.ToString();
    switch (type_) {
    case AutoDiscoveryRoute:
        str += "-" + esi_.ToString();
        str += "-" + integerToString(tag_);
        break;
    case MacAdvertisementRoute:
        str += "-" + integerToString(tag_);
        str += "-" + mac_addr_.ToString();
        str += "," + ip_address_.to_string();
        break;
    case InclusiveMulticastRoute:
        str += "-" + integerToString(tag_);
        str += "-" + ip_address_.to_string();
        break;
    case SegmentRoute:
        str += "-" + esi_.ToString();
        str += "-" + ip_address_.to_string();
        break;
    default:
        break;
    }

    return str;
}

string EvpnPrefix::ToXmppIdString() const {
    string str;
    if (tag_ != 0)
        str += integerToString(tag_) + "-";
    str += mac_addr_.ToString();
    str += "," + ip_address_.to_string() + "/" +
        integerToString(ip_address_length());
    return str;
}

uint8_t EvpnPrefix::ip_address_length() const {
    if (family_ == Address::INET)
        return 32;
    if (family_ == Address::INET6)
        return 128;
    return 32;
}

size_t EvpnPrefix::GetIpAddressSize() const {
    if (family_ == Address::INET)
        return 4;
    if (family_ == Address::INET6)
        return 16;
    return 0;
}

void EvpnPrefix::ReadIpAddress(const BgpProtoPrefix &proto_prefix,
    size_t ip_offset, size_t ip_size) {
    if (ip_size == 0) {
        family_ = Address::UNSPEC;
    } else if (ip_size == 4) {
        family_ = Address::INET;
        Ip4Address::bytes_type bytes;
        copy(proto_prefix.prefix.begin() + ip_offset,
            proto_prefix.prefix.begin() + ip_offset + ip_size, bytes.begin());
        ip_address_ = Ip4Address(bytes);
    } else if (ip_size == 16) {
        family_ = Address::INET6;
        Ip6Address::bytes_type bytes;
        copy(proto_prefix.prefix.begin() + ip_offset,
            proto_prefix.prefix.begin() + ip_offset + ip_size, bytes.begin());
        ip_address_ = Ip6Address(bytes);
    }
}

void EvpnPrefix::WriteIpAddress(BgpProtoPrefix *proto_prefix,
    size_t ip_offset) const {
    if (family_ == Address::INET) {
        const Ip4Address::bytes_type &bytes = ip_address_.to_v4().to_bytes();
        copy(bytes.begin(), bytes.begin() + 4,
            proto_prefix->prefix.begin() + ip_offset);
    } else if (family_ == Address::INET6) {
        const Ip6Address::bytes_type &bytes = ip_address_.to_v6().to_bytes();
        copy(bytes.begin(), bytes.begin() + 16,
            proto_prefix->prefix.begin() + ip_offset);
    }
}

EvpnRoute::EvpnRoute(const EvpnPrefix &prefix)
    : prefix_(prefix) {
}

int EvpnRoute::CompareTo(const Route &rhs) const {
    const EvpnRoute &evpn_rhs = static_cast<const EvpnRoute &>(rhs);
    return prefix_.CompareTo(evpn_rhs.prefix_);
}

string EvpnRoute::ToString() const {
    return prefix_.ToString();
}

string EvpnRoute::ToXmppIdString() const {
    return prefix_.ToXmppIdString();
}

bool EvpnRoute::IsValid() const {
    if (!BgpRoute::IsValid())
        return false;

    const BgpAttr *attr = BestPath()->GetAttr();
    switch (prefix_.type()) {
    case EvpnPrefix::AutoDiscoveryRoute: {
        return false;
    }
    case EvpnPrefix::MacAdvertisementRoute: {
        return prefix_.mac_addr().IsBroadcast();
    }
    case EvpnPrefix::InclusiveMulticastRoute: {
        const PmsiTunnel *pmsi_tunnel = attr->pmsi_tunnel();
        if (!pmsi_tunnel)
            return false;
        if (pmsi_tunnel->tunnel_type != PmsiTunnelSpec::IngressReplication)
            return false;
        return true;
    }
    case EvpnPrefix::SegmentRoute: {
        return false;
    }
    default: {
        break;
    }
    }

    return false;
}

void EvpnRoute::SetKey(const DBRequestKey *reqkey) {
    const EvpnTable::RequestKey *key =
        static_cast<const EvpnTable::RequestKey *>(reqkey);
    prefix_ = key->prefix;
}

void EvpnRoute::BuildProtoPrefix(BgpProtoPrefix *proto_prefix,
    const BgpAttr *attr, uint32_t label) const {
    prefix_.BuildProtoPrefix(attr, label, proto_prefix);
}

void EvpnRoute::BuildBgpProtoNextHop(vector<uint8_t> &nh,
        IpAddress nexthop) const {
    nh.resize(4);
    const Ip4Address::bytes_type &addr_bytes = nexthop.to_v4().to_bytes();
    std::copy(addr_bytes.begin(), addr_bytes.end(), nh.begin());
}

DBEntryBase::KeyPtr EvpnRoute::GetDBRequestKey() const {
    EvpnTable::RequestKey *key = new EvpnTable::RequestKey(GetPrefix(), NULL);
    return KeyPtr(key);
}
