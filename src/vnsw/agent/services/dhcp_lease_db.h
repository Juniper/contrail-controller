/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_dhcp_lease_h__
#define vnsw_agent_dhcp_lease_h__

#include <boost/dynamic_bitset.hpp>

class Timer;

// DHCP lease database
class DhcpLeaseDb {
public:
    static const uint32_t kDhcpLeaseTimer = 60000;        // milli seconds

    struct DhcpLease {
        MacAddress mac_;
        Ip4Address ip_;
        mutable uint64_t lease_expiry_time_;

        DhcpLease(const MacAddress &m, const Ip4Address &i, uint64_t t) :
            mac_(m), ip_(i), lease_expiry_time_(t) {}

        bool operator <(const DhcpLease &rhs) const {
            return mac_ < rhs.mac_;
        }
    };

    DhcpLeaseDb(const Ip4Address &subnet, uint8_t plen,
                const std::vector<Ip4Address> &reserve_addresses,
                boost::asio::io_service &io);
    virtual ~DhcpLeaseDb();

    // update subnet details
    void Update(const Ip4Address &subnet, uint8_t plen,
                const std::vector<Ip4Address> &reserve_addresses);

    // allocate an address for the lease time (specified in seconds)
    bool Allocate(const MacAddress &mac, Ip4Address *address, uint64_t lease);
    bool Release(const MacAddress &mac);

    Ip4Address subnet() const { return subnet_; }
    uint8_t plen() const { return plen_; }
    const std::set<DhcpLease> &leases() const { return leases_; }

private:
    typedef boost::dynamic_bitset<> Bitmap;

    bool LeaseTimerExpiry();
    void UpdateLeaseFile();
    void ReserveAddresses(const std::vector<Ip4Address> &addresses);
    void IndexToAddress(size_t index, Ip4Address *address);
    size_t AddressToIndex(const Ip4Address &address);

    Ip4Address subnet_;
    uint8_t    plen_;
    Bitmap     lease_bitmap_;
    std::vector<Ip4Address> reserve_addresses_;
    std::set<DhcpLease> leases_;

    uint32_t lease_timeout_;
    Timer *timer_;

    DISALLOW_COPY_AND_ASSIGN(DhcpLeaseDb);
};

#endif // vnsw_agent_dhcp_lease_h__
