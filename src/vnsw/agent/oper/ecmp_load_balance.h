/*
 * Copyright (c) 2016 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_ecmp_load_balance_hpp
#define vnsw_agent_ecmp_load_balance_hpp
#include <boost/intrusive_ptr.hpp>
#include <vnc_cfg_types.h>

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

inline void intrusive_ptr_release(EcmpField* ptr) {
    uint32_t prev = ptr->ref_count_.fetch_and_decrement();
    if (prev == 1) {
      delete ptr;
    }
}

class EcmpHashFields {
public:
    typedef boost::intrusive_ptr<EcmpField> EcmpFieldPtr;

    EcmpHashFields() {
    }

    EcmpHashFields(const uint8_t hash_fields_to_use ) {
       hash_fields_to_use_ = hash_fields_to_use;
    }
    void  operator = (const uint8_t hash_fields_to_use) {
       hash_fields_to_use_ = hash_fields_to_use;
    }

    void AllocateEcmpFields() {
        sip_ = new EcmpField;
        dip_ = new EcmpField;
        proto_ = new EcmpField;
        sport_ = new EcmpField;
        dport_ = new EcmpField;
    }

    uint8_t HashFieldsToUse() const {
        return hash_fields_to_use_;
    }

    void SetHashFieldtoUse(EcmpField *ptr, uint8_t key) {
        if (ptr && ptr->RefCount() == 1) {
            comp_hash_fields_to_use_ |= 1 << key;
        }
    }
    // If the field is not set create intrusive pointer
    // else release the pointer.
    void SetChangeInHashField(bool is_field_set, EcmpFieldPtr& fieldPtr,
                              EcmpFieldPtr &objFieldPtr) {
        if (!is_field_set) {
            if (!objFieldPtr.get()) {
                objFieldPtr = fieldPtr;
            }
        } else {
            objFieldPtr.reset();
        }
    }
    // This function will be called to ge intersection of ecmp fields
    uint8_t  CalculateHashFieldsToUse() {
        comp_hash_fields_to_use_ = 0;
        SetHashFieldtoUse(sip_.get(), EcmpLoadBalance::SOURCE_IP);
        SetHashFieldtoUse(dip_.get(), EcmpLoadBalance::DESTINATION_IP);
        SetHashFieldtoUse(proto_.get(), EcmpLoadBalance::IP_PROTOCOL);
        SetHashFieldtoUse(sport_.get(), EcmpLoadBalance::SOURCE_PORT);
        SetHashFieldtoUse(dport_.get(), EcmpLoadBalance::DESTINATION_PORT);
        return comp_hash_fields_to_use_;
    }
    //This function used to calculate the Change in ecmp fields
    void CalculateChangeInEcmpFields(const EcmpLoadBalance &ecmp_load_balance,
                                     EcmpHashFields& ecmp_hash_fields) {
        SetChangeInHashField(ecmp_load_balance.is_source_ip_set(),
                             ecmp_hash_fields.sip_, sip_);
        SetChangeInHashField(ecmp_load_balance.is_destination_ip_set(),
                             ecmp_hash_fields.dip_, dip_);
        SetChangeInHashField(ecmp_load_balance.is_ip_protocol_set(),
                             ecmp_hash_fields.proto_, proto_);
        SetChangeInHashField(ecmp_load_balance.is_source_port_set(),
                             ecmp_hash_fields.sport_, sport_);
        SetChangeInHashField(ecmp_load_balance.is_destination_port_set(),
                             ecmp_hash_fields.dport_, dport_);
    }

    bool IsFieldsInUseChanged() {
        return hash_fields_to_use_ != CalculateHashFieldsToUse();
    }

    void SetHashFieldstoUse() {
        hash_fields_to_use_ = comp_hash_fields_to_use_;
    }

    void Reset() {
        sip_ = NULL;
        dip_ = NULL;
        proto_ = NULL;
        sport_ = NULL;
        dport_ = NULL;
    }

private:
    // This will have latest computed value
    uint8_t comp_hash_fields_to_use_;
    uint8_t hash_fields_to_use_;
    EcmpFieldPtr sip_;
    EcmpFieldPtr dip_;
    EcmpFieldPtr proto_;
    EcmpFieldPtr sport_;
    EcmpFieldPtr dport_;
    DISALLOW_COPY_AND_ASSIGN(EcmpHashFields);
};
#endif
