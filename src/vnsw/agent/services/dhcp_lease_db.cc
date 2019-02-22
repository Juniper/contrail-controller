
#include <pugixml/pugixml.hpp>
#include <boost/filesystem.hpp>
#include "base/address_util.h"
#include "base/timer.h"
#include "cmn/agent_cmn.h"
#include "init/agent_param.h"
#include "services/services_types.h"
#include "services/services_init.h"
#include "services/dhcp_lease_db.h"
#include "services/dhcp_proto.h"

using namespace pugi;

DhcpLeaseDb::DhcpLeaseDb(const Ip4Address &subnet, uint8_t plen,
                         const std::vector<Ip4Address> &reserve_addresses,
                         const std::string &lease_filename,
                         boost::asio::io_service &io) :
    subnet_(subnet), plen_(plen),
    max_lease_update_count_(0), lease_update_count_(0),
    lease_timeout_(kDhcpLeaseTimer), lease_filename_(lease_filename) {
    ReserveAddresses(reserve_addresses, true);
    LoadLeaseFile();
    timer_ = TimerManager::CreateTimer(io, "DhcpLeaseTimer",
                                       TaskScheduler::GetInstance()->
                                       GetTaskId("Agent::Services"),
                                       PktHandler::DHCP);
    timer_->Start(lease_timeout_,
                  boost::bind(&DhcpLeaseDb::LeaseTimerExpiry, this));
}

DhcpLeaseDb::~DhcpLeaseDb() {
    lease_bitmap_.clear();
    released_lease_bitmap_.clear();
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
    // remove(lease_filename_.c_str());
}

void DhcpLeaseDb::Update(const Ip4Address &subnet, uint8_t plen,
                         const std::vector<Ip4Address> &reserve_addresses) {
    // TODO: in case we update after allocating addresses from earlier subnet,
    // they will be ignored; no trace of that in the agent after this update;
    bool subnet_change = false;
    if (subnet != subnet_ || plen != plen_) {
        subnet_ = subnet;
        plen_ = plen;
        leases_.clear();
        lease_bitmap_.clear();
        released_lease_bitmap_.clear();
        remove(lease_filename_.c_str());
        subnet_change = true;
    }
    ReserveAddresses(reserve_addresses, subnet_change);
}

bool DhcpLeaseDb::Allocate(const MacAddress &mac, Ip4Address *ip,
                           uint64_t lease) {
    size_t index;
    uint64_t expiry = ClockMonotonicUsec() + (lease * 1000000);

    std::set<DhcpLease>::iterator it =
        leases_.find(DhcpLease(mac, Ip4Address(), 0, false));
    if (it != leases_.end()) {
        index = AddressToIndex(it->ip_);
        if (!IsReservedAddress(it->ip_) &&
            (!it->released_ || released_lease_bitmap_[index])) {
            *ip = it->ip_;
            lease_update_count_++;
            UpdateLease(mac, *ip, expiry, false);
            PersistLeaseRecord(mac, *ip, expiry, false);
            return true;
        } else {
            // A reserved address was leased earlier or the lease has been
            // given to a different client, cannot give the same
            // lease now; release it and allocate newly
            leases_.erase(it);
        }
    }

    index = lease_bitmap_.find_first();
    if (index == lease_bitmap_.npos) {
        index = released_lease_bitmap_.find_first();
        if (index == released_lease_bitmap_.npos) {
            DHCP_TRACE(Error, "DHCP Lease not available for MAC " <<
                       mac.ToString() << " in Subnet " <<
                       subnet_.to_string());
            return false;
        }
    }

    IndexToAddress(index, ip);
    UpdateLease(mac, *ip, expiry, false);
    PersistLeaseRecord(mac, *ip, expiry, false);
    return true;
}

bool DhcpLeaseDb::Release(const MacAddress &mac) {
    std::set<DhcpLease>::const_iterator it =
        leases_.find(DhcpLease(mac, Ip4Address(), 0, false));
    if (it != leases_.end()) {
        lease_update_count_++;
        UpdateLease(mac, it->ip_, it->lease_expiry_time_, true);
        PersistLeaseRecord(mac, it->ip_, it->lease_expiry_time_, true);
        return true;
    }

    return false;
}

