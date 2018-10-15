/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include <vector>
#include <boost/foreach.hpp>
#include <boost/assign/list_of.hpp>
#include "base/address_util.h"

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "bfd/rest_api/bfd_json_config.h"

namespace BFD {
namespace REST {

static bool IsIPAddressValid(const char* addr) {
    boost::system::error_code ec;
    AddressFromString(addr, &ec);
    return !ec;
}

bool JsonData::ParseFromJsonString(const std::string& json) {
    contrail_rapidjson::Document document;
    document.Parse<0>(json.c_str());
    return ParseFromJsonDocument(document);
}

void JsonData::EncodeJsonString(std::string* json) {
    contrail_rapidjson::Document document;
    EncodeJsonDocument(&document, &document.GetAllocator());
    contrail_rapidjson::StringBuffer strbuf;
    contrail_rapidjson::Writer<contrail_rapidjson::StringBuffer> writer(strbuf);
    document.Accept(writer);
    *json = strbuf.GetString();
}

bool JsonData::AreConstraintsMet(const std::vector<Constraint>& constraints,
                                const contrail_rapidjson::Value& document) {
    if (!document.IsObject())
        return false;

    BOOST_FOREACH(const struct Constraint& constraint, constraints) {
        if (!document.HasMember(constraint.member_name) ||
            !(document[constraint.member_name].*(constraint.typecheck_func))())
            return false;
    }
    return true;
}

JsonConfig::JsonConfig()
{}

JsonConfig::JsonConfig(boost::asio::ip::address address,
                       TimeInterval desired_min_tx_interval,
                       TimeInterval required_min_rx_interval,
                       int detection_time_multiplier)
                       : address(address),
                         desired_min_tx_interval(desired_min_tx_interval),
                         required_min_rx_interval(required_min_rx_interval),
                         detection_time_multiplier(detection_time_multiplier)
                       {}

bool JsonConfig::ValidateJsonDocument(const contrail_rapidjson::Value& document) {
    static const std::vector<Constraint> constraints = boost::assign::list_of
        (Constraint("Address", &contrail_rapidjson::Value::IsString) )
        (Constraint("DesiredMinTxInterval", &contrail_rapidjson::Value::IsInt))
        (Constraint("RequiredMinRxInterval", &contrail_rapidjson::Value::IsInt))
        (Constraint("DetectionMultiplier", &contrail_rapidjson::Value::IsInt));
    return AreConstraintsMet(constraints, document) &&
            IsIPAddressValid(document["Address"].GetString());
}

bool JsonConfig::ParseFromJsonDocument(const contrail_rapidjson::Value& document) {
    if (ValidateJsonDocument(document)) {
        using boost::posix_time::microseconds;

        boost::system::error_code ec;
        address = AddressFromString(document["Address"].GetString(), &ec);
        desired_min_tx_interval =
            microseconds(document["DesiredMinTxInterval"].GetInt());
        required_min_rx_interval =
            microseconds(document["RequiredMinRxInterval"].GetInt());
        detection_time_multiplier =
            document["DetectionMultiplier"].GetInt();
        return true;
    }
    return false;
}

void JsonConfig::EncodeJsonDocument(contrail_rapidjson::Value* document,
                                contrail_rapidjson::Value::AllocatorType *allocator) {
    document->SetObject();
    std::string address_str = address.to_string();
    contrail_rapidjson::Value address_val(address_str.c_str(), *allocator);
    document->AddMember("Address", address_val, *allocator);

    document->AddMember("DesiredMinTxInterval",
        desired_min_tx_interval.total_microseconds(), *allocator);
    document->AddMember("RequiredMinRxInterval",
        required_min_rx_interval.total_microseconds(), *allocator);
    document->AddMember("DetectionMultiplier",
        detection_time_multiplier, *allocator);
}

bool JsonState::ValidateJsonDocument(const contrail_rapidjson::Value& document) {
    const std::vector<Constraint> constraints = boost::assign::list_of
        (Constraint("LocalSessionState", &contrail_rapidjson::Value::IsString))
        (Constraint("RemoteSessionState", &contrail_rapidjson::Value::IsString))
        (Constraint("RemoteMinRxInterval", &contrail_rapidjson::Value::IsInt))
        (Constraint("LocalDiscriminator", &contrail_rapidjson::Value::IsInt))
        (Constraint("RemoteDiscriminator", &contrail_rapidjson::Value::IsInt));
    return AreConstraintsMet(constraints, document) &&
        // Check whether these strings are convertible to BFD states.
        BFDStateFromString(document["LocalSessionState"].GetString()) &&
        BFDStateFromString(document["RemoteSessionState"].GetString());
}

bool JsonState::ParseFromJsonDocument(const contrail_rapidjson::Value& document) {
    if (!session_config.ParseFromJsonDocument(document))
        return false;

    if (ValidateJsonDocument(document)) {
        using boost::posix_time::microseconds;

        remote_min_rx_interval =
            microseconds(document["RemoteMinRxInterval"].GetInt());
        local_discriminator =
            document["LocalDiscriminator"].GetInt();
        remote_discriminator =
            document["RemoteDiscriminator"].GetInt();
        bfd_local_state =
            *BFDStateFromString(document["LocalSessionState"].GetString());
        bfd_remote_state =
            *BFDStateFromString(document["RemoteSessionState"].GetString());
        return true;
    }
    return false;
}

void JsonState::EncodeJsonDocument(contrail_rapidjson::Value* document,
    contrail_rapidjson::Value::AllocatorType *allocator) {
    document->SetObject();
    session_config.EncodeJsonDocument(document, allocator);

    std::string local_session_state_str
        = boost::lexical_cast<std::string>(bfd_local_state);
    contrail_rapidjson::Value local_session_state_val(local_session_state_str.c_str(),
                                                *allocator);
    document->AddMember("LocalSessionState", local_session_state_val,
                        *allocator);

    std::string remote_session_state_str =
        boost::lexical_cast<std::string>(bfd_remote_state);
    contrail_rapidjson::Value remote_session_state_val(remote_session_state_str.c_str(),
                                                *allocator);
    document->AddMember("RemoteSessionState", remote_session_state_val,
                        *allocator);
    document->AddMember("LocalDiscriminator", local_discriminator,
                        *allocator);
    document->AddMember("RemoteDiscriminator", remote_discriminator,
                        *allocator);
    document->AddMember("RemoteMinRxInterval",
                        remote_min_rx_interval.total_microseconds(),
                        *allocator);
}

bool JsonStateNotification::ValidateJsonDocument(
                                    const contrail_rapidjson::Value& document) {
    const std::vector<Constraint> constraints = boost::assign::list_of
        (Constraint("Address", &contrail_rapidjson::Value::IsString))
        (Constraint("SessionState", &contrail_rapidjson::Value::IsString));
    return AreConstraintsMet(constraints, document) &&
            IsIPAddressValid(document["Address"].GetString()) &&
            BFDStateFromString(document["SessionState"].GetString());
}

bool JsonStateNotification::ParseFromJsonDocument(const contrail_rapidjson::Value
                                                        &document) {
    if (ValidateJsonDocument(document)) {

        boost::system::error_code ec;
        address = AddressFromString(document["Address"].GetString(), &ec);
        state =
            *BFDStateFromString(document["SessionState"].GetString());
        return true;
    }
    return false;
}

void JsonStateNotification::EncodeJsonDocument(contrail_rapidjson::Value* document,
        contrail_rapidjson::Value::AllocatorType *allocator) {
    document->SetObject();
    std::string address_str = address.to_string();
    contrail_rapidjson::Value address_val(address_str.c_str(), *allocator);
    document->AddMember("Address", address_val, *allocator);

    std::string session_state_str = boost::lexical_cast<std::string>(state);
    contrail_rapidjson::Value session_state_val(session_state_str.c_str(), *allocator);
    document->AddMember("SessionState", session_state_val, *allocator);
}

void JsonStateNotificationList::EncodeJsonDocument(contrail_rapidjson::Value* document,
        contrail_rapidjson::Value::AllocatorType *allocator) {
    document->SetArray();
    for (std::vector<JsonStateNotification>::iterator it =
            notifications.begin(); it != notifications.end(); ++it) {
        contrail_rapidjson::Value notification;
        it->EncodeJsonDocument(&notification, allocator);
        document->PushBack(notification, *allocator);
    }
}

bool JsonStateNotificationList::ValidateJsonDocument(
                    const contrail_rapidjson::Value& document) {
    return true;
}

bool JsonStateNotificationList::ParseFromJsonDocument(
                    const contrail_rapidjson::Value& document) {
    return false;
}

void JsonStateMap::EncodeJsonDocument(contrail_rapidjson::Value* document,
    contrail_rapidjson::Value::AllocatorType* allocator) {
    document->SetObject();

    for (StateMap::iterator it = states.begin(); it != states.end(); ++it) {
        std::string address_str = it->first.to_string();
        contrail_rapidjson::Value address_val(address_str.c_str(), *allocator);

        std::string session_state_str =
            boost::lexical_cast<std::string>(it->second);
        contrail_rapidjson::Value session_state_val(session_state_str.c_str(),
                                            *allocator);

        document->AddMember(address_val, session_state_val, *allocator);
    }
}

bool JsonStateMap::ValidateJsonDocument(const contrail_rapidjson::Value& document) {
    if (!document.IsObject())
        return false;

    for (contrail_rapidjson::Value::ConstMemberIterator it = document.MemberBegin();
            it != document.MemberEnd(); ++it) {
        using boost::system::error_code;

        if (!it->name.IsString() || !it->value.IsString())
            return false;

        error_code ec;
        if (ec != boost::system::errc::success)
            return false;
        if (!BFDStateFromString(it->value.GetString()))
            return false;
    }
    return true;
}

bool JsonStateMap::ParseFromJsonDocument(
        const contrail_rapidjson::Value& document) {
    if (!ValidateJsonDocument(document))
        return false;

    states.clear();

    for (contrail_rapidjson::Value::ConstMemberIterator
            it = document.MemberBegin(); it != document.MemberEnd(); ++it) {
        boost::system::error_code ec;
        boost::asio::ip::address address =
            AddressFromString(it->name.GetString(), &ec);
        BFDState state = *BFDStateFromString(it->value.GetString());
        states[address] = state;
    }
    return true;
}

}  // namespace REST
}  // namespace BFD

