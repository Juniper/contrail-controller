/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#include <sstream>
#include <base/logging.h>
#include <oper/windows/windows_net_interface.h>

namespace WindowsNetworkInterface {

/* This function assumes that name will have the following format:
        Container NIC xxxxxxxx
*/
boost::optional<NET_LUID> GetVmInterfaceLuidFromName(const std::string& name) {
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
boost::optional<NET_LUID> GetPhysicalInterfaceLuidFromName(const std::string& name) {
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

static boost::optional<InterfaceOsParams::IfGuid> GetInterfaceGuidFromLuid(const NET_LUID intf_luid) {
    GUID intf_guid;

    NETIO_STATUS status = ConvertInterfaceLuidToGuid(&intf_luid, &intf_guid);
    if (status != NO_ERROR) {
        LOG(ERROR, "ERROR: on converting LUID to GUID:" << status);
        return boost::none;
    }

    InterfaceOsParams::IfGuid result;
    memcpy(&result, &intf_guid, sizeof(intf_guid));

    return result;
}

boost::optional<NET_IFINDEX> GetInterfaceIndexFromLuid(const NET_LUID intf_luid) {
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
    const ULONG flags = GAA_FLAG_INCLUDE_PREFIX
                      | GAA_FLAG_INCLUDE_ALL_INTERFACES
                      | GAA_FLAG_INCLUDE_ALL_COMPARTMENTS;
    const ULONG family = AF_UNSPEC;
    const int MAX_RETRIES = 10;
    const int TIMEOUT = 100;

    DWORD ret;
    ULONG buffer_size = 0;
    int retries = 0;

    while (retries < MAX_RETRIES) {
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
            if (iter->Luid.Value == intf_luid.Value && iter->PhysicalAddressLength == 6) {
                return MacAddress(iter->PhysicalAddress[0],
                                  iter->PhysicalAddress[1],
                                  iter->PhysicalAddress[2],
                                  iter->PhysicalAddress[3],
                                  iter->PhysicalAddress[4],
                                  iter->PhysicalAddress[5]);
            }
            iter = iter->Next;
        }

        Sleep(TIMEOUT);
        ++retries;
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

boost::optional<InterfaceOsParams> GetRealIdsFromOptionalLuid(boost::optional<NET_LUID> net_luid) {
    InterfaceOsParams os_params;
    os_params.os_oper_state_ = false;

    if (!net_luid) {
        LOG(ERROR, "Error: empty LUID");
        return boost::none;
    }

    os_params.os_guid_ = GetInterfaceGuidFromLuid(*net_luid);

    auto os_index = GetInterfaceIndexFromLuid(*net_luid);
    if (!os_index) {
        LOG(ERROR, "Error: on querying os_index by LUID: LUID=" << LuidToString(*net_luid));
        return boost::none;
    }

    auto if_name = GetInterfaceNameFromLuid(*net_luid);
    if (!if_name) {
        LOG(ERROR, "Error: on querying if_name by LUID: LUID=" << LuidToString(*net_luid));
        return boost::none;
    }

    auto mac = GetMacAddressFromLuid(*net_luid);
    if (!mac) {
        LOG(ERROR, "Error: on querying MAC address by LUID: LUID=" << LuidToString(*net_luid));
        return boost::none;
    }

    os_params.os_index_ = *os_index;
    os_params.name_ = *if_name;
    os_params.mac_ = *mac;

    /* We assume that interface is UP */
    os_params.os_oper_state_ = true;

    return os_params;
}

} // namespace WindowsNetworkInterface
