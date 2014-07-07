/*
 * Copyright (c) 2014 Codilime.
 */

#ifndef SRC_BFD_REST_COMMON_
#define SRC_BFD_REST_COMMON_

namespace BFD {
namespace REST {

void SendResponse(HttpSession *session, const std::string &msg,
                  int status_code = 200);
void SendErrorResponse(HttpSession *session, const std::string &error_msg,
                        int status_code = 500);

}

#endif
