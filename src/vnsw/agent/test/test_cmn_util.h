/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_cmn_util_h
#define vnsw_agent_test_cmn_util_h

#include "test/test_init.h"

static const int kProjectUuid = 101;

struct TestLinkLocalService {
    std::string linklocal_name;
    std::string linklocal_ip;
    uint16_t    linklocal_port;
    std::string fabric_dns_name;
    std::vector<std::string> fabric_ip;
    uint16_t    fabric_port;
};

uuid MakeUuid(int id);
void DelXmlHdr(char *buff, int &len);
void DelXmlTail(char *buff, int &len);
void AddXmlHdr(char *buff, int &len);
void AddXmlTail(char *buff, int &len);
void AddLinkString(char *buff, int &len, const char *node_name1,
                   const char *name1, const char *node_name2, const char *name2);
void DelLinkString(char *buff, int &len, const char *node_name1,
                   const char *name1, const char *node_name2, const char *name2);
void AddNodeString(char *buff, int &len, const char *node_name, const char *name,
                   int id, const char *attr, bool admin_state = true);
void AddNodeString(char *buff, int &len, const char *node_name,
                   const char *name, int id);
void AddNodeString(char *buff, int &len, const char *nodename, const char *name,
                   IpamInfo *ipam, int count,
                   const std::vector<std::string> *vm_host_routes = NULL,
                   const char *add_subnet_tags = NULL);
void AddVmPortVrfNodeString(char *buff, int &len, const char *name, int id);
void DelNodeString(char *buff, int &len, const char *node_name, const char *name);
void ApplyXmlString(const char *buff); 
void AddLink(const char *node_name1, const char *name1, const char *node_name2,
             const char *name2);
void DelLink(const char *node_name1, const char *name1, const char *node_name2,
             const char *name2);
void AddNode(const char *node_name, const char *name, int id);
void AddNode(const char *node_name, const char *name, int id, const char *attr,
             bool admin_state = true);
void DelNode(const char *node_name, const char *name);
void IntfSyncMsg(PortInfo *input, int id);
void IntfCfgAdd(int intf_id, const string &name, const string ipaddr,
                int vm_id, int vn_id, const string &mac, uint16_t vlan,
                int project_id = kProjectUuid);
void IntfCfgAdd(int intf_id, const string &name, const string ipaddr,
                int vm_id, int vn_id, const string &mac);
void IntfCfgAdd(PortInfo *input, int id);
void IntfCfgDel(PortInfo *input, int id);
NextHop *InetInterfaceNHGet(NextHopTable *table, const char *ifname,
                            InterfaceNHFlags::Type type, bool is_mcast,
                            bool policy);
NextHop *ReceiveNHGet(NextHopTable *table, const char *ifname, bool policy);
bool VrfFind(const char *name);
bool VrfFind(const char *name, bool ret_del);
VrfEntry *VrfGet(const char *name);
bool VnFind(int id);
VnEntry *VnGet(int id);
bool AclFind(int id);
AclDBEntry *AclGet(int id);
VmEntry *VmGet(int id);
bool VmFind(int id);
bool VmPortFind(int id);
bool VmPortFindRetDel(int id);
uint32_t VmPortGetId(int id);
bool VmPortFind(PortInfo *input, int id);
bool VmPortL2Active(int id);
bool VmPortL2Active(PortInfo *input, int id);
bool VmPortActive(int id);
bool VmPortActive(PortInfo *input, int id);
bool VmPortPolicyEnabled(int id);
bool VmPortPolicyEnabled(PortInfo *input, int id);
Interface *VmPortGet(int id);
bool VmPortFloatingIpCount(int id, unsigned int count);
bool VmPortGetStats(PortInfo *input, int id, uint32_t & bytes, uint32_t & pkts);
bool VmPortStats(PortInfo *input, int id, uint32_t bytes, uint32_t pkts);
bool VmPortStatsMatch(Interface *intf, uint32_t ibytes, uint32_t ipkts, 
                             uint32_t obytes, uint32_t opkts);
