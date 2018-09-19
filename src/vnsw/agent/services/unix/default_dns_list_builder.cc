#include "../dns_proto.h"
#include <fstream>

DnsProto::DefaultServerList DnsProto::BuildDefaultServerListImpl() {
    DefaultServerList ip_list;

    std::ifstream fd;
    fd.open("/etc/resolv.conf");
    if (!fd.is_open()) {
        return ip_list;
    }

    std::string line;
    while (getline(fd, line)) {
        std::size_t pos = line.find_first_of("#");
        std::stringstream ss(line.substr(0, pos));
        std::string key;
        ss >> key;
        if (key == "nameserver") {
            std::string ip_str;
            ss >> ip_str;
            boost::system::error_code ec;
            IpAddress ip = IpAddress::from_string(ip_str, ec);
            if (!ec.value()) {
                ip_list.push_back(ip);
            }
        }
    }

    fd.close();

    return ip_list;
}