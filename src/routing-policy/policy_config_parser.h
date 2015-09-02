/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef ctrlplane_policy_config_parser_h
#define ctrlplane_policy_config_parser_h
#include <string>
class PolicyConfigParser {
public:
    PolicyConfigParser();
    bool Parse(const std::string &content);
};
#endif //ctrlplane_policy_config_parser_h
