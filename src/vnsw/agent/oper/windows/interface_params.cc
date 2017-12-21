/*
 * Copyright (c) 2017 Juniper Networks, Inc. All rights reserved.
 */

#include <oper/interface.h>

#include <Iphlpapi.h>

/* This function assumes that name will have the following format:
        Container NIC xxxxxxxx
*/
static boost::optional<NET_LUID> GetVmInterfaceLuidFromName(const std::string& name) {
    std::stringstream ss;
    ss << "vEthernet (" << name << ")";

    const std::string alias = ss.str();

    const size_t mb_name_buffer_size = alias.length() + 1;
    wchar_t *mb_name = new wchar_t[mb_name_buffer_size];
    memset(mb_name, 0, sizeof(*mb_name) * mb_name_buffer_size);

    size_t converted_chars;
    errno_t status = mbstowcs_s(&converted_chars, mb_name, mb_name_buffer_size,
                                alias.c_str(), _TRUNCATE);
    if (status != 0) {
        LOG(ERROR, "on converting interface name to wchar: name='" << name << "'");
        assert(0);
    }

    const int MAX_RETRIES = 10;
    const int TIMEOUT = 1000;
    int retries = 0;
    while (retries < MAX_RETRIES) {
        NET_LUID intf_luid;
        NETIO_STATUS net_error = ConvertInterfaceAliasToLuid(mb_name, &intf_luid);
        if (net_error == NO_ERROR) {
            delete mb_name;
            return intf_luid;
        }

        Sleep(TIMEOUT);
        ++retries;
    }

    LOG(ERROR, "could not retrieve LUID for interface name='" << name << "'");
    return boost::none;
}

/* This function assumes that name will be an `ifName` of the interface */
static boost::optional<NET_LUID> GetPhysicalInterfaceLuidFromName(const std::string& name) {
    const int MAX_RETRIES = 10;
    const int TIMEOUT = 1000;
    int retries = 0;
    while (retries < MAX_RETRIES) {
        NET_LUID intf_luid;
        NETIO_STATUS net_error = ConvertInterfaceNameToLuidA(name.c_str(), &intf_luid);
        if (net_error == NO_ERROR)
            return intf_luid;

        Sleep(TIMEOUT);
        ++retries;
    }

    LOG(ERROR, "could not retrieve LUID for interface name='" << name << "'");
    return boost::none;
}

static boost::optional<NET_LUID> GetInterfaceLuidFromName(const std::string& name,
                                                   const Interface::Type intf_type) {
    if (intf_type == Interface::VM_INTERFACE) {
        return GetVmInterfaceLuidFromName(name);
    } else if (intf_type == Interface::PHYSICAL || intf_type == Interface::INET) {
        return GetPhysicalInterfaceLuidFromName(name);
    } else {
        LOG(ERROR, "ERROR: unsupported interface type interface=" << name
            << ", type = " << intf_type);
        return boost::none;
    }
}

static boost::optional<Interface::IfGuid> GetInterfaceGuidFromLuid(const NET_LUID intf_luid) {
    GUID intf_guid;

    NETIO_STATUS status = ConvertInterfaceLuidToGuid(&intf_luid, &intf_guid);
    if (status != NO_ERROR) {
        LOG(ERROR, "ERROR: on converting LUID to GUID:" << status);
        return boost::none;
    }

    Interface::IfGuid result;
    memcpy(&result, &intf_guid, sizeof(intf_guid));

    return result;
}

static boost::optional<NET_LUID> GetInterfaceLuidFromGuid(const Interface::IfGuid& intf_guid) {
    GUID win_guid;
    assert(sizeof(win_guid) == intf_guid.size());
    memcpy(&win_guid, &intf_guid, intf_guid.size());

    NET_LUID intf_luid;
    NETIO_STATUS status = ConvertInterfaceGuidToLuid(&win_guid, &intf_luid);
    if (status != NO_ERROR) {
        LOG(ERROR, "ERROR: on converting GUID to LUID:" << status);
        return boost::none;
    }

    return intf_luid;
}

static boost::optional<NET_IFINDEX> GetInterfaceIndexFromLuid(const NET_LUID intf_luid) {
    NET_IFINDEX intf_os_index;

    NETIO_STATUS status = ConvertInterfaceLuidToIndex(&intf_luid, &intf_os_index);
    if (status != NO_ERROR) {
        LOG(ERROR, "ERROR: on converting LUID to index: " << status);
        return boost::none;
    }

    return intf_os_index;
}

