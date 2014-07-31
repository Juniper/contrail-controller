/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#ifndef SRC_BFD_JSON_CONFIG_H_
#define SRC_BFD_JSON_CONFIG_H_

#include "bfd/bfd_common.h"

#include <vector>
#include <map>
#include <string>
#include <boost/asio/ip/address.hpp>
#include <rapidjson/document.h>

namespace BFD {
namespace REST {

struct JsonData {
    class Constraint {
     public:
        typedef bool (rapidjson::Value::*TypecheckFunc)() const;

        Constraint(const char* member_name, TypecheckFunc func)
            : member_name(member_name), typecheck_func(func) {}

        const char* member_name;
        TypecheckFunc typecheck_func;
    };

    virtual bool ValidateJsonDocument(const rapidjson::Value& document) = 0;
    virtual bool ParseFromJsonDocument(const rapidjson::Value& document) = 0;
    virtual void EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator) = 0;

    bool ParseFromJsonString(const std::string& json);
    void EncodeJsonString(std::string* json);

    virtual ~JsonData() {}

 protected:
    bool AreConstraintsMet(const std::vector<Constraint>& constraints,
    const rapidjson::Value& document);
};

struct JsonConfig : public JsonData {
    JsonConfig();
    JsonConfig(boost::asio::ip::address address,
               TimeInterval desired_min_tx_interval,
               TimeInterval required_min_rx_interval,
               int detection_time_multiplier);

    bool ValidateJsonDocument(const rapidjson::Value& document);
    bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator);

    boost::asio::ip::address address;
    TimeInterval desired_min_tx_interval;
    TimeInterval required_min_rx_interval;
    int detection_time_multiplier;
};

struct JsonState : public JsonData {
    virtual bool ValidateJsonDocument(const rapidjson::Value& document);
    virtual bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator);

    JsonConfig session_config;

    Discriminator local_discriminator;
    Discriminator remote_discriminator;
    BFDState bfd_local_state;
    BFDState bfd_remote_state;
    TimeInterval remote_min_rx_interval;
};

struct JsonStateNotification : public JsonData {
    virtual bool ValidateJsonDocument(const rapidjson::Value& document);
    virtual bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator);

    boost::asio::ip::address address;
    BFDState state;
};

struct JsonStateNotificationList : public JsonData {
    virtual bool ValidateJsonDocument(const rapidjson::Value& document);
    virtual bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator);

    std::vector<JsonStateNotification> notifications;
};

struct JsonStateMap : public JsonData {
    typedef std::map<boost::asio::ip::address, BFDState> StateMap;
    StateMap states;

    virtual bool ValidateJsonDocument(const rapidjson::Value& document);
    virtual bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType* allocator);
};

}  // namespace REST
}  // namespace BFD

#endif  // SRC_BFD_JSON_CONFIG_H_
