
#include "net/address_util.h"
#include "base/timer.h"
#include "cmn/agent_cmn.h"
#include "services/services_types.h"
#include "services/services_init.h"
#include "services/dhcp_lease_db.h"
#include "services/dhcp_proto.h"

DhcpLeaseDb::DhcpLeaseDb(const Ip4Address &subnet, uint8_t plen,
                         const std::vector<Ip4Address> &reserve_addresses,
                         boost::asio::io_service &io) :
    subnet_(subnet), plen_(plen), lease_timeout_(kDhcpLeaseTimer) {
    ReserveAddresses(reserve_addresses);
    timer_ = TimerManager::CreateTimer(io, "DhcpLeaseTimer",
                                       TaskScheduler::GetInstance()->
                                       GetTaskId("Agent::Services"),
                                       PktHandler::DHCP);
    timer_->Start(lease_timeout_,
                  boost::bind(&DhcpLeaseDb::LeaseTimerExpiry, this));
}

DhcpLeaseDb::~DhcpLeaseDb() {
    lease_bitmap_.clear();
    timer_->Cancel();
    TimerManager::DeleteTimer(timer_);
}

void DhcpLeaseDb::Update(const Ip4Address &subnet, uint8_t plen,
                         const std::vector<Ip4Address> &reserve_addresses) {
    // TODO: in case we update after allocating addresses from earlier subnet,
    // they will be ignored; no trace of that in the agent after this update;
    if (subnet != subnet_ || plen != plen_) {
        subnet_ = subnet;
        plen_ = plen;
        leases_.clear();
        lease_bitmap_.clear();
    }
    ReserveAddresses(reserve_addresses);
}

bool DhcpLeaseDb::Allocate(const MacAddress &mac, Ip4Address *address,
                           uint64_t lease) {
    size_t index;

    std::set<DhcpLease>::iterator it =
        leases_.find(DhcpLease(mac, Ip4Address(), 0));
    if (it != leases_.end()) {
        *address = it->ip_;
        it->lease_expiry_time_ = ClockMonotonicUsec() + (lease * 1000000);
        DHCP_TRACE(Trace, "DHCP Lease renew : " << mac.ToString() << " " <<
                   address->to_string());
        return true;
    }

    index = lease_bitmap_.find_first();
    if (index == lease_bitmap_.npos) {
        DHCP_TRACE(Trace, "DHCP Lease not available for MAC " << mac.ToString() <<
                   " in Subnet " << subnet_.to_string());
        return false;
    }

    IndexToAddress(index, address);
    lease_bitmap_[index] = 0;
    leases_.insert(DhcpLease(mac, *address,
                             ClockMonotonicUsec() + (lease * 1000000)));
    DHCP_TRACE(Trace, "New DHCP Lease : " << mac.ToString() << " " <<
               address->to_string());

    return true;
}

bool DhcpLeaseDb::Release(const MacAddress &mac) {
    std::set<DhcpLease>::const_iterator it =
        leases_.find(DhcpLease(mac, Ip4Address(), 0));
    if (it != leases_.end()) {
        DHCP_TRACE(Trace, "DHCP Lease released : " << it->ip_.to_string());
        size_t index = AddressToIndex(it->ip_);
        lease_bitmap_[index] = 1;
        leases_.erase(it);
        return true;
    }

    return false;
}

bool DhcpLeaseDb::LeaseTimerExpiry() {
    uint64_t current_time = ClockMonotonicUsec();

    for (std::set<DhcpLease>::iterator it = leases_.begin();
         it != leases_.end(); ) {
        if (current_time > it->lease_expiry_time_) {
            size_t index = AddressToIndex(it->ip_);
            lease_bitmap_[index] = 1;
            leases_.erase(it++);
        } else {
            it++;
        }
    }

    return true;
}

// block the reserved addresses
void DhcpLeaseDb::ReserveAddresses(const std::vector<Ip4Address> &addresses) {
    uint32_t num_bits = 1 << (32 - plen_);
    if (num_bits < (2 + addresses.size()))
        return;

    lease_bitmap_.resize(num_bits, 1);
    lease_bitmap_[0] = 0;
    lease_bitmap_[(1 << (32 - plen_)) - 1] = 0;
    for (std::vector<Ip4Address>::const_iterator it = addresses.begin();
         it != addresses.end(); ++it) {
        if (IsIp4SubnetMember(*it, subnet_, plen_))
            lease_bitmap_[AddressToIndex(*it)] = 0;
    }
    reserve_addresses_ = addresses;
}

void DhcpLeaseDb::IndexToAddress(size_t index, Ip4Address *address) {
    Ip4Address ip(subnet_.to_ulong() + index);
    *address = ip;
}

size_t DhcpLeaseDb::AddressToIndex(const Ip4Address &address) {
    return address.to_ulong() & (0xFFFFFFFF >> plen_);
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
            out_lease_data.push_back(data);
        }
        VmInterface *vmi = static_cast<VmInterface *>(it->first);
        gw_leases.physical_interface = vmi->parent()->name();
        gw_leases.leases = out_lease_data;
        lease_list.push_back(gw_leases);
    }

    GwDhcpLeasesResponse *resp = new GwDhcpLeasesResponse();
    resp->set_context(context());
    resp->set_gw_leases(lease_list);
    resp->Response();
}
