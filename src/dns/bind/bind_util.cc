/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#include <boost/assign/list_of.hpp>
#include <bind/bind_util.h>

using namespace boost::assign;
SandeshTraceBufferPtr DnsBindTraceBuf(SandeshTraceBufferCreate("DnsBind", 2000));

DnsTypeMap g_dns_type_map = map_list_of<std::string, uint16_t>
                                ("A", 1)
                                ("NS", 2)
                                ("CNAME", 5)
                                ("SOA", 6)
                                ("PTR", 0x0C)
                                ("TXT", 0x10)
                                ("AAAA", 0x1C)
                                ("ANY", 0xFF);

DnsTypeNumMap g_dns_type_num_map = map_list_of<uint16_t, std::string>
                                (DNS_A_RECORD, "A")
                                (DNS_NS_RECORD, "NS")
                                (DNS_CNAME_RECORD, "CNAME")
                                (DNS_TYPE_SOA, "SOA")
                                (DNS_PTR_RECORD, "PTR")
                                (DNS_TXT_RECORD, "TXT")
                                (DNS_AAAA_RECORD, "AAAA")
                                (DNS_TYPE_ANY, "ANY");

DnsTypeNumMap g_dns_class_num_map = map_list_of<uint16_t, std::string>
                                (DNS_CLASS_IN, "IN")
                                (DNS_CLASS_NONE, "None")
                                (DNS_CLASS_ANY, "Any");

DnsResponseMap g_dns_response_map = map_list_of<uint16_t, std::string>
                                (0, "No error")
                                (1, "Format error")
                                (2, "Server failure")
                                (3, "Non-existent domain")
                                (4, "Not implemented")
                                (5, "Query refused")
                                (6, "Name exists when it should not")
                                (7, "RR Set Exists when it should not")
                                (8, "RR Set that should exist does not")
                                (9, "Not Authorized")
                                (10, "Name not contained in zone")
                                (16, "Bad OPT Version")
                                (17, "Key not recognized")
                                (18, "Signature out of time window")
                                (19, "Bad TKEY Mode")
                                (20, "Duplicate key name")
                                (21, "Algorithm not supported")
                                (22, "Bad truncation")
                                (4095, "Invalid response code");

std::string DnsItem::ToString() const {
    return BindUtil::DnsClass(eclass) + "/" +
           BindUtil::DnsType(type) + "/" + name + "/" + data + ";";
}   

uint16_t BindUtil::DnsClass(const std::string &cl) {
    if (cl == "IN")
        return DNS_CLASS_IN;
    return DNS_CLASS_ANY;
}

std::string BindUtil::DnsClass(uint16_t cl) {
    DnsTypeNumIter iter = g_dns_class_num_map.find(cl);
    if (iter == g_dns_type_num_map.end())
        return "";
    return iter->second;
}

uint16_t BindUtil::DnsType(const std::string &tp) {
    DnsTypeIter iter = g_dns_type_map.find(tp);
    if (iter == g_dns_type_map.end())
        return -1;
    return iter->second;
}

std::string BindUtil::DnsType(uint16_t tp) {
    DnsTypeNumIter iter = g_dns_type_num_map.find(tp);
    if (iter == g_dns_type_num_map.end())
        return "";
    return iter->second;
}

const std::string &BindUtil::DnsResponseCode(uint16_t code) {
    DnsResponseIter iter = g_dns_response_map.find(code);
    if (iter == g_dns_response_map.end())
        return DnsResponseCode(4095);
    return iter->second;
}

uint8_t *BindUtil::AddName(uint8_t *ptr, const std::string &addr, 
                           uint16_t plen, uint16_t offset, uint16_t &length) {
    std::size_t size = addr.size();
    std::size_t cur_pos = 0;
    std::size_t prev_pos = 0;
    while (cur_pos < size && cur_pos != std::string::npos) {
        if (offset && !plen) {
            ptr = WriteShort(ptr, offset);
            length += 2;
            return ptr;
        }
        std::size_t len;
        cur_pos = addr.find('.', prev_pos);
        if (cur_pos == std::string::npos)
            len = size - prev_pos;
        else
            len = cur_pos - prev_pos;

        *ptr = len;
        memcpy(ptr + 1, addr.substr(prev_pos, len).data(), len);
        ptr += len + 1;
        plen = (plen > len) ? plen - len - 1 : 0;

        prev_pos = cur_pos + 1;
        length += len + 1;
    }
    ptr = WriteByte(ptr, 0);
    length++;

    return ptr;
}