bool DhcpLeaseDb::LeaseTimerExpiry() {
    uint64_t current_time = ClockMonotonicUsec();

    std::vector<DhcpLease> changed_leases;
    for (std::set<DhcpLease>::iterator it = leases_.begin();
         it != leases_.end(); ) {
        size_t index = AddressToIndex(it->ip_);
        if (it->released_ && !released_lease_bitmap_[index]) {
            // lease is not valid and address is re-allocated; remove the lease
            DHCP_TRACE(Trace, "DHCP Lease removed : " << it->mac_.ToString() <<
                       it->ip_.to_string());
            leases_.erase(it++);
            continue;
        }
        if (current_time > it->lease_expiry_time_) {
            it->released_ = true;
            released_lease_bitmap_[index] = 1;
            changed_leases.push_back(*it);
        }
        it++;
    }

    lease_update_count_ += changed_leases.size();
    if (lease_update_count_ >= max_lease_update_count_) {
        // Re-create the lease file, so that it is compacted
        CreateLeaseFile();
        lease_update_count_ = 0;
    } else if (changed_leases.size()) {
        PersistLeaseRecords(changed_leases);
    }

    return true;
}

void DhcpLeaseDb::UpdateLease(const MacAddress &mac, const Ip4Address &ip,
                              uint64_t expiry, bool released) {
    size_t index = AddressToIndex(ip);
    lease_bitmap_[index] = 0;
    released_lease_bitmap_[index] = (released) ? 1 : 0;
    std::set<DhcpLease>::iterator it =
        leases_.find(DhcpLease(mac, Ip4Address(), 0, false));
    if (it != leases_.end()) {
        it->ip_ = ip;
        it->lease_expiry_time_ = expiry;
        it->released_ = released;
    } else {
        leases_.insert(DhcpLease(mac, ip, expiry, released));
    }
    DHCP_TRACE(Trace, "DHCP Lease : " << mac.ToString() << " " <<
               ip.to_string() << " " << expiry << " " <<
               (released ? "released" : "valid"));
}

// block the reserved addresses
void DhcpLeaseDb::ReserveAddresses(const std::vector<Ip4Address> &addresses,
                                   bool subnet_change) {
    if (subnet_change) {
        uint32_t num_bits = 1 << (32 - plen_);
        if (num_bits < (2 + addresses.size()))
            return;

        lease_bitmap_.resize(num_bits, 1);
        lease_bitmap_[0] = 0;
        lease_bitmap_[(1 << (32 - plen_)) - 1] = 0;
        released_lease_bitmap_.resize(num_bits, 0);
        max_lease_update_count_ = (num_bits < 256) ? 256 :
                                  ((num_bits > 8192) ? 8192 : num_bits);
    }
    for (std::vector<Ip4Address>::const_iterator it = addresses.begin();
         it != addresses.end(); ++it) {
        if (IsIp4SubnetMember(*it, subnet_, plen_)) {
            size_t index = AddressToIndex(*it);
            lease_bitmap_[index] = 0;
            released_lease_bitmap_[index] = 0;
        }
    }
    reserve_addresses_ = addresses;
}

void DhcpLeaseDb::IndexToAddress(size_t index, Ip4Address *address) const {
    Ip4Address ip(subnet_.to_ulong() + index);
    *address = ip;
}

size_t DhcpLeaseDb::AddressToIndex(const Ip4Address &address) const {
    return address.to_ulong() & (0xFFFFFFFF >> plen_);
}

bool DhcpLeaseDb::IsReservedAddress(const Ip4Address &address) const {
    for (std::vector<Ip4Address>::const_iterator it =
         reserve_addresses_.begin(); it != reserve_addresses_.end(); ++it) {
        if (address == *it)
            return true;
    }
    return false;
}

