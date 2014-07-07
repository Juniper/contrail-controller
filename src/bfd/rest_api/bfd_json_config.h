/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */
#ifndef BFD_JSON_CONFIG_H_
#define BFD_JSON_CONFIG_H_

#include <vector>
#include <map>
#include <string>
#include <boost/asio/ip/address.hpp>
#include <rapidjson/document.h>

#include "bfd/bfd_common.h"

namespace BFD {

struct JsonData {
    struct Constraint {
        const char* member_name;
        bool (rapidjson::Value::*typecheck_func)();
    };

    virtual bool ValidateJsonDocument(const rapidjson::Value& document) = 0;
    virtual bool ParseFromJsonDocument(const rapidjson::Value& document) = 0;
    virtual void EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator) = 0;

    bool ParseFromJsonString(const std::string& json);
    void EncodeJsonString(std::string* json);

    virtual ~JsonData() {}

 protected:
    bool AreConstraintsMet(std::vector<Constraint> constraints,
        const rapidjson::Value& document);
};

struct JsonConfig : public JsonData {
    boost::asio::ip::address address;
    TimeInterval desired_min_tx_interval;
    TimeInterval required_min_rx_interval;
    int detection_time_multiplier;

    bool ValidateJsonDocument(const rapidjson::Value& document);
    bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator);
};

struct JsonState : public JsonData {
    BfdJsonConfig session_config;

    Discriminator local_discriminator;
    Discriminator remote_discriminator;
    BFDState bfd_local_state;
    BFDState bfd_remote_state;
    TimeInterval remote_min_rx_interval;

    bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator);
};

struct JsonStateNotification : public JsonData {
    boost::asio::ip::address address;
    BFDState state;

    bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator);
};

struct JsonStateNotificationList : public JsonData {
    std::vector<BfdJsonStateNotification> notifications;

    bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator);
};

struct JsonStateMap : public JsonData {
    typedef std::map<boost::asio::ip::address, BFDState> StateMap;
    StateMap states;

    bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType* allocator);
};

}  // namespace BFD

#endif /* BFD_JSON_CONFIG_H_ */
