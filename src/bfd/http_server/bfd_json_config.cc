/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include <vector>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "bfd/http_server/bfd_json_config.h"

namespace BFD {

static const char *address_name = "Address";
static const char *desired_min_tx_interval_name = "DesiredMinTxInterval";
static const char *required_min_rx_interval_name = "RequiredMinRxInterval";
static const char *detection_time_multiplier_name = "DetectionMultiplier";

static const char *local_session_state_name = "LocalSessionState";
static const char *remote_session_state_name = "RemoteSessionState";
static const char *remote_min_rx_interval_name = "RemoteMinRxInterval";
static const char *local_discriminator_name = "LocalDiscriminator";
static const char *remote_discriminator_name = "RemoteDiscriminator";

static const char *session_state_name = "SessionState";


bool JsonData::ParseFromJsonString(const std::string& json) {
    rapidjson::Document document;
    document.Parse<0>(json.c_str());
    return ParseFromJsonDocument(document);
}

void JsonData::EncodeJsonString(std::string* json) {
    rapidjson::Document document;
    EncodeJsonDocument(&document, &document.GetAllocator());
    rapidjson::StringBuffer strbuf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
    document.Accept(writer);
    *json = strbuf.GetString();
}


bool BfdJsonConfig::ParseFromJsonDocument(const rapidjson::Value& document) {
    if (document.IsObject() && document.HasMember(address_name) && document[address_name].IsString()
            && document.HasMember(desired_min_tx_interval_name) && document[desired_min_tx_interval_name].IsInt()
            && document.HasMember(required_min_rx_interval_name) && document[required_min_rx_interval_name].IsInt()
            && document.HasMember(detection_time_multiplier_name) && document[detection_time_multiplier_name].IsInt()) {
        boost::system::error_code ec;
        address = boost::asio::ip::address::from_string(document[address_name].GetString(), ec);
        if (ec)
            return false;

        desired_min_tx_interval = boost::posix_time::microseconds(document[desired_min_tx_interval_name].GetInt());
        required_min_rx_interval = boost::posix_time::microseconds(document[required_min_rx_interval_name].GetInt());
        detection_time_multiplier = document[detection_time_multiplier_name].GetInt();
        return true;
    } else {
        return false;
    }
}

void BfdJsonConfig::EncodeJsonDocument(rapidjson::Value* document, rapidjson::Value::AllocatorType *allocator) {
    document->SetObject();
    std::string address_str = address.to_string();
    rapidjson::Value address_val(address_str.c_str(), *allocator);
    document->AddMember(address_name, address_val, *allocator);

    document->AddMember(desired_min_tx_interval_name, desired_min_tx_interval.total_microseconds(), *allocator);
    document->AddMember(required_min_rx_interval_name, required_min_rx_interval.total_microseconds(), *allocator);
    document->AddMember(detection_time_multiplier_name, detection_time_multiplier, *allocator);
}

void BfdJsonState::EncodeJsonDocument(rapidjson::Value* document, rapidjson::Value::AllocatorType *allocator) {
    document->SetObject();
    session_config.EncodeJsonDocument(document, allocator);

    std::string local_session_state_str = boost::lexical_cast<std::string>(bfd_local_state);
    rapidjson::Value local_session_state_val(local_session_state_str.c_str(), *allocator);
    document->AddMember(local_session_state_name, local_session_state_val, *allocator);

    std::string remote_session_state_str = boost::lexical_cast<std::string>(bfd_remote_state);
    rapidjson::Value remote_session_state_val(remote_session_state_str.c_str(), *allocator);
    document->AddMember(remote_session_state_name, remote_session_state_val, *allocator);

    document->AddMember(local_discriminator_name, local_discriminator, *allocator);
    document->AddMember(remote_discriminator_name, remote_discriminator, *allocator);
    document->AddMember(remote_min_rx_interval_name, remote_min_rx_interval.total_microseconds(), *allocator);
}

bool BfdJsonState::ParseFromJsonDocument(const rapidjson::Value& document) {
    if (!session_config.ParseFromJsonDocument(document))
        return false;

    if (document.IsObject() && document.HasMember(local_session_state_name) &&
            document[local_session_state_name].IsString()
            && document.HasMember(remote_session_state_name) && document[remote_session_state_name].IsString()
            && document.HasMember(remote_min_rx_interval_name) && document[remote_min_rx_interval_name].IsInt()
            && document.HasMember(local_discriminator_name) && document[local_discriminator_name].IsInt()
            && document.HasMember(remote_discriminator_name) && document[remote_discriminator_name].IsInt() ) {

        remote_min_rx_interval = boost::posix_time::microseconds(document[remote_min_rx_interval_name].GetInt());
        local_discriminator  = document[local_discriminator_name].GetInt();
        remote_discriminator = document[remote_discriminator_name].GetInt();
        if (!BFDStateFromString(document[local_session_state_name].GetString(), &bfd_local_state))
            return false;
        if (!BFDStateFromString(document[remote_session_state_name].GetString(), &bfd_remote_state))
            return false;

        return true;
    } else {
        return false;
    }
}


bool BfdJsonStateNotification::ParseFromJsonDocument(const rapidjson::Value& document) {
    if (document.IsObject() && document.HasMember(address_name) && document[address_name].IsString()
             && document.HasMember(session_state_name) && document[session_state_name].IsString() ) {
        boost::system::error_code ec;
        address = boost::asio::ip::address::from_string(document[address_name].GetString(), ec);
        if (ec)
            return false;

         if (!BFDStateFromString(document[session_state_name].GetString(), &state))
             return false;

         return true;
    } else {
        return false;
    }
}

void BfdJsonStateNotification::EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator) {
    document->SetObject();
    std::string address_str = address.to_string();
    rapidjson::Value address_val(address_str.c_str(), *allocator);
    document->AddMember(address_name, address_val, *allocator);

    std::string session_state_str = boost::lexical_cast<std::string>(state);
    rapidjson::Value session_state_val(session_state_str.c_str(), *allocator);
    document->AddMember(session_state_name, session_state_val, *allocator);
}

