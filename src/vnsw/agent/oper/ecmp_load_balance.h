/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ecmp_load_balance_hpp
#define vnsw_agent_ecmp_load_balance_hpp
#include <boost/intrusive_ptr.hpp>
#include <vnc_cfg_types.h>
#include "vr_flow.h"

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

class EcmpField {
public:
    EcmpField() {
        ref_count_= 0;
    }

    uint32_t RefCount() const {
        return ref_count_;
    }
private:
   friend void intrusive_ptr_add_ref(EcmpField* ptr);
   friend void intrusive_ptr_release(EcmpField* ptr);
   mutable tbb::atomic<uint32_t> ref_count_;
};

inline void intrusive_ptr_add_ref(EcmpField* ptr) {
    ptr->ref_count_.fetch_and_increment();
}

inline void intrusive_ptr_release(EcmpField* ptr){
    uint32_t prev = ptr->ref_count_.fetch_and_decrement();
    if(prev == 1) {
      delete ptr;
    }
}

class EcmpHashFields {
public:
    typedef boost::intrusive_ptr<EcmpField> EcmpFieldPtr;

    EcmpHashFields(uint8_t hash_fields_to_use):
        hash_fields_to_use_(hash_fields_to_use) {
    }

    uint8_t HashFieldsToUse() {
        return hash_fields_to_use_;
    }
    // This function will be called to get most common ecmp fields
    uint8_t  CalculateHashFieldsToUse() {
        uint32_t max = 0;
        if (sip_.get() && max < sip_.get()->RefCount())
            max = sip_.get()->RefCount();

        if (dip_.get() && max < dip_.get()->RefCount())
            max = dip_.get()->RefCount();

        if (proto_.get() && max < proto_.get()->RefCount())
            max = proto_.get()->RefCount();

        if (sport_.get() && max < sport_.get()->RefCount())
            max = sport_.get()->RefCount();

        if (dport_.get() && max < dport_.get()->RefCount())
            max = dport_.get()->RefCount();

        hash_fields_to_use_ = 0;

        if (sip_.get() && max == sip_.get()->RefCount())
            hash_fields_to_use_ |= VR_FLOW_KEY_SRC_IP;

        if (dip_.get() && max == dip_.get()->RefCount())
            hash_fields_to_use_ |= VR_FLOW_KEY_DST_IP;

        if (proto_.get() && max == proto_.get()->RefCount())
            hash_fields_to_use_ |= VR_FLOW_KEY_PROTO;

        if (sport_.get() && max == sport_.get()->RefCount())
            hash_fields_to_use_ |= VR_FLOW_KEY_SRC_PORT;

        if (dport_.get() && max == dport_.get()->RefCount())
            hash_fields_to_use_ |= VR_FLOW_KEY_DST_PORT;
        return hash_fields_to_use_;
    }
    //This function used to calculate the Change in ecmp fields
    //between old and new ecmp fields allocate the Intrusive pointer
    //if route or Composite NH doesn't point any ecmp field afterd
    //that assign intrusive pointer every time new route comes with that field
    void CalculateChangeInEcmpFields(
        const EcmpLoadBalance &old_ecmp_load_balanceparms,
        const EcmpLoadBalance &current_ecmp_load_balance,
        EcmpHashFields& ecmp_hash_fields) {
        EcmpLoadBalance old_ecmp_load_balance;
        old_ecmp_load_balance.Copy(old_ecmp_load_balanceparms);

        if (old_ecmp_load_balance.AllSet())
            old_ecmp_load_balance.reset();

        if (!old_ecmp_load_balance.is_ip_protocol_set() &&
             current_ecmp_load_balance.is_ip_protocol_set()) {
            if (!proto_.get()) {
                if (!ecmp_hash_fields.proto_.get())
                    ecmp_hash_fields.proto_ = new EcmpField;
                proto_ = ecmp_hash_fields.proto_;
            } else {
                ecmp_hash_fields.proto_ = proto_;
            }
        } else if (old_ecmp_load_balance.is_ip_protocol_set() &&
                   !current_ecmp_load_balance.is_ip_protocol_set()) {
            intrusive_ptr_release(proto_.get());
        }

        if (!old_ecmp_load_balance.is_source_ip_set() &&
             current_ecmp_load_balance.is_source_ip_set()) {
            if (!sip_.get()) {
                if (!ecmp_hash_fields.sip_.get())
                    ecmp_hash_fields.sip_ = new EcmpField;
                sip_ = ecmp_hash_fields.sip_;
            } else {
                ecmp_hash_fields.sip_ = sip_;
            }
        } else if (old_ecmp_load_balance.is_source_ip_set() &&
                   !current_ecmp_load_balance.is_source_ip_set()) {
            intrusive_ptr_release(sip_.get());
        }

        if (!old_ecmp_load_balance.is_destination_ip_set() &&
            current_ecmp_load_balance.is_destination_ip_set()) {
            if (!dip_.get()) {
                if (!ecmp_hash_fields.dip_.get())
                    ecmp_hash_fields.dip_ = new EcmpField;
                dip_ = ecmp_hash_fields.dip_;
            } else {
                ecmp_hash_fields.dip_ = dip_;
            }
        } else if (old_ecmp_load_balance.is_destination_ip_set() &&
                  !current_ecmp_load_balance.is_destination_ip_set()) {
            intrusive_ptr_release(dip_.get());
        }

        if (!old_ecmp_load_balance.is_source_port_set() &&
             current_ecmp_load_balance.is_source_port_set()) {
            if (!sport_.get()) {
                if (!ecmp_hash_fields.sport_.get())
                    ecmp_hash_fields.sport_ = new EcmpField;
                sport_= ecmp_hash_fields.sport_;
            } else {
                ecmp_hash_fields.sport_ = sport_;
            }
        } else if (old_ecmp_load_balance.is_source_port_set() &&
                   !current_ecmp_load_balance.is_source_port_set()) {
            intrusive_ptr_release(sport_.get());
        }

        if (!old_ecmp_load_balance.is_destination_port_set() &&
            current_ecmp_load_balance.is_destination_port_set()) {
            if (!dport_.get()) {
                if (!ecmp_hash_fields.dport_.get())
                    ecmp_hash_fields.dport_ = new EcmpField;
                dport_ = ecmp_hash_fields.dport_;
            } else {
                ecmp_hash_fields.dport_ = dport_;
            }
        } else if (old_ecmp_load_balance.is_destination_port_set() &&
                   !current_ecmp_load_balance.is_destination_port_set()) {
            intrusive_ptr_release(dport_.get());
        }
}
private:
    uint8_t hash_fields_to_use_;
    EcmpFieldPtr sip_;
    EcmpFieldPtr dip_;
    EcmpFieldPtr proto_;
    EcmpFieldPtr sport_;
    EcmpFieldPtr dport_;
};
#endif
