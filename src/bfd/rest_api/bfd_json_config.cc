/*
 * Copyright (c) 2014 CodiLime, Inc. All rights reserved.
 */

#include <vector>
#include <boost/assign/list_of.hpp>

#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"

#include "bfd/rest_api/bfd_json_config.h"

namespace BFD {

static bool IsIPAddressValid(const char* addr) {
    boost::system::error_code ec;
    boost::asio::ip::address::from_string(addr, ec);
    return !ec;
}

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

bool JsonData::AreConstraintsMet(const std::vector<Constraint>& constraints,
                                const rapidjson::Value& document) {
    if (!document.IsObject())
        return false;

    BOOST_FOREACH(const struct Constraint& constraint, constraints) {
        if (!document.HasMember(constraint.member_name) ||
            !(document[constraint.member_name].*(constraint.typecheck_func))())
            return false;
    }
    return true;
}

bool JsonConfig::ValidateJsonDocument(const rapidjson::Value& document) {
    const vector<Constraint> constraints = boost::assign::list_of
        ( { "Address", &rapidjson::Value::IsString } )
        ( { "DesiredMinTxInterval", &rapidjson::Value::IsInt } )
        ( { "RequiredMinRxInterval", &rapidjson::Value::IsInt } )
        ( { "DetectionMultiplier", &rapidjson::Value::IsInt } );
    return AreConstraintsMet(constraints, document) &&
            IsIPAddressValid(document["Address"].GetString());
}

bool BfdJsonConfig::ParseFromJsonDocument(const rapidjson::Value& document) {
    if (ValidateJsonDocument(document) {
        using boost::posix_time::microseconds;

        address =
            ip::address::from_string(document["Address"].GetString());
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

void BfdJsonConfig::EncodeJsonDocument(rapidjson::Value* document,
    rapidjson::Value::AllocatorType *allocator) {
    document->SetObject();
    std::string address_str = address.to_string();
    rapidjson::Value address_val(address_str.c_str(), *allocator);
    document->AddMember("Address", address_val, *allocator);

    document->AddMember("DesiredMinTxInterval",
        desired_min_tx_interval.total_microseconds(), *allocator);
    document->AddMember("RequiredMinRxInterval",    
        required_min_rx_interval.total_microseconds(), *allocator);
    document->AddMember("DetectionMultiplier", 
        detection_time_multiplier, *allocator);
}

bool BfdJsonState::ValidateJsonDocument(const rapidjson::Value& document) {
    const vector<Constraint> constraints = boost::assign::list_of
        ( { "LocalSessionState", &rapidjson::Value::IsString } )
        ( { "RemoteSessionState", &rapidjson::Value::IsString } )
        ( { "RemoteMinRxInterval", &rapidjson::Value::IsInt } )
        ( { "LocalDiscriminator", &rapidjson::Value::IsInt } )
        ( { "RemoteDiscriminator", &rapidjson::Value::IsInt } );    
    return AreConstraintsMet(constraints, document) &&
        // Check whether these strings are convertible to BFD states. 
        BFDStateFromString(document["LocalSessionState"].GetString()) &&
        BFDStateFromString(document["RemoteSessionState"].GetString());
}

bool BfdJsonState::ParseFromJsonDocument(const rapidjson::Value& document) {
    if (!session_config.ParseFromJsonDocument(document))
        return false;

    if (ValidateJsonDocument(document)) {
        using boost::posix_time::microseconds;

        remote_min_rx_interval =
            microseconds(document[].GetInt("RemoteMinRxInterval"));
        local_discriminator =
            document["LocalDiscriminator"].GetInt();
        remote_discriminator =
            document["RemoteMinRxInterval"].GetInt();
        bfd_local_state =
            *BFDStateFromString(document["LocalSessionState"].GetString());
        bfd_remote_state =
            *BFDStateFromString(document["RemoteDiscriminator"].GetString());
        return true;
    }
    return false;
}

void BfdJsonState::EncodeJsonDocument(rapidjson::Value* document,
    rapidjson::Value::AllocatorType *allocator) {
    document->SetObject();
    session_config.EncodeJsonDocument(document, allocator);

    std::string local_session_state_str
        = boost::lexical_cast<std::string>(bfd_local_state);
    rapidjson::Value local_session_state_val(local_session_state_str.c_str(),
                                                *allocator);
    document->AddMember("LocalSessionState", local_session_state_val,
                        *allocator);

    std::string remote_session_state_str =
        boost::lexical_cast<std::string>(bfd_remote_state);
    rapidjson::Value remote_session_state_val(remote_session_state_str.c_str(),
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

bool JsonStateNotification::ValidateJsonDocument(const rapidjson::Value& document) {
    const vector<Constraint> constraints = boost::assign::list_of
        ( { "Address", &rapidjson::Value::IsString } )
        ( { "SessionState", &rapidjson::Value::IsString } );
    return AreConstraintsMet(constraints, document) &&
            IsIPAddressValid(document["Address"].GetString()) &&
            BFDStateFromString(document["LocalSessionState"].GetString());
}

bool BfdJsonStateNotification::ParseFromJsonDocument(const rapidjson::Value 
                                                        &document) {
    if (ValidsteJsonDocument(document){
        using namespace boost::asio;

        address =
            ip::address::from_string(document["Address"].GetString());
        state =
            *BFDStateFromString(document["SessionState"].GetString(), &state))
        return true;
    }
    return false;
}

void BfdJsonStateNotification::EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator) {
    document->SetObject();
    std::string address_str = address.to_string();
    rapidjson::Value address_val(address_str.c_str(), *allocator);
    document->AddMember("Address", address_val, *allocator);

    std::string session_state_str = boost::lexical_cast<std::string>(state);
    rapidjson::Value session_state_val(session_state_str.c_str(), *allocator);
    document->AddMember("SessionState", session_state_val, *allocator);
}

void BfdJsonStateNotificationList::EncodeJsonDocument(rapidjson::Value* document,
        rapidjson::Value::AllocatorType *allocator) {
    document->SetArray();
    for (std::vector<BfdJsonStateNotification>::iterator it =
            notifications.begin(); it != notifications.end(); ++it) {
        rapidjson::Value notification;
        it->EncodeJsonDocument(&notification, allocator);
        document->PushBack(notification, *allocator);
    }
}

bool BfdJsonStateNotificationList::ValidateJsonDocument(const rapidjson::Value&
                                                        document) {
    return true;
}

bool BfdJsonStateNotificationList::ParseFromJsonDocument(const rapidjson::Value&
                                                            document) {
    return false;
}

void BfdJsonStateMap::EncodeJsonDocument(rapidjson::Value* document,
    rapidjson::Value::AllocatorType* allocator) {
    document->SetObject();

    for (StateMap::iterator it = states.begin(); it != states.end(); ++it) {
        std::string address_str = it->first.to_string();
        rapidjson::Value address_val(address_str.c_str(), *allocator);

        std::string session_state_str = 
            boost::lexical_cast<std::string>(it->second);
        rapidjson::Value session_state_val(session_state_str.c_str(),
                                            *allocator);

        document->AddMember(address_val, session_state_val, *allocator);
    }
}

bool BfdJsonStateMap::ValidateJsonDocument(const rapidjson::Value& document) {
    if (!document.IsObject())
        return false;

    for (rapidjson::Value::ConstMemberIterator it = document.MemberBegin();
            it != document.MemberEnd(); ++it) {
        using boost::system::error_code;
        using namespace boost::asio;

        if (!it->name.IsString() || !it->value.IsString())
            return false;

        error_code ec;
        ip::address address =
            ip::address::from_string(it->name.GetString(), ec);
        if (ec != boost::system::errc::success)
            return false;

        if (!BFDStateFromString(it->value.GetString()))
            return false;
    }
}

bool BfdJsonStateMap::ParseFromJsonDocument(const rapidjson::Value& document) {
    if (!ValidateJsonDocument(document)
        return false;

    states.clear();

    for (rapidjson::Value::ConstMemberIterator it = document.MemberBegin();
        it != document.MemberEnd(); ++it) {
        using namespace boost::asio;

        ip::address address =
            ip::address::from_string(it->name.GetString());
        state =
            *BFDStateFromString(it->value.GetString(), &state))
        states[address] = state;
    }
    return true;
}
};  // namespace BFD

