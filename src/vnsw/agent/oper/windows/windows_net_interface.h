/*
 * Copyright (c) 2018 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_oper_windows_net_interface_h
#define vnsw_agent_oper_windows_net_interface_h

#include <string>
#include <boost/optional.hpp>

#include <winsock2.h>
#include <Iphlpapi.h>

#include <oper/interface_os_params.h>

namespace WindowsNetworkInterface {

/* This function assumes that name will have the following format:
 *      Container NIC xxxxxxxx
 */
boost::optional<NET_LUID> GetVmInterfaceLuidFromName(const std::string& name);

/* This function assumes that name will be an `ifName` of the interface */
boost::optional<NET_LUID> GetPhysicalInterfaceLuidFromName(const std::string& name);

boost::optional<NET_IFINDEX> GetInterfaceIndexFromLuid(const NET_LUID intf_luid);

boost::optional<InterfaceOsParams> GetRealIdsFromOptionalLuid(boost::optional<NET_LUID> net_luid);

} // namespace WindowsNetworkInterface {

#endif // vnsw_agent_oper_windows_net_interface_h
