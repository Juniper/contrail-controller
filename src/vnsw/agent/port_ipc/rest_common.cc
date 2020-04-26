/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#include <rapidjson/document.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include <http/http_session.h>
#include <base/string_util.h>
#include "rest_common.h"

namespace REST {

void SendResponse(HttpSession *session,
                  const std::string &msg, int status_code, std::string context) {
    const std::string response =
        "HTTP/1.1 " + integerToString(status_code) + " OK\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n"
        "Content-Length: " +
        integerToString(msg.length()) + "\r\n" "\r\n" + msg;
    if (context.empty()) {
    session->Send((const uint8_t *)(response.c_str()),
                  response.length(), NULL);
    } else {
    session->SendSession(context, (const uint8_t *)(response.c_str()),
                  response.length(), NULL);
    }
}

void SendErrorResponse(HttpSession *session,
                        const std::string &error_msg, int status_code, std::string context) {
    contrail_rapidjson::Document document;
    document.SetObject();
    contrail_rapidjson::Value v;
    document.AddMember("error",
                       v.SetString(error_msg.c_str(), document.GetAllocator()),
                       document.GetAllocator());
    contrail_rapidjson::StringBuffer strbuf;
    contrail_rapidjson::Writer<contrail_rapidjson::StringBuffer> writer(strbuf);
    document.Accept(writer);
    SendResponse(session, strbuf.GetString(), status_code, context);
}

}  // namespace REST