InetInterface *InetInterfaceGet(const char *ifname);
bool VnStatsMatch(char *vn, uint64_t in_bytes, uint64_t in_pkts, 
                  uint64_t out_bytes, uint64_t out_pkts);
bool VmPortInactive(int id);
std::string VmPortGetAnalyzerName(int id);
Interface::MirrorDirection VmPortGetMirrorDirection(int id);
bool VmPortInactive(PortInfo *input, int id);
PhysicalInterface *EthInterfaceGet(const char *name);
VmInterface *VmInterfaceGet(int id);
bool VmPortPolicyEnable(int id);
bool VmPortPolicyEnable(PortInfo *input, int id);
bool VmPortPolicyDisable(int id);
bool VmPortPolicyDisable(PortInfo *input, int id);
bool DBTableFind(const string &table_name);
void DeleteTap(int fd);
void DeleteTapIntf(const int fd[], int count);
int CreateTap(const char *name);
void CreateTapIntf(const char *name, int count);
void CreateTapInterfaces(const char *name, int count, int *fd);
void VnAddReq(int id, const char *name);
void VnAddReq(int id, const char *name, int acl_id);
void VnAddReq(int id, const char *name, int acl_id, const char *vrf_name);
void VnAddReq(int id, const char *name, const char *vrf_name);
void VnDelReq(int id);
void VrfAddReq(const char *name);
void VrfDelReq(const char *name);
void VmAddReq(int id);
void VmDelReq(int id);
void AclAddReq(int id);
void AclDelReq(int id);
void AclAddReq(int id, int ace_id, bool drop);
void DeleteRoute(const char *vrf, const char *ip, uint8_t plen);
void DeleteRoute(const char *vrf, const char *ip);
bool RouteFind(const string &vrf_name, const Ip4Address &addr, int plen);
bool RouteFind(const string &vrf_name, const string &addr, int plen);
bool L2RouteFind(const string &vrf_name, const struct ether_addr &mac);
bool MCRouteFind(const string &vrf_name, const Ip4Address &saddr,
                 const Ip4Address &daddr);
bool MCRouteFind(const string &vrf_name, const Ip4Address &addr);
bool MCRouteFind(const string &vrf_name, const string &saddr,
                 const string &daddr);
bool MCRouteFind(const string &vrf_name, const string &addr);
Inet4UnicastRouteEntry *RouteGet(const string &vrf_name, const Ip4Address &addr, int plen);
Inet4MulticastRouteEntry *MCRouteGet(const string &vrf_name, const Ip4Address &grp_addr);
Inet4MulticastRouteEntry *MCRouteGet(const string &vrf_name, const string &grp_addr);
Layer2RouteEntry *L2RouteGet(const string &vrf_name, const struct ether_addr &mac);
bool TunnelNHFind(const Ip4Address &server_ip);
bool TunnelNHFind(const Ip4Address &server_ip, bool policy, TunnelType::Type type);
bool EcmpTunnelRouteAdd(const Peer *peer, const string &vrf_name, const Ip4Address &vm_ip,
                       uint8_t plen, ComponentNHKeyList &comp_nh_list,
                       bool local_ecmp, const string &vn_name, const SecurityGroupList &sg,
                       const PathPreference &path_preference);
bool Layer2TunnelRouteAdd(const Peer *peer, const string &vm_vrf, 
                          TunnelType::TypeBmap bmap, const Ip4Address &server_ip,
                          uint32_t label, struct ether_addr &remote_vm_mac,
                          const Ip4Address &vm_addr, uint8_t plen);
bool Inet4TunnelRouteAdd(const Peer *peer, const string &vm_vrf, const Ip4Address &vm_addr,
                         uint8_t plen, const Ip4Address &server_ip, TunnelType::TypeBmap bmap,
                         uint32_t label, const string &dest_vn_name,
                         const SecurityGroupList &sg,
                         const PathPreference &path_preference);
