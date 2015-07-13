/*
 * Copyright (c) 2015 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_dhcp_lease_h__
#define vnsw_agent_dhcp_lease_h__

#include <fstream>
#include <boost/dynamic_bitset.hpp>

class Timer;
namespace pugi {
class xml_node;
}

// DHCP lease management is implemented here for a given subnet - used by
// Gateway interfaces. Hosts on the gateway interface get addresses allocated
// from the subnet assigned to the Gateway interface. Addresses other than
// reserved addresses can be leased or released.
// Two bitmasks are maintained, each bit representing an IP of the subnet
//     1. lease_bitmap - bit set implies IP is available and can be leased
//     2. released_lease_bitmap - bit set implies IP was leased earlier but
//        can be given again. This is maintained so that we can give the same
//        lease, if possible.
// When a new lease request comes, first available address from lease_bitmap
// is allocated. When lease_bitmap is exhausted, a released address from
// released_lease_bitmap is allocated.
//
// Lease records are persisted in a file. Records are appended to the file,
// with the last record being the latest for a client. The lease file is
// compacted after a certain number of lease updates.

class DhcpLeaseDb {
public:
    static const uint32_t kDhcpLeaseTimer = 300000;        // milli seconds

    struct DhcpLease {
        MacAddress mac_;
        mutable Ip4Address ip_;
        mutable uint64_t lease_expiry_time_;
        mutable bool released_;

        DhcpLease(const MacAddress &m, const Ip4Address &i,
                  uint64_t t, bool r) :
            mac_(m), ip_(i), lease_expiry_time_(t), released_(r) {}

        bool operator <(const DhcpLease &rhs) const {
            return mac_ < rhs.mac_;
        }
    };

    DhcpLeaseDb(const Ip4Address &subnet, uint8_t plen,
                const std::vector<Ip4Address> &reserve_addresses,
                const std::string &lease_filename, boost::asio::io_service &io);
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
    void ClearLeases();
    void set_lease_timeout(uint32_t timeout);

private:
    friend class DhcpTest;
    typedef boost::dynamic_bitset<> Bitmap;

    bool LeaseTimerExpiry();
    void UpdateLease(const MacAddress &mac, const Ip4Address &ip,
                     uint64_t expiry, bool released);
    void ReserveAddresses(const std::vector<Ip4Address> &addresses,
                          bool subnet_change);
    void IndexToAddress(size_t index, Ip4Address *address) const;
    size_t AddressToIndex(const Ip4Address &address) const;
    bool IsReservedAddress(const Ip4Address &address) const;
    void UpdateLeaseFileName(const std::string &name);
    void CreateLeaseFile();
    void PersistLeaseRecord(const MacAddress &mac, const Ip4Address &ip,
                            const uint64_t &expiry, bool released);
    void PersistLeaseRecords(const std::vector<DhcpLease> &leases);
    void WriteLeaseRecord(std::ofstream &lease_ofstream,
                          const MacAddress &mac, const Ip4Address &ip,
                          const uint64_t &expiry, bool released);
    void LoadLeaseFile();
    void ReadLeaseFile(std::string &leases);
    void ParseLeaseFile(const std::string &leases);
    void ParseLease(const pugi::xml_node &lease);

    Ip4Address subnet_;
    uint8_t    plen_;
    Bitmap     lease_bitmap_;
    Bitmap     released_lease_bitmap_; // bitmap indicating released addresses
    std::vector<Ip4Address> reserve_addresses_;
    std::set<DhcpLease> leases_;

    uint32_t max_lease_update_count_;
    uint32_t lease_update_count_;
    uint32_t lease_timeout_;
    Timer *timer_;
    std::string lease_filename_;

    DISALLOW_COPY_AND_ASSIGN(DhcpLeaseDb);
};

#endif // vnsw_agent_dhcp_lease_h__
