/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ecmp_load_balance_hpp
#define vnsw_agent_ecmp_load_balance_hpp

#include <vnc_cfg_types.h>

using namespace boost::uuids;
using namespace std;

#define LOAD_BALANCE_DECISION "field-hash"
#define SOURCE_MAC_STR "l2-source-address"
#define DESTINATION_MAC_STR "l2-destination-address"
#define SOURCE_IP_STR "l3-source-address"
#define DESTINATION_IP_STR "l3-destination-address"
#define IP_PROTOCOL_STR "l4-protocol"
#define SOURCE_PORT_STR "l4-source-port"
#define DESTINATION_PORT_STR "l4-destination-port"

namespace autogen {
    struct EcmpHashingIncludeFields;
}

class EcmpLoadBalance {
  public:
    static const uint8_t ALL = 0x3F;
    enum HashingFields {
        SOURCE_MAC,
        DESTINATION_MAC,
        SOURCE_IP,
        DESTINATION_IP,
        IP_PROTOCOL,
        SOURCE_PORT,
        DESTINATION_PORT
    };
    EcmpLoadBalance() : hash_fields_to_use_(0), is_set_(false) {
    }
    virtual ~EcmpLoadBalance() { }
    
    static void GetStringVector(uint8_t ecmp_hash_fields_to_use,
                                std::vector<std::string> &string_vector) {
        for (uint8_t field_type = ((uint8_t) EcmpLoadBalance::SOURCE_MAC);
             field_type <= ((uint8_t) EcmpLoadBalance::DESTINATION_PORT);
             field_type++) {
            uint8_t field = ecmp_hash_fields_to_use & field_type;
            if (field == 0)
                continue;
            if (field == ((uint8_t) EcmpLoadBalance::SOURCE_MAC))
                string_vector.push_back(SOURCE_MAC_STR);
            if (field == ((uint8_t) EcmpLoadBalance::DESTINATION_MAC))
                string_vector.push_back(DESTINATION_MAC_STR);
            if (field == ((uint8_t) EcmpLoadBalance::SOURCE_IP))
                string_vector.push_back(SOURCE_IP_STR);
            if (field == ((uint8_t) EcmpLoadBalance::DESTINATION_IP))
                string_vector.push_back(DESTINATION_IP_STR);
            if (field == ((uint8_t) EcmpLoadBalance::IP_PROTOCOL))
                string_vector.push_back(IP_PROTOCOL_STR);
            if (field == ((uint8_t) EcmpLoadBalance::SOURCE_PORT))
                string_vector.push_back(SOURCE_PORT_STR);
            if (field == ((uint8_t) EcmpLoadBalance::DESTINATION_PORT))
                string_vector.push_back(DESTINATION_PORT_STR);
        }
    }

    virtual bool Compare(const EcmpLoadBalance *rhs) const {
        return false;
    }

    bool operator!=(const EcmpLoadBalance &rhs) const {
        return ((is_set_ == rhs.is_set_) &&
                (hash_fields_to_use_ == rhs.hash_fields_to_use_));
    }

    bool operator<(const EcmpLoadBalance &rhs) const {
        return IsLess(rhs);
    }

    bool IsLess(const EcmpLoadBalance &rhs) const {
        if (is_set_ != rhs.is_set_)
            return is_set_ < rhs.is_set_;
        return hash_fields_to_use_ < rhs.hash_fields_to_use_;
    }

    void Copy(const EcmpLoadBalance &rhs) {
        is_set_ = rhs.is_set_;
        hash_fields_to_use_ = rhs.hash_fields_to_use_;
    }