bool Layer2TunnelRouteAdd(const Peer *peer, const string &vm_vrf, 
                          TunnelType::TypeBmap bmap, const char *server_ip,
                          uint32_t label, struct ether_addr &remote_vm_mac,
                          const char *vm_addr, uint8_t plen);
bool Inet4TunnelRouteAdd(const Peer *peer, const string &vm_vrf, char *vm_addr,
                         uint8_t plen, char *server_ip, TunnelType::TypeBmap bmap,
                         uint32_t label, const string &dest_vn_name,
                         const SecurityGroupList &sg,
                         const PathPreference &path_preference);
bool TunnelRouteAdd(const char *server, const char *vmip, const char *vm_vrf,
                    int label, const char *vn);
bool AddArp(const char *ip, const char *mac_str, const char *ifname);
bool DelArp(const string &ip, const char *mac_str, const string &ifname);
void *asio_poll(void *arg);
void AsioRun();
void AsioStop();
void AddVm(const char *name, int id);
void DelVm(const char *name);
void AddVrf(const char *name, int id = 0);
void DelVrf(const char *name);
void ModifyForwardingModeVn(const string &name, int id, const string &fw_mode);
void AddL2Vn(const char *name, int id);
void AddVn(const char *name, int id, bool admin_state = true);
void DelVn(const char *name);
void AddPort(const char *name, int id, const char *attr = NULL);
void AddPortByStatus(const char *name, int id, bool admin_status);
void DelPort(const char *name);
void AddAcl(const char *name, int id);
void DelAcl(const char *name);
void AddAcl(const char *name, int id, const char *src_vn, const char *dest_vn,
            const char *action);
void AddVrfAssignNetworkAcl(const char *name, int id, const char *src_vn,
                            const char *dest_vn, const char *action,
                            std::string vrf_name);
void AddSg(const char *name, int id, int sg_id = 1);
void DelOperDBAcl(int id);
void AddFloatingIp(const char *name, int id, const char *addr);
void DelFloatingIp(const char *name);
void AddFloatingIpPool(const char *name, int id);
void DelFloatingIpPool(const char *name);
void AddIPAM(const char *name, IpamInfo *ipam, int size, const char *ipam_attr = NULL,
             const char *vdns_name = NULL,
             const std::vector<std::string> *vm_host_routes = NULL,
             const char *add_subnet_tags = NULL);
void DelIPAM(const char *name, const char *vdns_name = NULL);
void AddVDNS(const char *vdns_name, const char *vdns_attr);
void DelVDNS(const char *vdns_name);
void AddLinkLocalConfig(const TestLinkLocalService *services, int count);
void DelLinkLocalConfig();
void DeleteGlobalVrouterConfig();
void send_icmp(int fd, uint8_t smac, uint8_t dmac, uint32_t sip, uint32_t dip);
bool FlowStats(FlowIp *input, int id, uint32_t bytes, uint32_t pkts);
void DeleteVmportEnv(struct PortInfo *input, int count, int del_vn, int acl_id = 0,
                     const char *vn = NULL, const char *vrf = NULL);
void DeleteVmportFIpEnv(struct PortInfo *input, int count, int del_vn, int acl_id = 0,
                     const char *vn = NULL, const char *vrf = NULL);
void CreateVmportEnvInternal(struct PortInfo *input, int count, int acl_id = 0,
                     const char *vn = NULL, const char *vrf = NULL, 
                     const char *vm_interface_attr = NULL, bool l2_vn = false,
                     bool with_ip = false, bool ecmp = false,
                     bool vn_admin_state = true);
void CreateL2VmportEnv(struct PortInfo *input, int count, int acl_id = 0,
                     const char *vn = NULL, const char *vrf = NULL);
void CreateVmportEnvWithoutIp(struct PortInfo *input, int count, int acl_id = 0,
                     const char *vn = NULL, const char *vrf = NULL);
void CreateVmportEnv(struct PortInfo *input, int count, int acl_id = 0,
                     const char *vn = NULL, const char *vrf = NULL,
                     const char *vm_interface_attr = NULL,
                     bool vn_admin_state = true);