uint8_t *BindUtil::AddQuestionSection(uint8_t *ptr, const std::string &name, 
                                      uint16_t type, uint16_t cl, 
                                      uint16_t &length) {
    ptr = AddName(ptr, name, 0, 0, length);
    ptr = WriteShort(ptr, type);
    ptr = WriteShort(ptr, cl);
    length += 4;

    return ptr;
}

uint8_t *BindUtil::AddData(uint8_t *ptr, const DnsItem &item, 
                           uint16_t &length) {
    boost::system::error_code ec;

    if (item.type == DNS_A_RECORD) {
        ptr = WriteShort(ptr, 4);
        ptr = WriteWord(ptr, boost::asio::ip::address_v4::from_string(
                             item.data, ec).to_ulong());
        length += 2 + 4;
    } else if (item.type == DNS_AAAA_RECORD) {
        ptr = WriteShort(ptr, 16);
        boost::asio::ip::address_v6 addr = 
                boost::asio::ip::address_v6::from_string(item.data, ec);
        if (ec.value()) {
            memset(ptr, 0, 16);
        } else {
            memcpy(ptr, addr.to_bytes().data(), 16);
        }
        ptr += 16;
        length += 2 + 16;
    } else if(item.type == DNS_TYPE_SOA) {
        uint16_t data_len = DataLength(item.soa.ns_plen, item.soa.ns_offset,
                                       item.soa.primary_ns.size()) +
                            DataLength(item.soa.mailbox_plen, 
                                       item.soa.mailbox_offset,
                                       item.soa.mailbox.size()) + 20;
        ptr = WriteShort(ptr, data_len);
        ptr = AddName(ptr, item.soa.primary_ns, item.soa.ns_plen,
                      item.soa.ns_offset, length);
        ptr = AddName(ptr, item.soa.mailbox, item.soa.mailbox_plen,
                      item.soa.mailbox_offset, length);
        ptr = WriteWord(ptr, item.soa.serial);
        ptr = WriteWord(ptr, item.soa.refresh);
        ptr = WriteWord(ptr, item.soa.retry);
        ptr = WriteWord(ptr, item.soa.expiry);
        ptr = WriteWord(ptr, item.soa.ttl);
        length += 2 + 20;
    } else {
        uint16_t data_len = DataLength(item.data_plen, item.data_offset,
                                       item.data.size());
        ptr = WriteShort(ptr, data_len);
        ptr = AddName(ptr, item.data, item.data_plen, item.data_offset, length);
        length += 2;
    }

    return ptr;
}

uint8_t *BindUtil::AddAnswerSection(uint8_t *ptr, const DnsItem &item, 
                                    uint16_t &length) {
    ptr = AddName(ptr, item.name, item.name_plen, item.name_offset, length);
    ptr = WriteShort(ptr, item.type);
    ptr = WriteShort(ptr, item.eclass);
    ptr = WriteWord(ptr, item.ttl);
    length += 2 + 2 + 4;
    ptr = AddData(ptr, item, length);

    return ptr;
}

uint8_t *BindUtil::AddAdditionalSection(uint8_t *ptr, const std::string name, 
                                        uint16_t type, uint16_t cl, 
                                        uint32_t ttl, const std::string &data, 
                                        uint16_t &length) {
    ptr = AddQuestionSection(ptr, name, type, cl, length);
    ptr = WriteWord(ptr, ttl);
    ptr = WriteShort(ptr, data.size() + 1);

    // adding the domain name as it is, without replacing '.' with length
    ptr = WriteByte(ptr, data.size());
    memcpy(ptr, data.data(), data.size());
    ptr += data.size();
    length += 4 + 2 + 1 + data.size();

    return ptr;
}

uint8_t *BindUtil::AddUpdate(uint8_t *ptr, const DnsItem &item,
                             uint16_t cl, uint32_t ttl, uint16_t &length) {
    ptr = AddQuestionSection(ptr, item.name, item.type, cl, length);
    ptr = WriteWord(ptr, ttl);
    length += 4;
    ptr = AddData(ptr, item, length);

    return ptr;
}

uint8_t *BindUtil::ReadName(uint8_t *dns, uint8_t *ptr, std::string &name,
                            uint16_t &plen, uint16_t &offset) {
    std::size_t len = *ptr;
    while (len) {
        if (len & 0xC0) {
            uint16_t dummy;
            plen = name.size() ? name.size() - 1 : 0;
            ptr = ReadShort(ptr, offset);
            ReadName(dns, dns + (offset & ~0xC000), name, dummy, dummy);
            return ptr;
        } else {
            name.append((char *)ptr+1, len);
            ptr += len + 1;
            len = *ptr;
            if (len)
                name.append(".");
        }
    }
    ptr++;

    return ptr;
}