    void set_source_mac() {
        is_set_ = true;
        hash_fields_to_use_ |= (1 << SOURCE_MAC);
    }
    void reset_source_mac() {
        is_set_ = true;
        hash_fields_to_use_ &= ~(1 << SOURCE_MAC);
    }
    void set_destination_mac() {
        is_set_ = true;
        hash_fields_to_use_ |= (1 << DESTINATION_MAC);
    }
    void reset_destination_mac() {
        is_set_ = true;
        hash_fields_to_use_ &= ~(1 << DESTINATION_MAC);
    }
    void set_source_ip() {
        is_set_ = true;
        hash_fields_to_use_ |= (1 << SOURCE_IP);
    }
    void reset_source_ip() {
        is_set_ = true;
        hash_fields_to_use_ &= ~(1 << SOURCE_IP);
    }
    void set_destination_ip() {
        is_set_ = true;
        hash_fields_to_use_ |= (1 << DESTINATION_IP);
    }
    void reset_destination_ip() {
        is_set_ = true;
        hash_fields_to_use_ &= ~(1 << DESTINATION_IP);
    }
    void set_ip_protocol() {
        is_set_ = true;
        hash_fields_to_use_ |= (1 << IP_PROTOCOL);
    }
    void reset_ip_protocol() {
        is_set_ = true;
        hash_fields_to_use_ &= ~(1 << IP_PROTOCOL);
    }
    void set_source_port() {
        is_set_ = true;
        hash_fields_to_use_ |= (1 << SOURCE_PORT);
    }
    void reset_source_port() {
        is_set_ = true;
        hash_fields_to_use_ &= ~(1 << SOURCE_PORT);
    }
    void set_destination_port() {
        is_set_ = true;
        hash_fields_to_use_ |= (1 << DESTINATION_PORT);
    }
    void reset_destination_port() {
        is_set_ = true;
        hash_fields_to_use_ &= ~(1 << DESTINATION_PORT);
    }

    static bool is_source_mac_set(uint32_t hash_fields_to_use) {
        return ((hash_fields_to_use & (1 << EcmpLoadBalance::SOURCE_MAC)) != 0);
    }

    static bool is_destination_mac_set(uint32_t hash_fields_to_use) {
        return ((hash_fields_to_use & (1 << EcmpLoadBalance::DESTINATION_MAC))
                != 0);
    }

    static bool is_source_ip_set(uint32_t hash_fields_to_use) {
        return ((hash_fields_to_use & (1 << EcmpLoadBalance::SOURCE_IP)) != 0);
    }

    static bool is_destination_ip_set(uint32_t hash_fields_to_use) {
        return ((hash_fields_to_use & (1 << EcmpLoadBalance::DESTINATION_IP))
                != 0);
    }

    static bool is_source_port_set(uint32_t hash_fields_to_use) {
        return ((hash_fields_to_use & (1 << EcmpLoadBalance::SOURCE_PORT))
                != 0);
    }

    static bool is_destination_port_set(uint32_t hash_fields_to_use) {
        return ((hash_fields_to_use & (1 << EcmpLoadBalance::DESTINATION_PORT))
                != 0);
    }

    static bool is_ip_protocol_set(uint32_t hash_fields_to_use) {
        return ((hash_fields_to_use & (1 << EcmpLoadBalance::IP_PROTOCOL))
                != 0);
    }

    uint8_t hash_fields_to_use() const {return hash_fields_to_use_;}
    void set_hash_fields_to_use(uint8_t hash_fields_to_use) {
        hash_fields_to_use_ = hash_fields_to_use;
    }
    void reset() {
        is_set_ = false;
        hash_fields_to_use_ = 0;
    }
    bool is_set() const {return is_set_;}

    bool UpdateFields(const autogen::EcmpHashingIncludeFields &ecmp_hashing_fields) {
        is_set_ = true;

        bool old_hash_fields_to_use = hash_fields_to_use_;
        if (ecmp_hashing_fields.source_mac)
            set_source_mac();
        else
            reset_source_mac();
        if (ecmp_hashing_fields.destination_mac)
            set_destination_mac();
        else
            reset_destination_mac();
        if (ecmp_hashing_fields.source_ip)
            set_source_ip();
        else
            reset_source_ip();
        if (ecmp_hashing_fields.destination_ip)
            set_destination_ip();
        else
            reset_destination_ip();
        if (ecmp_hashing_fields.ip_protocol)
            set_ip_protocol();
        else
            reset_ip_protocol();
        if (ecmp_hashing_fields.source_port)
            set_source_port();
        else
            reset_source_port();
        if (ecmp_hashing_fields.destination_port)
            set_destination_port();
        else
            reset_destination_port();

        if (old_hash_fields_to_use != hash_fields_to_use_)
            return true;
        return false;
    }

private:    
    uint8_t hash_fields_to_use_;
    bool is_set_;
    const Agent *agent_;
    DISALLOW_COPY_AND_ASSIGN(EcmpLoadBalance);
};

#endif