// Added for UT
void DhcpLeaseDb::ClearLeases() {
    // clear the lease bitmaps and leases
    leases_.clear();
    lease_bitmap_.set();
    released_lease_bitmap_.reset();
    ReserveAddresses(reserve_addresses_, true);
}

// Added for UT
void DhcpLeaseDb::set_lease_timeout(uint32_t timeout) {
    if (lease_timeout_ != timeout) {
        lease_timeout_ = timeout;
        timer_->Cancel();
        timer_->Start(lease_timeout_,
                      boost::bind(&DhcpLeaseDb::LeaseTimerExpiry, this));
    }
}

// Write the complete lease file
void DhcpLeaseDb::CreateLeaseFile() {
    std::ofstream lease_ofstream;
    lease_ofstream.open(lease_filename_.c_str(),
                        std::ofstream::out | std::ofstream::trunc);
    if (!lease_ofstream.good()) {
        lease_ofstream.close();
        DHCP_TRACE(Error, "Cannot create DHCP Lease file: " << lease_filename_);
        return;
    }

    for (std::set<DhcpLease>::const_iterator it = leases_.begin();
         it != leases_.end(); ++it) {
        WriteLeaseRecord(lease_ofstream, it->mac_, it->ip_,
                         it->lease_expiry_time_, it->released_);
    }

    lease_ofstream.flush();
    lease_ofstream.close();
}

// Append lease record
void DhcpLeaseDb::PersistLeaseRecord(const MacAddress &mac,
                                     const Ip4Address &ip,
                                     const uint64_t &expiry,
                                     bool released) {
    std::ofstream lease_ofstream;
    lease_ofstream.open(lease_filename_.c_str(), std::ofstream::app);
    if (!lease_ofstream.good()) {
        lease_ofstream.close();
        DHCP_TRACE(Error, "Cannot open DHCP Lease file for writing : " <<
                   lease_filename_);
        return;
    }

    WriteLeaseRecord(lease_ofstream, mac, ip, expiry, released);

    lease_ofstream.flush();
    lease_ofstream.close();
}

void DhcpLeaseDb::PersistLeaseRecords(const std::vector<DhcpLease> &leases) {
    std::ofstream lease_ofstream;
    lease_ofstream.open(lease_filename_.c_str(), std::ofstream::app);
    if (!lease_ofstream.good()) {
        lease_ofstream.close();
        DHCP_TRACE(Error, "Cannot open DHCP Lease file for writing : "
                   << lease_filename_);
        return;
    }

    for (std::vector<DhcpLease>::const_iterator it = leases.begin();
         it != leases.end(); ++it) {
        WriteLeaseRecord(lease_ofstream, it->mac_, it->ip_,
                         it->lease_expiry_time_, it->released_);
    }

    lease_ofstream.flush();
    lease_ofstream.close();
}

void DhcpLeaseDb::WriteLeaseRecord(std::ofstream &lease_ofstream,
                                   const MacAddress &mac, const Ip4Address &ip,
                                   const uint64_t &expiry, bool released) {
    lease_ofstream << "<lease>";
    lease_ofstream << " <mac>" << mac.ToString() << "</mac>";
    lease_ofstream << " <ip>" << ip.to_string() << "</ip>";
    lease_ofstream << " <expiry>" << expiry << "</expiry>";
    lease_ofstream << " <released>" <<
        (released ? "true" : "false") << "</released>";
    lease_ofstream << " </lease>" << std::endl;

    if (!lease_ofstream.good()) {
        DHCP_TRACE(Error, "Lease write to " << lease_filename_ <<
                   " failed : " << mac.ToString() << " " << ip.to_string() <<
                   (released ? " released" : " valid"));
    }
}

void DhcpLeaseDb::LoadLeaseFile() {
    std::string leases;
    ReadLeaseFile(leases);
    ParseLeaseFile(leases);
}