uint8_t *BindUtil::ReadData(uint8_t *buf, uint8_t *ptr, DnsItem &item) {
    boost::system::error_code ec;
    if (item.type == DNS_A_RECORD) {
        uint32_t ip;
        ptr = ReadWord(ptr, ip);
        boost::asio::ip::address_v4 addr(ip);
        item.data = addr.to_string(ec);
    } else if(item.type == DNS_AAAA_RECORD) {
        boost::asio::ip::address_v6::bytes_type ip;
        memcpy(&ip[0], ptr, 16);
        boost::asio::ip::address_v6 addr(ip);
        item.data = addr.to_string(ec);
        ptr += 16;
    } else if(item.type == DNS_TYPE_SOA) {
        ptr = ReadName(buf, ptr, item.soa.primary_ns,
                       item.soa.ns_plen, item.soa.ns_offset);
        ptr = ReadName(buf, ptr, item.soa.mailbox,
                       item.soa.mailbox_plen, item.soa.mailbox_offset);
        ptr = ReadWord(ptr, item.soa.serial);
        ptr = ReadWord(ptr, item.soa.refresh);
        ptr = ReadWord(ptr, item.soa.retry);
        ptr = ReadWord(ptr, item.soa.expiry);
        ptr = ReadWord(ptr, item.soa.ttl);
    } else
        ptr = ReadName(buf, ptr, item.data, item.data_plen, item.data_offset);

    return ptr;
}

uint8_t *BindUtil::ReadQuestionEntry(uint8_t *buf, uint8_t *ptr, 
                                     DnsItem &item) {
    ptr = ReadName(buf, ptr, item.name, item.name_plen, item.name_offset);
    ptr = ReadShort(ptr, item.type);
    ptr = ReadShort(ptr, item.eclass);

    return ptr;
}

uint8_t *BindUtil::ReadAnswerEntry(uint8_t *buf, uint8_t *ptr, DnsItem &item) {
    ptr = ReadQuestionEntry(buf, ptr, item);
    ptr = ReadWord(ptr, item.ttl);
    uint16_t length;
    ptr = ReadShort(ptr, length);
    ptr = ReadData(buf, ptr, item);

    return ptr;
}

int BindUtil::ParseDnsQuery(uint8_t *buf, DnsItems &items) {
    dnshdr *dns = (dnshdr *) buf;
    uint16_t ques_rrcount = ntohs(dns->ques_rrcount);

    uint8_t *ptr = (uint8_t *) (dns + 1);
    for (unsigned int i = 0; i < ques_rrcount; ++i) {
        DnsItem item;
        item.offset = (ptr - buf) | 0xC000;
        ptr = ReadQuestionEntry(buf, ptr, item);
        items.push_back(item);
    }

    return ptr - buf;
}

void BindUtil::ParseDnsQuery(uint8_t *buf, uint16_t &xid, dns_flags &flags,
                             DnsItems &ques, DnsItems &ans,
                             DnsItems &auth, DnsItems &add) {
    dnshdr *dns = (dnshdr *) buf;
    xid = ntohs(dns->xid);
    flags = dns->flags;

    uint16_t ques_rrcount = ntohs(dns->ques_rrcount);
    uint16_t ans_rrcount = ntohs(dns->ans_rrcount);
    uint16_t auth_rrcount = ntohs(dns->auth_rrcount);
    uint16_t add_rrcount = ntohs(dns->add_rrcount);

    // question section
    uint8_t *ptr = (uint8_t *) (dns + 1);
    for (unsigned int i = 0; i < ques_rrcount; ++i) {
        DnsItem item;
        ptr = ReadQuestionEntry(buf, ptr, item);
        ques.push_back(item);
    }

    // answer section
    for (unsigned int i = 0; i < ans_rrcount; ++i) {
        DnsItem item;
        ptr = ReadAnswerEntry(buf, ptr, item);
        ans.push_back(item);
    }

    // authority section
    for (unsigned int i = 0; i < auth_rrcount; ++i) {
        DnsItem item;
        ptr = ReadAnswerEntry(buf, ptr, item);
        auth.push_back(item);
    }

    // additional section
    for (unsigned int i = 0; i < add_rrcount; ++i) {
        DnsItem item;
        ptr = ReadAnswerEntry(buf, ptr, item);
        add.push_back(item);
    }
}

