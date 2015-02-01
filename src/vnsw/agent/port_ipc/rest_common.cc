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
                  const std::string &msg, int status_code) {
    const std::string response =
        "HTTP/1.1 " + integerToString(status_code) + " OK\r\n"
        "Content-Type: application/json; charset=UTF-8\r\n"
        "Content-Length: " +
        integerToString(msg.length()) + "\r\n" "\r\n" + msg;
    session->Send((const u_int8_t *)(response.c_str()),
                  response.length(), NULL);
}

void SendErrorResponse(HttpSession *session,
                        const std::string &error_msg, int status_code) {
    rapidjson::Document document;
    document.SetObject();
    document.AddMember("error", error_msg.c_str(), document.GetAllocator());
    rapidjson::StringBuffer strbuf;
    rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
    document.Accept(writer);
    SendResponse(session, strbuf.GetString(), status_code);
}

}  // namespace REST
