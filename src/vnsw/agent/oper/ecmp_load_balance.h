/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ecmp_load_balance_hpp
#define vnsw_agent_ecmp_load_balance_hpp

#include <vnc_cfg_types.h>

using namespace boost::uuids;
using namespace std;

namespace autogen {
    struct EcmpHashingIncludeFields;
}

static const std::string HashingFieldsStr[] = {
    "l3-source-address",
    "l3-destination-address",
    "l4-protocol",
    "l4-source-port",
    "l4-destination-port"
};
static const std::string LoadBalanceDecision = "field-hash";

class EcmpLoadBalance {
public:
    enum kHashingFields {
        SOURCE_IP,
        DESTINATION_IP,
        IP_PROTOCOL,
        SOURCE_PORT,
        DESTINATION_PORT,
        NUM_HASH_FIELDS
    };

    EcmpLoadBalance() {
        SetAll();
    }
    virtual ~EcmpLoadBalance() { }
    
    const std::string &source_ip_str() const {
        return HashingFieldsStr[(uint8_t)EcmpLoadBalance::SOURCE_IP];
    }
    const std::string &destination_ip_str() const {
        return HashingFieldsStr[(uint8_t)EcmpLoadBalance::DESTINATION_IP];
    }
    const std::string &source_port_str() const {
        return HashingFieldsStr[(uint8_t)EcmpLoadBalance::SOURCE_PORT];
    }
    const std::string &destination_port_str() const {
        return HashingFieldsStr[(uint8_t)EcmpLoadBalance::DESTINATION_PORT];
    }
    const std::string &ip_protocol_str() const {
        return HashingFieldsStr[(uint8_t)EcmpLoadBalance::IP_PROTOCOL];
    }

    void GetStringVector (std::vector<std::string> &string_vector) const {
        for (uint8_t field_type = ((uint8_t) EcmpLoadBalance::SOURCE_IP);
             field_type < ((uint8_t) EcmpLoadBalance::NUM_HASH_FIELDS);
             field_type++) {
            if (hash_fields_to_use_[field_type])
                string_vector.push_back(HashingFieldsStr[field_type]);
        }
    }

    bool operator!=(const EcmpLoadBalance &rhs) const {
        for (uint8_t field_type = ((uint8_t) EcmpLoadBalance::SOURCE_IP);
             field_type < ((uint8_t) EcmpLoadBalance::NUM_HASH_FIELDS);
             field_type++) {
            if (hash_fields_to_use_[field_type] !=
                rhs.hash_fields_to_use_[field_type])
                return true;
        }
        return false;
    }

    virtual void Copy(const EcmpLoadBalance &rhs) {
        for (uint8_t field_type = ((uint8_t) EcmpLoadBalance::SOURCE_IP);
             field_type < ((uint8_t) EcmpLoadBalance::NUM_HASH_FIELDS);
             field_type++) {
            hash_fields_to_use_[field_type] =
                rhs.hash_fields_to_use_[field_type];
        }
    }

    void SetAll() {
        for (uint8_t field_type = ((uint8_t) EcmpLoadBalance::SOURCE_IP);
             field_type < ((uint8_t) EcmpLoadBalance::NUM_HASH_FIELDS);
             field_type++) {
            hash_fields_to_use_[field_type] = true;
        }
    }

    void ResetAll() {
        for (uint8_t field_type = ((uint8_t) EcmpLoadBalance::SOURCE_IP);
             field_type < ((uint8_t) EcmpLoadBalance::NUM_HASH_FIELDS);
             field_type++) {
            hash_fields_to_use_[field_type] = false;
        }
    }