int BindUtil::ParseDnsUpdate(uint8_t *buf, DnsUpdateData &data) {
    dnshdr *dns = (dnshdr *) buf;
    uint16_t zone_count = ntohs(dns->ques_rrcount);
    uint16_t prereq_count = ntohs(dns->ans_rrcount);
    uint16_t update_count = ntohs(dns->auth_rrcount);

    if (zone_count != 1) {
        DNS_BIND_TRACE(DnsBindError, 
                       "Invalid zone count in Update request : " << zone_count);
        return 0;
    }

    if (prereq_count != 0) {
        // Not supporting pre-requisites now
        DNS_BIND_TRACE(DnsBindError, 
                       "Update has pre-requisites; dropping the request");
        return 0;
    }

    uint8_t *ptr = (uint8_t *) (dns + 1);
    // Read zone
    uint16_t zone_type, zone_class, plen, offset;
    ptr = ReadName(buf, ptr, data.zone, plen, offset);
    ptr = ReadShort(ptr, zone_type);
    ptr = ReadShort(ptr, zone_class);

    // Read Updates
    for (unsigned int i = 0; i < update_count; ++i) {
        DnsItem item;
        ptr = ReadName(buf, ptr, item.name, plen, offset);
        ptr = ReadShort(ptr, item.type);
        ptr = ReadShort(ptr, item.eclass);
        ptr = ReadWord(ptr, item.ttl);
        ptr = ReadData(buf, ptr, item);
        data.items.push_back(item);
    }

    return ptr - buf;
}

void BindUtil::BuildDnsHeader(dnshdr *dns, uint16_t xid, DnsReq req, 
                              DnsOpcode op, bool rd, bool ra, uint8_t ret,
                              uint16_t ques_count) {
    dns->xid = htons(xid);
    dns->flags.req = req;
    dns->flags.op = op;
    dns->flags.auth = 0;
    dns->flags.trunc = 0;
    dns->flags.rd = rd;
    dns->flags.ra = ra;
    dns->flags.res = 0;
    dns->flags.ad = 0;
    dns->flags.cd = 0;
    dns->flags.ret = ret;
    dns->ques_rrcount = htons(ques_count);
    dns->ans_rrcount = 0;
    dns->auth_rrcount = 0;
    dns->add_rrcount = 0;
}

int BindUtil::BuildDnsQuery(uint8_t *buf, uint16_t xid, 
                            const std::string &domain,
                            const DnsItems &items) {
    dnshdr *dns = (dnshdr *) buf;
    BuildDnsHeader(dns, xid, DNS_QUERY_REQUEST, DNS_OPCODE_QUERY, 
                   1, 0, 0, items.size());
    dns->add_rrcount = htons(1);

    // TODO : can be optimised to reuse any names using offsets
    uint16_t len = sizeof(dnshdr);
    uint8_t *ques = (uint8_t *) (dns + 1);
    for (DnsItems::const_iterator it = items.begin(); it != items.end(); ++it) {
        ques = AddQuestionSection(ques, (*it).name, (*it).type,
                                  (*it).eclass, len);
    }

    std::string view = "view=" + domain;
    ques = AddAdditionalSection(ques, "view", DNS_TXT_RECORD, DNS_CLASS_IN, 
                                0, view, len);

    return len;
}

int BindUtil::BuildDnsUpdate(uint8_t *buf, Operation op, uint16_t xid, 
                             const std::string &domain, 
                             const std::string &zone, 
                             const DnsItems &items) {
    dnshdr *dns = (dnshdr *) buf;
    BuildDnsHeader(dns, xid, DNS_QUERY_REQUEST, DNS_OPCODE_UPDATE, 0, 0, 0, 1);
    // dns->ques_rrcount = htons(1);          // Number of zones
    // dns->ans_rrcount = 0;                  // Number of Prerequisites
    dns->auth_rrcount = htons(items.size());  // Number of Updates
    dns->add_rrcount = htons(1);              // Number of Additional RRs

    // Add zone
    uint16_t len = sizeof(dnshdr);
    uint8_t *ptr = (uint8_t *) (dns + 1);
    ptr = AddQuestionSection(ptr, zone, DNS_TYPE_SOA, DNS_CLASS_IN, len);

    // Add Updates
    switch (op) {
        case ADD_UPDATE:
        case CHANGE_UPDATE:
            for (DnsItems::const_iterator it = items.begin(); 
                 it != items.end(); ++it) {
                ptr = AddUpdate(ptr, *it, (*it).eclass, (*it).ttl, len);
            }
            break;

        case DELETE_UPDATE:
            for (DnsItems::const_iterator it = items.begin(); 
                 it != items.end(); ++it) {
                ptr = AddUpdate(ptr, *it, DNS_CLASS_NONE, 0, len);
            }
            break;

        default:
            assert(0);
    }

    // Add Additional RRs
    std::string view = "view=" + domain;
    ptr = AddAdditionalSection(ptr, "view", DNS_TXT_RECORD, DNS_CLASS_IN, 
                                0, view, len);

    return len;
}

