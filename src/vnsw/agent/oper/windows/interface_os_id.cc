#include <oper/interface.h>
#include <oper/vm_interface.h>

#include <iphlpapi.h>

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

InterfaceOsId::InterfaceOsId(const std::string& name) :
    name_(name) {
}

InetInterfaceOsId::InetInterfaceOsId(const std::string& name) :
    InterfaceOsId(name) {
}

boost::optional<Interface::IfGuid> InterfaceOsId::ObtainKernelIdentifier() {
    auto luid = GetVmInterfaceLuidFromName(name_);
    if (luid)
        return GetInterfaceGuidFromLuid(*luid);
    else
        return boost::none;
}

boost::optional<Interface::IfGuid> InetInterfaceOsId::ObtainKernelIdentifier() {
    auto luid = GetPhysicalInterfaceLuidFromName(name_);
    if (luid)
        return GetInterfaceGuidFromLuid(*luid);
    else
        return boost::none;
}
