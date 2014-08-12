/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef __bind_util_h__
#define __bind_util_h__

#include <iostream>
#include <stdint.h>
#include <vector>
#include <boost/asio.hpp>

#include "sandesh/sandesh_types.h"
#include "sandesh/sandesh.h"
#include "sandesh/sandesh_trace.h"
#include "bind/bind_types.h"
#include "bind/xmpp_dns_agent.h"

extern SandeshTraceBufferPtr DnsBindTraceBuf;
#define DNS_BIND_TRACE(obj, arg)                                              \
do {                                                                          \
    std::ostringstream _str;                                                  \
    _str << arg;                                                              \
    obj::TraceMsg(DnsBindTraceBuf, __FILE__, __LINE__, _str.str());           \
} while (false)                                                               \

#define DNS_SERVER_PORT 53

// DNS Class
#define DNS_CLASS_IN   1
#define DNS_CLASS_ANY  0x00ff
#define DNS_CLASS_NONE 0x00fe

// DNS record types
#define DNS_A_RECORD 1
#define DNS_NS_RECORD 2
#define DNS_AAAA_RECORD 0x1C
#define DNS_PTR_RECORD 0x0C
#define DNS_CNAME_RECORD 0x05
#define DNS_TXT_RECORD 0x10
#define DNS_TYPE_SOA 0x06
#define DNS_TYPE_ANY 0x00ff

// DNS return codes
#define DNS_ERR_NO_ERROR     0
#define DNS_ERR_FORMAT_ERROR 1
#define DNS_ERR_SERVER_FAIL  2
#define DNS_ERR_NO_SUCH_NAME 3
#define DNS_ERR_NO_IMPLEMENT 4
#define DNS_ERR_NOT_AUTH     9

enum DnsReq {
    DNS_QUERY_REQUEST = 0x0,
    DNS_QUERY_RESPONSE = 0x1,
};

enum DnsOpcode {
    DNS_OPCODE_QUERY = 0x0,
    DNS_OPCODE_STATUS = 0x2,
    DNS_OPCODE_NOTIFY = 0x04,
    DNS_OPCODE_UPDATE = 0x05,
};

typedef std::map<std::string, uint16_t> DnsTypeMap;
typedef std::map<std::string, uint16_t>::const_iterator DnsTypeIter;
typedef std::map<uint16_t, std::string> DnsTypeNumMap;
typedef std::map<uint16_t, std::string>::const_iterator DnsTypeNumIter;
typedef std::map<uint16_t, std::string> DnsResponseMap;
typedef std::map<uint16_t, std::string>::const_iterator DnsResponseIter;

struct dns_flags {
#if __BYTE_ORDER == __LITTLE_ENDIAN
    uint8_t rd:1;          // recursion desired
    uint8_t trunc:1;       // truncated
    uint8_t auth:1;        // authoritative answer
    uint8_t op:4;          // opcode
    uint8_t req:1;         // request / response
    uint8_t ret:4;         // return code
    uint8_t cd:1;          // checking disabled 
    uint8_t ad:1;          // answer authenticated
    uint8_t res:1;         // reserved
    uint8_t ra:1;          // recursion available
#elif __BYTE_ORDER == __BIG_ENDIAN
    uint8_t req:1;
    uint8_t op:4;
    uint8_t auth:1;
    uint8_t trunc:1;
    uint8_t rd:1;
    uint8_t ra:1;
    uint8_t res:1;
    uint8_t ad:1;
    uint8_t cd:1;
    uint8_t ret:4;
#else
#error "Adjust your <bits/endian.h> defines"
#endif
};

struct dnshdr {
    uint16_t xid;
    dns_flags flags;
    uint16_t ques_rrcount; // question RR count
    uint16_t ans_rrcount;  // answer RR count
    uint16_t auth_rrcount; // authority RR count
    uint16_t add_rrcount;  // additional RR count
};

// Data format in an SOA record
struct DnsSOAData {
    std::string primary_ns; // primary name server
    std::string mailbox;    // responsible authority's mailbox
    uint16_t ns_plen;       // length of the prefix in primary_ns that is unique
    uint16_t ns_offset;     // offset from where rest of primary_ns name exists
    uint16_t mailbox_plen;
    uint16_t mailbox_offset;
    uint32_t serial;        // serial number
    uint32_t refresh;       // refresh interval in seconds
    uint32_t retry;         // retry interval in seconds
    uint32_t expiry;        // expiration limit in seconds
    uint32_t ttl;           // minimum ttl in seconds

    DnsSOAData() : ns_plen(0), ns_offset(0), mailbox_plen(0), mailbox_offset(0),
                   serial(0), refresh(0), retry(0), expiry(0), ttl(0) {}
    bool operator ==(const DnsSOAData &rhs) const {
        if (primary_ns == rhs.primary_ns && mailbox == rhs.mailbox && 
            serial == rhs.serial && refresh == rhs.refresh && 
            retry == rhs.retry && expiry == rhs.expiry && ttl == rhs.ttl)
            return true;
        return false;
    }
};

