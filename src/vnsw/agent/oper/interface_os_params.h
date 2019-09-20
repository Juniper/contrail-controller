/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_interface_os_params_h
#define vnsw_agent_interface_os_params_h

#include <string>
#include <boost/optional.hpp>
#include <boost/uuid/uuid.hpp>
#include <net/mac_address.h>

struct InterfaceOsParams {
    typedef boost::uuids::uuid IfGuid;

    InterfaceOsParams() {}
    InterfaceOsParams(const std::string &name, size_t os_index, bool state) :
        name_(name), os_index_(os_index), os_oper_state_(state) {}

    std::string name_;
    MacAddress mac_;
    size_t os_index_;
    bool os_oper_state_;
    boost::optional<IfGuid> os_guid_;
};

#endif // vnsw_agent_interface_os_params_h