void CreateVmportFIpEnv(struct PortInfo *input, int count, int acl_id = 0,
                     const char *vn = NULL, const char *vrf = NULL);
void CreateVmportWithEcmp(struct PortInfo *input, int count, int acl_id = 0,
                          const char *vn = NULL, const char *vrf = NULL);
void FlushFlowTable();
bool FlowDelete(const string &vrf_name, const char *sip,
                const char *dip, uint8_t proto, uint16_t sport, uint16_t dport,
                int nh_id);
bool FlowFail(const string &vrf_name, const char *sip, const char *dip,
              uint8_t proto, uint16_t sport, uint16_t dport, int nh_id);
bool FlowFail(int vrf_id, const char *sip, const char *dip,
              uint8_t proto, uint16_t sport, uint16_t dport, int nh_id);
bool FlowGetNat(const string &vrf_name, const char *sip, const char *dip,
                uint8_t proto, uint16_t sport, uint16_t dport,
                std::string svn, std::string dvn, uint32_t hash_id,
                const char *nat_vrf, const char *nat_sip,
                const char *nat_dip, uint16_t nat_sport, int16_t nat_dport,
                int nh_id, int nat_nh_id);
FlowEntry* FlowGet(int nh_id, std::string sip, std::string dip, uint8_t proto,
                   uint16_t sport, uint16_t dport);
bool FlowGet(const string &vrf_name, const char *sip, const char *dip,
             uint8_t proto, uint16_t sport, uint16_t dport, bool rflow,
             std::string svn, std::string dvn, uint32_t hash_id, 
             int nh_id, int rev_nh_id = -1);
bool FlowGet(const string &vrf_name, const char *sip, const char *dip,
             uint8_t proto, uint16_t sport, uint16_t dport, bool rflow,
             std::string svn, std::string dvn, uint32_t hash_id, bool fwd, 
             bool nat, int nh_id, int rev_nh_id = -1);
bool FlowGet(int vrf_id, const char *sip, const char *dip, uint8_t proto, 
             uint16_t sport, uint16_t dport, bool short_flow, int hash_id,
             int reverse_hash_id, int nh_id, int rev_nh_id = -1);
FlowEntry* FlowGet(int vrf_id, std::string sip, std::string dip, uint8_t proto,
                   uint16_t sport, uint16_t dport, int nh_id);
bool FlowStatsMatch(const string &vrf_name, const char *sip, const char *dip,
                    uint8_t proto, uint16_t sport, uint16_t dport,
                    uint64_t pkts, uint64_t bytes, int nh_id);
bool FindFlow(const string &vrf_name, const char *sip, const char *dip,
              uint8_t proto, uint16_t sport, uint16_t dport, bool nat,
              const string &nat_vrf_name, const char *nat_sip,
              const char *nat_dip, uint16_t nat_sport, uint16_t nat_dport,
              int fwd_nh_id, int rev_nh_id);
PktGen *TxTcpPacketUtil(int ifindex, const char *sip, const char *dip,
                        int sport, int dport, uint32_t hash_idx);
PktGen *TxIpPacketUtil(int ifindex, const char *sip, const char *dip, int proto,
                       int hash_id = 0);
PktGen *TxMplsPacketUtil(int ifindex, const char *out_sip, const char *out_dip,
                         uint32_t label, const char *sip, const char *dip,
                         int proto, int hash_idx = 0);
PktGen *TxMplsTcpPacketUtil(int ifindex, const char *out_sip,
                            const char *out_dip, uint32_t label, 
                            const char *sip, const char *dip, 
                            int sport, int dport, int hash_idx = 0);

bool VrfStatsMatch(int vrf_id, std::string vrf_name, bool stats_match,
                   uint64_t discards, uint64_t resolves, uint64_t receives, 
                   uint64_t udp_tunnels, uint64_t udp_mpls_tunnels, 
                   uint64_t gre_mpls_tunnels, uint64_t ecmp_composites, 
                   uint64_t fabric_composites, uint64_t l2_composites,
                   uint64_t l3_composites, uint64_t multi_proto_composites,
                   uint64_t encaps, uint64_t l2_encaps);
