/*
 * Copyright (c) 2014 Codilime.
 */

#ifndef SRC_BFD_REST_COMMON_
#define SRC_BFD_REST_COMMON_

#include <string>

class HttpSession;

namespace BFD {
namespace REST {

void SendResponse(HttpSession *session, const std::string &msg,
                  int status_code = 200);
void SendErrorResponse(HttpSession *session, const std::string &error_msg,
                        int status_code = 500);

}  // namespace REST
}  // namespace BFD

#endif  // SRC_BFD_REST_COMMON_