struct DnsItem {
    uint16_t eclass;
    uint16_t type;
    uint32_t ttl;
    uint16_t priority;
    uint16_t offset;      // offset of the name in the read request from the VM
    uint16_t name_plen;   // length of the prefix in name that is unique
    uint16_t name_offset; // offset from where rest of name exists
    uint16_t data_plen;   // length of the prefix in data that is unique
    uint16_t data_offset; // offset from where rest of data exists
    std::string name;
    std::string data;
    DnsSOAData soa;

    DnsItem() : eclass(1), type(0), ttl(0), priority(0), offset(0),
    name_plen(0), name_offset(0), data_plen(0), data_offset(0), soa() {}

    std::string ToString() const;

    bool operator ==(const DnsItem &rhs) const {
        if (eclass == rhs.eclass && type == rhs.type && 
            name == rhs.name && data == rhs.data && soa == rhs.soa)
            return true;
        return false;
    }

    bool IsDelete() const {
        if (ttl == 0 && (eclass == DNS_CLASS_ANY || eclass == DNS_CLASS_NONE))
            return true;
        return false;
    }

    bool MatchDelete(const DnsItem &rhs) const {
        if ((rhs.eclass == DNS_CLASS_ANY || rhs.eclass == DNS_CLASS_NONE) &&
            (rhs.type == DNS_TYPE_ANY || type == rhs.type) &&
            (name == rhs.name) &&
            (rhs.data.size() == 0 || data == rhs.data))
            return true;
        return false;
    }
};

typedef std::list<DnsItem> DnsItems;
static inline std::string DnsItemsToString(DnsItems &items) {
    std::string str;
    for (DnsItems::iterator it = items.begin();
         !items.empty() && it != items.end(); ++it) {
        str += (*it).ToString();
    }
    return str;
}

struct DnsUpdateData {
    typedef boost::function<void(DnsItem &)> DeleteCallback;

    std::string virtual_dns;
    std::string zone;
    mutable DnsItems items;

    DnsUpdateData() {}
    DnsUpdateData(const std::string &vdns, const std::string &z) 
                : virtual_dns(vdns), zone(z) {}

    struct Compare {
        bool operator() (DnsUpdateData *const &lhs, DnsUpdateData *const &rhs) {
            if (!lhs || !rhs)
                return false;
            if (lhs->virtual_dns != rhs->virtual_dns)
                return lhs->virtual_dns < rhs->virtual_dns;
            return lhs->zone < rhs->zone;
        }
    };

    bool AddItem(DnsItem &item, bool replace = false) const {
        for (DnsItems::iterator it = items.begin(); it != items.end(); ++it) {
            if (item == *it) {
                if (replace)
                    *it = item;
                return false;
            }
        }
        items.push_back(item);
        return true;
    }

    bool DelItem(DnsItem &item) const {
        bool change = false;
        for (DnsItems::iterator it = items.begin(); it != items.end();) {
            if ((*it).MatchDelete(item)) {
                items.erase(it++);
                change = true;
            } else {
                ++it;
            }
        }
        return change;
    }
};  

struct Subnet {
    boost::asio::ip::address_v4 prefix;
    uint32_t plen;
    uint8_t flags;

    enum DnsConfigFlags {
        DeleteMarked = 1 << 0,
    };  

    Subnet() : plen(0), flags(0) {}
    Subnet(std::string addr, uint32_t len) : plen(len), flags(0) {
        boost::system::error_code ec;
        prefix = boost::asio::ip::address_v4::from_string(addr, ec);
    }

    bool operator< (const Subnet &rhs) const {
        if (prefix != rhs.prefix) {
            return (prefix < rhs.prefix);
        }
        return plen < rhs.plen;
    }

    void MarkDelete() { flags |= DeleteMarked; }
    bool IsDeleted() const { return (flags & DeleteMarked); }
    void ClearDelete() { flags &= ~DeleteMarked; }

    std::string ToString() const { return prefix.to_string(); }
};

typedef std::vector<Subnet> Subnets;
typedef std::vector<std::string> ZoneList;

class BindUtil {
public:
    enum Operation {
        ADD_UPDATE,
        CHANGE_UPDATE,
        DELETE_UPDATE
    };