bool BindUtil::IsIPv4(std::string name, uint32_t &addr) {
    boost::system::error_code ec; 
    boost::asio::ip::address_v4 address(boost::asio::ip::address_v4::
                                        from_string(name, ec));
    if (!ec.value()) {
        addr = address.to_ulong();
        return true;
    }
    return false;
}

// Get list of reverse zones, given a subnet
void BindUtil::GetReverseZones(const Subnet &subnet, ZoneList &zones) {
    std::string zone_name = "in-addr.arpa.";
    uint32_t plen = subnet.plen;
    uint32_t addr = subnet.prefix.to_ulong();
    while (plen >= 8) {
        std::stringstream str;
        str << (addr >> 24);
        zone_name = str.str() + "." + zone_name;
        addr = addr << 8;
        plen -= 8;
    }
    if (plen == 0) {
        // when prefix len is a multiple of 8, add only one reverse zone
        zones.push_back(zone_name);
        return;
    }
    for (int j = 0; j <= (0xFF >> plen); j++) {
        // when prefix len is not a multiple of 8, add reverse zones for all 
        // addresses upto the nearest 8 byte boundary for the prefix len. For
        // example, a /22 prefix results in 4 reverse zones.
        uint32_t last = (addr >> 24) + j;
        std::stringstream str;
        str << last;
        std::string rev_zone_name = str.str() + "." + zone_name;
        zones.push_back(rev_zone_name);
    }
}

void BindUtil::GetReverseZone(uint32_t addr, uint32_t plen, std::string &zone) {
    zone = "in-addr.arpa";
    while (plen >= 8) {
        std::stringstream str;
        str << (addr >> 24);
        zone = str.str() + "." + zone;
        addr = addr << 8;
        plen -= 8;
    }
    if (plen == 0) {
        return;
    }
    // when prefix len is not a multiple of 8, add extra byte from the addr
    std::stringstream str;
    str << (addr >> 24);
    zone = str.str() + "." + zone;
}

bool BindUtil::GetAddrFromPtrName(std::string &ptr_name, uint32_t &mask) {
    std::string name = boost::to_lower_copy(ptr_name);
    std::size_t pos = name.find(".in-addr.arpa");
    if (pos == std::string::npos)
        return false;

    mask = 0;
    uint8_t dot;
    uint32_t num;
    std::stringstream str(name);
    if (str.peek() == '.')
        str >> dot;
    int count = 0;
    while (count < 4) {
        if (str.peek() == 'i')
            break;
        str >> num;
        str >> dot;
        mask |= (num << (8 * count));
        count++;
    }
    while (count++ < 4)
        mask = mask << 8;
    return true;
}

std::string BindUtil::GetFQDN(const std::string &name, const std::string &domain,
                              const std::string &match) {
    if (name.find(match, 0) == std::string::npos)
        return name + "." + domain;
    else
        return name;
}

bool BindUtil::HasSpecialChars(const std::string &name) {
    for (unsigned int i = 0; i < name.size(); ++i) {
        uint8_t c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || (c == '-') || (c == '.')))
            return true;
    }
    return false;
}

void BindUtil::RemoveSpecialChars(std::string &name) {
    for (unsigned int i = 0; i < name.size(); ++i) {
        uint8_t c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || (c == '-') || (c == '.')))
            name[i] = '-';
    }
}

void DnsNameEncoder::AddName(std::string &name, uint16_t curr_msg_offset, 
                             uint16_t &name_plen, uint16_t &name_offset) {
    name_plen = 0;
    name_offset = 0;
    std::size_t pos = 0;
    while (pos < name.size()) {
        std::string str = name.substr(pos);
        if(IsPresent(str, name_offset)) {
            name_plen = pos ? pos - 1 : 0;
            break;
        }

        pos = name.find('.', pos + 1);
        if (pos == std::string::npos)
            break;
        pos++;
    }
    if (pos > 0)
        names_.push_back(Name(name, curr_msg_offset));
}

bool DnsNameEncoder::IsPresent(std::string &name, uint16_t &name_offset) {
    for (unsigned int i = 0; i < names_.size(); i++) {
        if (names_[i].name == name) {
            name_offset = names_[i].offset;
            return true;
        }
    }
    return false;
}
