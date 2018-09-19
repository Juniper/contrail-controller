#include "../dns_proto.h"

#include <iphlpapi.h>
#include <cstdlib>
#pragma comment(lib, "IPHLPAPI.lib")

DnsProto::DefaultServerList DnsProto::BuildDefaultServerListImpl() {
    DefaultServerList ip_list;

    FIXED_INFO *dnsServersInfo = (FIXED_INFO *)std::malloc(sizeof(FIXED_INFO));
    ULONG dnsServersInfoBuffSize = sizeof(FIXED_INFO);

    //We have to call GetNetworkParams twice -
    //First, we need to get size of the buffer
    //which will hold the information about default DNS resolvers.
    if (GetNetworkParams(dnsServersInfo, &dnsServersInfoBuffSize) == ERROR_BUFFER_OVERFLOW) {
        dnsServersInfo = (FIXED_INFO *)std::realloc(dnsServersInfo, dnsServersInfoBuffSize);
        assert(dnsServersInfo != NULL);
    }

    //With second call we fill allocated memory with the data.
    DWORD dwRetVal = GetNetworkParams(dnsServersInfo, &dnsServersInfoBuffSize);
    assert(dwRetVal == NO_ERROR);

    IP_ADDR_STRING *dnsServerIPAddr = &dnsServersInfo->DnsServerList;
    while(dnsServerIPAddr) {
        boost::system::error_code ec;
        IpAddress ip = IpAddress::from_string(dnsServerIPAddr->IpAddress.String, ec);
        if (!ec.value()) {
            ip_list.push_back(ip);
        }
        dnsServerIPAddr = dnsServerIPAddr->Next;
    }

    if (dnsServersInfo) {
        std::free(dnsServersInfo);
    }

    return ip_list;
}