bool VrfStatsMatchPrev(int vrf_id, uint64_t discards, uint64_t resolves, 
                   uint64_t receives, uint64_t udp_tunnels, 
                   uint64_t udp_mpls_tunnels, uint64_t gre_mpls_tunnels, 
                   uint64_t ecmp_composites, uint64_t fabric_composites, 
                   uint64_t l2_composites, uint64_t l3_composites, 
                   uint64_t multi_proto_composites, uint64_t encaps, 
                   uint64_t l2_encaps);
bool RouterIdMatch(Ip4Address rid2);
bool ResolvRouteFind(const string &vrf_name, const Ip4Address &addr, int plen);
bool VhostRecvRouteFind(const string &vrf_name, const Ip4Address &addr, int plen);
void AddVmPortVrf(const char *name, const string &ip, uint16_t tag);
void DelVmPortVrf(const char *name);
uint32_t PathCount(const string vrf_name, const Ip4Address &addr, int plen);
bool VlanNhFind(int id, uint16_t tag);
void AddInstanceIp(const char *name, int id, const char* addr);
void AddActiveActiveInstanceIp(const char *name, int id, const char* addr);
void DelInstanceIp(const char *name);
extern Peer *bgp_peer_;
bool FindMplsLabel(MplsLabel::Type type, uint32_t label);
MplsLabel *GetMplsLabel(MplsLabel::Type type, uint32_t label);
uint32_t GetFlowKeyNH(int id);
uint32_t GetFlowKeyNH(char *name);
bool FindNH(NextHopKey *key);
NextHop *GetNH(NextHopKey *key);
bool VmPortServiceVlanCount(int id, unsigned int count);
void AddEncapList(const char *encap1, const char *encap2, const char *encap3);
void DelEncapList();
void VxLanNetworkIdentifierMode(bool config);
int MplsToVrfId(int label);
void AddInterfaceRouteTable(const char *name, int id, TestIp4Prefix *addr, 
                           int count);
void ShutdownAgentController(Agent *agent);

class XmppChannelMock : public XmppChannel {
public:
    XmppChannelMock() { }
    virtual ~XmppChannelMock() { }
    bool Send(const uint8_t *, size_t, xmps::PeerId, SendReadyCb) {
        return true;
    }
    MOCK_METHOD2(RegisterReceive, void(xmps::PeerId, ReceiveCb));
    MOCK_METHOD1(UnRegisterReceive, void(xmps::PeerId));
    std::string ToString() const { return string("fake"); }
    std::string StateName() const { return string("Established"); }

    xmps::PeerState GetPeerState() const { return xmps::READY; }
    std::string FromString() const  { return string("fake-from"); }
    const XmppConnection *connection() const { return NULL; }

    virtual std::string LastStateName() const {
        return "";
    }
    virtual std::string LastStateChangeAt() const {
        return "";
    }
    virtual std::string LastEvent() const {
        return "";
    }
    virtual uint32_t rx_open() const {
        return 0;
    }
    virtual uint32_t rx_close() const {
        return 0;
    }
    virtual uint32_t rx_update() const {
        return 0;
    }
    virtual uint32_t rx_keepalive() const {
        return 0;
    }
    virtual uint32_t tx_open() const {
        return 0;
    }
    virtual uint32_t tx_close() const {
        return 0;
    }
    virtual uint32_t tx_update() const {
        return 0;
    }
    virtual uint32_t tx_keepalive() const {
        return 0;
    }
    virtual uint32_t FlapCount() const {
        return 0;
    }
    virtual std::string LastFlap() const {
        return "";
    }
};

BgpPeer *CreateBgpPeer(const Ip4Address &addr, std::string name);
void DeleteBgpPeer(Peer *peer);

#endif // vnsw_agent_test_cmn_util_h