void DhcpLeaseDb::ReadLeaseFile(std::string &leases) {
    std::ifstream ifile(lease_filename_.c_str());
    if (!ifile.good()) {
        ifile.close();
        DHCP_TRACE(Error, "Cannot open DHCP Lease file for reading : " <<
                   lease_filename_);
        return;
    }

    ifile.seekg(0, std::ios::end);
    leases.reserve(ifile.tellg());
    ifile.seekg(0, std::ios::beg);

    leases.assign((std::istreambuf_iterator<char>(ifile)),
                   std::istreambuf_iterator<char>());
    ifile.close();
}

void DhcpLeaseDb::ParseLeaseFile(const std::string &leases) {
    if (leases.empty())
        return;

    std::istringstream sstream(leases);
    xml_document xdoc;
    xml_parse_result result = xdoc.load(sstream);
    if (!result) {
        DHCP_TRACE(Error, "Unable to load DHCP leases. status=" <<
                   result.status << ", offset=" << result.offset);
        return;
    }

    for (xml_node root = xdoc.first_child(); root; root = root.next_sibling()) {
        if (strcmp(root.name(), "lease") == 0) {
            ParseLease(root);
        }
    }
}

void DhcpLeaseDb::ParseLease(const xml_node &lease) {
    MacAddress mac;
    Ip4Address ip;
    uint64_t expiry = 0;
    boost::system::error_code ec;
    bool released = false;
    bool error = false;
    for (xml_node root = lease.first_child(); root; root = root.next_sibling()) {
        if (strcmp(root.name(), "mac") == 0) {
            mac = MacAddress::FromString(root.child_value(), &ec);
            if (ec)
                error = true;
            continue;
        }
        if (strcmp(root.name(), "ip") == 0) {
            ip = Ip4Address::from_string(root.child_value(), ec);
            if (ec)
                error = true;
            continue;
        }
        if (strcmp(root.name(), "expiry") == 0) {
            char *endp;
            expiry = strtoull(root.child_value(), &endp, 10);
            while (isspace(*endp)) endp++;
            if (endp[0] != '\0')
                error = true;
            continue;
        }
        if (strcmp(root.name(), "released") == 0) {
            if (strcmp(root.child_value(), "true") == 0)
                released = true;
        }
    }

    if (mac.IsZero() || ip.is_unspecified() || error) {
        DHCP_TRACE(Error, "Invalid DHCP Lease record : " << mac.ToString() << " " <<
                   ip.to_string() << " " << expiry);
        return;
    }

    UpdateLease(mac, ip, expiry, released);
}

void ShowGwDhcpLeases::HandleRequest() const {
    std::vector<GwDhcpLeases> lease_list;
    const DhcpProto::LeaseManagerMap &lease_mgr =
        Agent::GetInstance()->GetDhcpProto()->lease_manager();
    for (DhcpProto::LeaseManagerMap::const_iterator it = lease_mgr.begin();
         it != lease_mgr.end(); ++it) {
        GwDhcpLeases gw_leases;
        std::vector<DhcpLeaseData> out_lease_data;
        const std::set<DhcpLeaseDb::DhcpLease> &lease_data =
            it->second->leases();
        for (std::set<DhcpLeaseDb::DhcpLease>::const_iterator lit =
             lease_data.begin(); lit != lease_data.end(); ++lit) {
            uint64_t current_time = ClockMonotonicUsec();
            DhcpLeaseData data;
            data.mac = lit->mac_.ToString();
            data.ip = lit->ip_.to_string();
            data.expiry_us = (lit->lease_expiry_time_ > current_time) ?
                (lit->lease_expiry_time_ - current_time) : 0;
            data.released = lit->released_ ? "yes" : "no";
            out_lease_data.push_back(data);
        }
        VmInterface *vmi = static_cast<VmInterface *>(it->first);
        gw_leases.physical_interface = vmi->parent()->name();
        gw_leases.vm_interface = vmi->name();
        gw_leases.leases = out_lease_data;
        lease_list.push_back(gw_leases);
    }

    GwDhcpLeasesResponse *resp = new GwDhcpLeasesResponse();
    resp->set_context(context());
    resp->set_gw_leases(lease_list);
    resp->Response();
}
