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
#include <bfd/bfd_common.h>

namespace BFD {


class JsonData {
 public:
    virtual bool ParseFromJsonDocument(const rapidjson::Value& document) = 0;
    virtual void EncodeJsonDocument(rapidjson::Value* document, rapidjson::Value::AllocatorType *allocator) = 0;

    bool ParseFromJsonString(const std::string& json);
    void EncodeJsonString(std::string* json);

    virtual ~JsonData() {}
};

class BfdJsonConfig : public JsonData {
 public:
    boost::asio::ip::address address;
    TimeInterval desired_min_tx_interval;
    TimeInterval required_min_rx_interval;
    int detection_time_multiplier;

    bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document, rapidjson::Value::AllocatorType *allocator);
};

class BfdJsonState : public JsonData {
 public:
    BfdJsonConfig session_config;

    Discriminator local_discriminator;
    Discriminator remote_discriminator;
    BFDState bfd_local_state;
    BFDState bfd_remote_state;
    TimeInterval remote_min_rx_interval;

    bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document, rapidjson::Value::AllocatorType *allocator);
};

class BfdJsonStateNotification : public JsonData {
 public:
    boost::asio::ip::address address;
    BFDState state;

    bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document, rapidjson::Value::AllocatorType *allocator);
};

class BfdJsonStateNotificationList : public JsonData {
 public:
    std::vector<BfdJsonStateNotification> notifications;

    bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document, rapidjson::Value::AllocatorType *allocator);
};

class BfdJsonStateMap : public JsonData {
 public:
    typedef std::map<boost::asio::ip::address, BFDState> StateMap;
    StateMap states;

    bool ParseFromJsonDocument(const rapidjson::Value& document);
    void EncodeJsonDocument(rapidjson::Value* document, rapidjson::Value::AllocatorType* allocator);
};

};  // namespace BFD

#endif /* BFD_JSON_CONFIG_H_ */