void BfdJsonStateNotificationList::EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator) {
    document->SetArray();
    for (std::vector<BfdJsonStateNotification>::iterator it = notifications.begin(); it != notifications.end(); ++it) {
        rapidjson::Value notification;
        it->EncodeJsonDocument(&notification, allocator);
        document->PushBack(notification, *allocator);
    }
}

bool BfdJsonStateNotificationList::ParseFromJsonDocument(const rapidjson::Value& document) {
    return false;
}


void BfdJsonStateMap::EncodeJsonDocument(rapidjson::Value* document, rapidjson::Value::AllocatorType* allocator) {
    document->SetObject();

    for (StateMap::iterator it = states.begin(); it != states.end(); ++it) {
        std::string address_str = it->first.to_string();
        rapidjson::Value address_val(address_str.c_str(), *allocator);

        std::string session_state_str = boost::lexical_cast<std::string>(it->second);
        rapidjson::Value session_state_val(session_state_str.c_str(), *allocator);

        document->AddMember(address_val, session_state_val, *allocator);
    }
}

bool BfdJsonStateMap::ParseFromJsonDocument(const rapidjson::Value& document) {
    states.clear();

    if (!document.IsObject())
        return false;
    for (rapidjson::Value::ConstMemberIterator it = document.MemberBegin(); it != document.MemberEnd(); ++it) {
        if (!it->name.IsString() || !it->value.IsString())
            return false;
        boost::system::error_code ec;
        boost::asio::ip::address address = boost::asio::ip::address::from_string(it->name.GetString(), ec);
        if (boost::system::errc::success != ec)
            return false;
        BFDState state;
        if (!BFDStateFromString(it->value.GetString(), &state))
           return false;
        states[address] = state;
    }
    return true;
}
};  // namespace BFD

