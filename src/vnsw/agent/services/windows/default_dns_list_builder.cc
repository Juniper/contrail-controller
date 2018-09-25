#include "../dns_proto.h"

#include <iphlpapi.h>
#include <cstdlib>
#include "base/logging.h"

DnsProto::DefaultServerList DnsProto::BuildDefaultServerListImpl() {
    DefaultServerList ip_list{};

    FIXED_INFO *dnsServersInfo = NULL;
    ULONG dnsServersInfoBuffSize = 0;

    //We have to call GetNetworkParams twice -
    //First, we need to get size of the buffer
    //which will hold the information about default DNS resolvers.
    DWORD dwRetVal = GetNetworkParams(dnsServersInfo, &dnsServersInfoBuffSize);
    if (dwRetVal != ERROR_BUFFER_OVERFLOW) {
        //We had different error than ERROR_BUFFER_OVERFLOW.
        //It shouldn't happen so return empty list of ips.
        LOG(ERROR, "Error while getting required size of dnsServersInfo buffer: " << GetFormattedWindowsErrorMsg());
        return ip_list;
    }

    dnsServersInfo = (FIXED_INFO *)std::malloc(dnsServersInfoBuffSize);
    if (!dnsServersInfo) {
        LOG(ERROR, "Allocation of DNS servers information buffer failed");
        return ip_list;
    }

    //With second call we fill allocated memory with the data.
    dwRetVal = GetNetworkParams(dnsServersInfo, &dnsServersInfoBuffSize);
    if (dwRetVal != NO_ERROR) {
        LOG(ERROR, "Error while filling dnsServerInfo buffer: " << GetFormattedWindowsErrorMsg());
        return ip_list;
    }

    PIP_ADDR_STRING dnsServerIPAddr = &dnsServersInfo->DnsServerList;
    while(dnsServerIPAddr) {
        boost::system::error_code ec;
        IpAddress ip = IpAddress::from_string(dnsServerIPAddr->IpAddress.String, ec);
        if (!ec.value()) {
            ip_list.push_back(ip);
        }
        dnsServerIPAddr = dnsServerIPAddr->Next;
    }
    std::free(dnsServersInfo);

    return ip_list;
}