    bool AllSet() const {
        for (uint8_t field_type = ((uint8_t) EcmpLoadBalance::SOURCE_IP);
             field_type < ((uint8_t) EcmpLoadBalance::NUM_HASH_FIELDS);
             field_type++) {
            if (hash_fields_to_use_[field_type] == false)
                return false;
        }
        return true;
    }
    void set_source_ip() {
        hash_fields_to_use_[SOURCE_IP] = true;
    }
    void reset_source_ip() {
        hash_fields_to_use_[SOURCE_IP] = false;
    }
    void set_destination_ip() {
        hash_fields_to_use_[DESTINATION_IP] = true;
    }
    void reset_destination_ip() {
        hash_fields_to_use_[DESTINATION_IP] = false;
    }
    void set_ip_protocol() {
        hash_fields_to_use_[IP_PROTOCOL] = true;
    }
    void reset_ip_protocol() {
        hash_fields_to_use_[IP_PROTOCOL] = false;
    }
    void set_source_port() {
        hash_fields_to_use_[SOURCE_PORT] = true;
    }
    void reset_source_port() {
        hash_fields_to_use_[SOURCE_PORT] = false;
    }
    void set_destination_port() {
        hash_fields_to_use_[DESTINATION_PORT] = true;
    }
    void reset_destination_port() {
        hash_fields_to_use_[DESTINATION_PORT] = false;
    }

    bool is_source_ip_set() const {
        return (hash_fields_to_use_[SOURCE_IP]);
    }
    bool is_destination_ip_set() const {
        return (hash_fields_to_use_[DESTINATION_IP]);
    }
    bool is_source_port_set() const {
        return (hash_fields_to_use_[SOURCE_PORT]);
    }
    bool is_destination_port_set() const {
        return (hash_fields_to_use_[DESTINATION_PORT]);
    }
    bool is_ip_protocol_set() const {
        return (hash_fields_to_use_[IP_PROTOCOL]);
    }

    void reset() {
        for (uint8_t field_type = ((uint8_t) EcmpLoadBalance::SOURCE_IP);
             field_type < ((uint8_t) EcmpLoadBalance::NUM_HASH_FIELDS);
             field_type++) {
            hash_fields_to_use_[field_type] = false;
        }
    }

    bool UpdateFields
        (const autogen::EcmpHashingIncludeFields &ecmp_hashing_fields) {
        bool ret = false;    

        if (hash_fields_to_use_[SOURCE_IP] != ecmp_hashing_fields.source_ip) {
            hash_fields_to_use_[SOURCE_IP] = ecmp_hashing_fields.source_ip;
            ret = true;
        }
        if (hash_fields_to_use_[DESTINATION_IP] !=
            ecmp_hashing_fields.destination_ip) {
            hash_fields_to_use_[DESTINATION_IP] =
                ecmp_hashing_fields.destination_ip;
            ret = true;
        }
        if (hash_fields_to_use_[SOURCE_PORT] !=
            ecmp_hashing_fields.source_port) {
            hash_fields_to_use_[SOURCE_PORT] = ecmp_hashing_fields.source_port;
            ret = true;
        }
        if (hash_fields_to_use_[DESTINATION_PORT] !=
            ecmp_hashing_fields.destination_port) {
            hash_fields_to_use_[DESTINATION_PORT] =
                ecmp_hashing_fields.destination_port;
            ret = true;
        }
        if (hash_fields_to_use_[IP_PROTOCOL] !=
            ecmp_hashing_fields.ip_protocol) {
            hash_fields_to_use_[IP_PROTOCOL] = ecmp_hashing_fields.ip_protocol;
            ret = true;
        }
        return ret;
    }

private:    
    bool hash_fields_to_use_[NUM_HASH_FIELDS];
};

class VmiEcmpLoadBalance : public EcmpLoadBalance {
public:
    VmiEcmpLoadBalance() : EcmpLoadBalance(), use_global_vrouter_(true) { }
    virtual ~VmiEcmpLoadBalance() { }

    bool use_global_vrouter() const {return use_global_vrouter_;}
    void set_use_global_vrouter(bool use_global_vrouter) {
        use_global_vrouter_ = use_global_vrouter;
    }
    virtual void Copy(const VmiEcmpLoadBalance &rhs) {
        use_global_vrouter_ = rhs.use_global_vrouter_;
        EcmpLoadBalance::Copy(rhs);
    }

private:
    bool use_global_vrouter_;
};

#endif