static boost::optional<std::string> GetInterfaceNameFromLuid(const NET_LUID intf_luid) {
    char if_name[1024] = { 0 };

    NETIO_STATUS status = ConvertInterfaceLuidToNameA(&intf_luid, if_name, sizeof(if_name));
    if (status != NO_ERROR) {
        LOG(ERROR, "on converting LUID to index: " << status);
        return boost::none;
    }

    return std::string(if_name);
}

static boost::optional<MacAddress> GetMacAddressFromLuid(const NET_LUID intf_luid) {
    DWORD ret;

    ULONG flags = GAA_FLAG_INCLUDE_PREFIX
                  | GAA_FLAG_INCLUDE_ALL_INTERFACES
                  | GAA_FLAG_INCLUDE_ALL_COMPARTMENTS;
    ULONG family = AF_UNSPEC;
    ULONG buffer_size = 0;

    ret = GetAdaptersAddresses(family, flags, NULL, NULL, &buffer_size);
    assert(ret == ERROR_BUFFER_OVERFLOW);

    PIP_ADAPTER_ADDRESSES adapter_addresses = (PIP_ADAPTER_ADDRESSES)malloc(buffer_size);
    ret = GetAdaptersAddresses(family, flags, NULL, adapter_addresses, &buffer_size);
    if (ret != ERROR_SUCCESS) {
        LOG(ERROR, "could not retrieve adapters information");
        return boost::none;
    }

    PIP_ADAPTER_ADDRESSES iter = adapter_addresses;
    while (iter != NULL) {
        if (iter->Luid.Value == intf_luid.Value) {
            assert(iter->PhysicalAddressLength == 6);
            return MacAddress(iter->PhysicalAddress[0],
                              iter->PhysicalAddress[1],
                              iter->PhysicalAddress[2],
                              iter->PhysicalAddress[3],
                              iter->PhysicalAddress[4],
                              iter->PhysicalAddress[5]);
        }

        iter = iter->Next;
    }

    LOG(ERROR, "mac address not found for LUID " << intf_luid.Value);
    return boost::none;
}

static std::string LuidToString(const NET_LUID intf_luid) {
    std::stringstream ss;

    ss << "<IfType = " << intf_luid.Info.IfType
       << ", NetLuidIndex = " << intf_luid.Info.NetLuidIndex
       << ", Value = " << intf_luid.Value << ">";

    return ss.str();
}

void Interface::GetOsSpecificParams(Agent *agent, const std::string &name) {
    /* In case of pkt0 interface, we assume that it is UP, set os_index to dummy value 0,
        since on Windows this parameter is not used because interfaces are represented by named pipes.
        Name and mac are set to constant values from agent specific for that interface */
    if (type_ == PACKET) {
        os_oper_state_ = true;
        os_index_ = 0;
        name_ = agent->pkt_interface_name();
        mac_ = agent->pkt_interface_mac();
        return;
    }

    /* Get interface's GUID. Should only happen on first call of `GetOsParams`. */
    if (!os_guid_) {
        auto net_luid = GetInterfaceLuidFromName(name, type_);
        if (!net_luid) {
            LOG(ERROR, "Error: on querying LUID by name: name=" << name);
            os_oper_state_ = false;
            return;
        }

        os_guid_ = GetInterfaceGuidFromLuid(*net_luid);
        if (!os_guid_) {
            LOG(ERROR, "Error: on querying GUID by LUID: LUID="
                << LuidToString(*net_luid));
            os_oper_state_ = false;
            return;
        }
    }

    auto net_luid = GetInterfaceLuidFromGuid(*os_guid_);
    if (!net_luid) {
        LOG(ERROR, "Error: on querying LUID by GUID: GUID=" << *os_guid_);
        os_oper_state_ = false;
        return;
    }

    auto os_index = GetInterfaceIndexFromLuid(*net_luid);
    if (!os_index) {
        LOG(ERROR, "Error: on querying os_index by GUID: GUID=" << *os_guid_
            << " LUID=" << LuidToString(*net_luid));
        os_oper_state_ = false;
        return;;
    }

    auto if_name = GetInterfaceNameFromLuid(*net_luid);
    if (!if_name) {
        LOG(ERROR, "Error: on querying if_name by GUID: GUID=" << *os_guid_
            << " LUID=" << LuidToString(*net_luid));
        os_oper_state_ = false;
        return;
    }

    auto mac = GetMacAddressFromLuid(*net_luid);
    if (!mac) {
        LOG(ERROR, "Error: on querying MAC address by GUID: GUID=" << *os_guid_
            << " LUID=" << LuidToString(*net_luid));
        os_oper_state_ = false;
        return;
    }

    os_index_ = *os_index;
    name_ = *if_name;
    mac_ = *mac;

    /* We assume that interface is UP */
    os_oper_state_ = true;
}