    static uint16_t DnsClass(const std::string &cl);
    static std::string DnsClass(uint16_t cl);
    static uint16_t DnsType(const std::string &tp);
    static std::string DnsType(uint16_t tp);
    static const std::string &DnsResponseCode(uint16_t code);
    static uint8_t *AddName(uint8_t *ptr, const std::string &addr,
                            uint16_t plen, uint16_t offset, uint16_t &length);
    static int ParseDnsQuery(uint8_t *buf, DnsItems &items);
    static void ParseDnsQuery(uint8_t *buf, uint16_t &xid, dns_flags &flags,
                              DnsItems &ques, DnsItems &ans,
                              DnsItems &auth, DnsItems &add);
    static int ParseDnsUpdate(uint8_t *buf, DnsUpdateData &data);
    static int BuildDnsQuery(uint8_t *buf, uint16_t xid, 
                             const std::string &domain,
                             const DnsItems &items);
    static int BuildDnsUpdate(uint8_t *buf, Operation op, uint16_t xid, 
                              const std::string &domain, 
                              const std::string &zone, 
                              const DnsItems &items);
    static uint8_t *AddQuestionSection(uint8_t *ptr, const std::string &name, 
                                       uint16_t type, uint16_t cl, 
                                       uint16_t &length);
    static uint8_t *AddAnswerSection(uint8_t *ptr, const DnsItem &item, 
                                     uint16_t &length);
    static void BuildDnsHeader(dnshdr *dns, uint16_t xid, DnsReq req, 
                               DnsOpcode op, bool rd, bool ra, uint8_t ret, 
                               uint16_t ques_count);

    static inline uint16_t DataLength(uint16_t plen, uint16_t offset,
                                      uint16_t size) {
        return (offset ? (plen ? plen + 2 + 1 : 2) : (size ? size + 2 : 1));
    }
    static bool IsIPv4(std::string name, uint32_t &addr);
    static void GetReverseZones(const Subnet &subnet, ZoneList &zones);
    static void GetReverseZone(uint32_t addr, uint32_t plen, std::string &zone);
    static bool GetAddrFromPtrName(std::string &ptr_name, uint32_t &mask);
    static std::string GetFQDN(const std::string &name, const std::string &domain, 
                               const std::string &match);
    static bool HasSpecialChars(const std::string &name);
    static void RemoveSpecialChars(std::string &name);
private:

    static inline uint8_t *ReadByte(uint8_t *ptr, uint8_t &value) {
        value = *(uint8_t *) ptr;
        ptr += 1;

        return ptr;
    }

    static inline uint8_t *ReadShort(uint8_t *ptr, uint16_t &value) {
        value = ntohs(*(uint16_t *) ptr);
        ptr += 2;

        return ptr;
    }

    static inline uint8_t *ReadWord(uint8_t *ptr, uint32_t &value) {
        value = ntohl(*(uint32_t *) ptr);
        ptr += 4;

        return ptr;
    }

    static inline uint8_t *WriteByte(uint8_t *ptr, uint8_t value) {
        *(uint8_t *) ptr = value;
        ptr += 1;

        return ptr;
    }

    static inline uint8_t *WriteShort(uint8_t *ptr, uint16_t value) {
        *(uint16_t *) ptr = htons(value);
        ptr += 2;

        return ptr;
    }

    static inline uint8_t *WriteWord(uint8_t *ptr, uint32_t value) {
        *(uint32_t *) ptr = htonl(value);
        ptr += 4;

        return ptr;
    }
    static uint8_t *AddData(uint8_t *ptr, const DnsItem &item, 
                            uint16_t &length);
    static uint8_t *AddAdditionalSection(uint8_t *ptr, const std::string name, 
                                         uint16_t type, uint16_t cl, 
                                         uint32_t ttl, const std::string &data, 
                                         uint16_t &length);
    static uint8_t *AddUpdate(uint8_t *ptr, const DnsItem &item,
                             uint16_t cl, uint32_t ttl, uint16_t &length);
    static uint8_t *ReadName(uint8_t *dns, uint8_t *ptr, std::string &name,
                             uint16_t &plen, uint16_t &offset);
    static uint8_t *ReadData(uint8_t *buf, uint8_t *ptr, DnsItem &item);
    static uint8_t *ReadQuestionEntry(uint8_t *buf, uint8_t *ptr, 
                                      DnsItem &item);
    static uint8_t *ReadAnswerEntry(uint8_t *dns, uint8_t *ptr, DnsItem &item);
};

// Identify the offsets for names in a DNS message
class DnsNameEncoder {
public:
    struct Name {
        std::string name;
        uint16_t offset;
        Name(std::string &n, uint16_t oset) : name(n), offset(oset) {}
    };

    DnsNameEncoder() {};
    virtual ~DnsNameEncoder() {};
    void AddName(std::string &name, uint16_t curr_msg_offset, 
                 uint16_t &name_plen, uint16_t &name_offset);

private:
    bool IsPresent(std::string &name, uint16_t &name_offset);

    std::vector<Name> names_;
    DISALLOW_COPY_AND_ASSIGN(DnsNameEncoder);
};

#endif // __bind_util_h__
