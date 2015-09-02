/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef _ROOT_REST_COMMON_
#define _ROOT_REST_COMMON_

#include <string>

class HttpSession;

namespace REST {

void SendResponse(HttpSession *session, const std::string &msg,
                  int status_code = 200);
void SendErrorResponse(HttpSession *session, const std::string &error_msg,
                       int status_code = 500);

}  // namespace REST

#endif  // _ROOT_REST_COMMON_
