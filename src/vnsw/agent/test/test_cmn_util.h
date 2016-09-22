/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_cmn_util_h
#define vnsw_agent_test_cmn_util_h

#include "test/test_init.h"

using namespace std;

static const int kProjectUuid = 101;

struct TestLinkLocalService {
    std::string linklocal_name;
    std::string linklocal_ip;
    uint16_t    linklocal_port;
    std::string fabric_dns_name;
    std::vector<std::string> fabric_ip;
    uint16_t    fabric_port;
};

class TestTaskHold {
public:
    TestTaskHold(int task_id, int task_instance);
    ~TestTaskHold();
private:
    class HoldTask : public Task {
    public:
        HoldTask(TestTaskHold *hold_entry);
        bool Run();
        std::string Description() const { return "TestHoldTask"; }
    private:
        TestTaskHold *hold_entry_;
    };

    friend class HoldTask;
    int task_id_;
    int task_instance_;
    tbb::atomic<bool> task_held_;
};

struct TestForwardingClassData {
    uint32_t id_;
    uint32_t dscp_;
    uint32_t vlan_priority_;
    uint32_t mpls_exp_;
    uint32_t qos_queue_;
};

struct TestQosConfigData {
    std::string name_;
    uint32_t id_;
    std::string type_;
    uint32_t default_forwarding_class_;

    std::map<uint32_t, uint32_t> dscp_;
    std::map<uint32_t, uint32_t> vlan_priority_;
    std::map<uint32_t, uint32_t> mpls_exp_;
};

uuid MakeUuid(int id);
void DelXmlHdr(char *buff, int &len);
void DelXmlTail(char *buff, int &len);
void AddXmlHdr(char *buff, int &len);
void AddXmlTail(char *buff, int &len);
void AddLinkString(char *buff, int &len, const char *node_name1,
                   const char *name1, const char *node_name2, const char *name2,
                   const char *metadata = NULL);
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
string GetMetadata(const char *node1, const char *node2,
                   const char *mdata = NULL);
void AddLink(const char *node_name1, const char *name1, const char *node_name2,
             const char *name2, const char *mdata = NULL);
void DelLink(const char *node_name1, const char *name1, const char *node_name2,
             const char *name2);
void AddNode(const char *node_name, const char *name, int id);
void AddNode(const char *node_name, const char *name, int id, const char *attr,
             bool admin_state = true);
void AddNode(Agent *agent, const char *node_name, const char *name, int id,
             const char *attr, bool admin_state = true);
void DelNode(const char *node_name, const char *name);
void DelNode(Agent *agent, const char *node_name, const char *name);
void IntfSyncMsg(PortInfo *input, int id);
CfgIntEntry *CfgPortGet(boost::uuids::uuid u);
void IntfCfgAddThrift(PortInfo *input, int id);
void IntfCfgAdd(int intf_id, const string &name, const string ipaddr,
                int vm_id, int vn_id, const string &mac, uint16_t vlan,
                const string ip6addr, int project_id = kProjectUuid);
void IntfCfgAdd(int intf_id, const string &name, const string ipaddr,
                int vm_id, int vn_id, const string &mac, const string ip6addr);
void IntfCfgAdd(PortInfo *input, int id);
void IntfCfgDel(PortInfo *input, int id);
void IntfCfgDel(int id);
NextHop *InetInterfaceNHGet(NextHopTable *table, const char *ifname,
                            InterfaceNHFlags::Type type, bool is_mcast,
                            bool policy);
NextHop *ReceiveNHGet(NextHopTable *table, const char *ifname, bool policy);
bool VrfFind(const char *name);
bool VrfFind(const char *name, bool ret_del);
VrfEntry *VrfGet(const char *name, bool ret_del=false);
bool VnFind(int id);
VnEntry *VnGet(int id);
bool VxlanFind(int id);
VxLanId *VxlanGet(int id);
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
bool VmPortV6Active(int id);
bool VmPortV6Active(PortInfo *input, int id);
bool VmPortPolicyEnabled(int id);
bool VmPortPolicyEnabled(PortInfo *input, int id);
Interface *VmPortGet(int id);
bool VmPortFloatingIpCount(int id, unsigned int count);
bool VmPortAliasIpCount(int id, unsigned int count);
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
void VnVxlanAddReq(int id, const char *name, uint32_t vxlan_id);
void VnDelReq(int id);
void VrfAddReq(const char *name);
void VrfAddReq(const char *name, const boost::uuids::uuid &vn_uuid);
void VrfDelReq(const char *name);
void VmAddReq(int id);
void VmDelReq(int id);
void AclAddReq(int id);
void AclDelReq(int id);
void AclAddReq(int id, int ace_id, bool drop);
void StartAcl(string *str, const char *name, int id);
void EndAcl(string *str);
void AddAceEntry(string *str, const char *src_vn, const char *dst_vn,
                 const char *proto, uint16_t sport_start, uint16_t sport_end,
                 uint16_t dport_start, uint16_t dport_end,
                 const char *action, const std::string &vrf_assign,
                 const std::string &mirror_ip);

void DeleteRoute(const char *vrf, const char *ip, uint8_t plen,
                 const Peer *peer = NULL);
void DeleteRoute(const char *vrf, const char *ip);
bool RouteFind(const string &vrf_name, const Ip4Address &addr, int plen);
bool RouteFind(const string &vrf_name, const string &addr, int plen);
bool L2RouteFind(const string &vrf_name, const MacAddress &mac);
bool L2RouteFind(const string &vrf_name, const MacAddress &mac,
                 const IpAddress &ip);
bool RouteFindV6(const string &vrf_name, const string &addr, int plen);
bool RouteFindV6(const string &vrf_name, const Ip6Address &addr, int plen);
bool MCRouteFind(const string &vrf_name, const Ip4Address &saddr,
                 const Ip4Address &daddr);
bool MCRouteFind(const string &vrf_name, const Ip4Address &addr);
bool MCRouteFind(const string &vrf_name, const string &saddr,
                 const string &daddr);
bool MCRouteFind(const string &vrf_name, const string &addr);
InetUnicastRouteEntry *RouteGet(const string &vrf_name, const Ip4Address &addr, int plen);
InetUnicastRouteEntry *RouteGetV6(const string &vrf_name, const Ip6Address &addr, int plen);
Inet4MulticastRouteEntry *MCRouteGet(const string &vrf_name, const Ip4Address &grp_addr);
Inet4MulticastRouteEntry *MCRouteGet(const string &vrf_name, const string &grp_addr);
BridgeRouteEntry *L2RouteGet(const string &vrf_name, const MacAddress &mac);
BridgeRouteEntry *L2RouteGet(const string &vrf_name, const MacAddress &mac,
                             const IpAddress &ip_addr);
const NextHop* L2RouteToNextHop(const string &vrf, const MacAddress &mac);
EvpnRouteEntry *EvpnRouteGet(const string &vrf_name, const MacAddress &mac,
                             const IpAddress &ip_addr, uint32_t ethernet_tag);
const NextHop* RouteToNextHop(const string &vrf_name, const Ip4Address &addr,
                              int plen);
bool TunnelNHFind(const Ip4Address &server_ip);
bool TunnelNHFind(const Ip4Address &server_ip, bool policy, TunnelType::Type type);
bool EcmpTunnelRouteAdd(const Peer *peer, const string &vrf_name, const Ip4Address &vm_ip,
                       uint8_t plen, ComponentNHKeyList &comp_nh_list,
                       bool local_ecmp, const string &vn_name, const SecurityGroupList &sg,
                       const PathPreference &path_preference);
bool EcmpTunnelRouteAdd(Agent *agent, const Peer *peer, const string &vrf_name,
                        const string &prefix, uint8_t plen,
                        const string &remote_server_1, uint32_t label1,
                        const string &remote_server_2, uint32_t label2,
                        const string &vn);
bool BridgeTunnelRouteAdd(const Peer *peer, const string &vm_vrf,
                          TunnelType::TypeBmap bmap, const Ip4Address &server_ip,
                          uint32_t label, MacAddress &remote_vm_mac,
                          const IpAddress &vm_addr, uint8_t plen);
bool Inet4TunnelRouteAdd(const Peer *peer, const string &vm_vrf, const Ip4Address &vm_addr,
                         uint8_t plen, const Ip4Address &server_ip, TunnelType::TypeBmap bmap,
                         uint32_t label, const string &dest_vn_name,
                         const SecurityGroupList &sg,
                         const PathPreference &path_preference);
bool BridgeTunnelRouteAdd(const Peer *peer, const string &vm_vrf,
                          TunnelType::TypeBmap bmap, const char *server_ip,
                          uint32_t label, MacAddress &remote_vm_mac,
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
void AddVrf(const char *name, int id = 0, bool default_ri = true);
void DelVrf(const char *name);
void ModifyForwardingModeVn(const string &name, int id, const string &fw_mode);
void AddL2L3Vn(const char *name, int id);
void AddL2Vn(const char *name, int id);
void AddL3Vn(const char *name, int id);
void AddVn(const char *name, int id, bool admin_state = true);
void AddVn(const char *name, int id, int vxlan_id, bool admin_state = true);
void DelVn(const char *name);
void AddPortWithMac(const char *name, int id, const char *mac,
                    const char *attr);
void AddPort(const char *name, int id, const char *attr = NULL);
void AddSriovPort(const char *name, int id);
void AddPortByStatus(const char *name, int id, bool admin_status);
void DelPort(const char *name);
void AddAcl(const char *name, int id);
void DelAcl(const char *name);
void AddAcl(const char *name, int id, const char *src_vn, const char *dest_vn,
            const char *action);
void AddVrfAssignNetworkAcl(const char *name, int id, const char *src_vn,
                            const char *dest_vn, const char *action,
                            std::string vrf_name);
void AddQosAcl(const char *name, int id, const char *src_vn,
               const char *dest_vn, const char *action,
               std::string qos_config);
void AddMirrorAcl(const char *name, int id, const char *src_vn,
                  const char *dest_vn, const char *action,
                  std::string mirror_ip);
void AddSg(const char *name, int id, int sg_id = 1);
void DelOperDBAcl(int id);
void AddFloatingIp(const char *name, int id, const char *addr,
                   const char *fixed_ip="0.0.0.0");
void DelFloatingIp(const char *name);
void AddFloatingIpPool(const char *name, int id);
void DelFloatingIpPool(const char *name);
void AddAliasIp(const char *name, int id, const char *addr);
void DelAliasIp(const char *name);
void AddAliasIpPool(const char *name, int id);
void DelAliasIpPool(const char *name);
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
void AddVmPort(const char *vmi, int intf_id, const char *ip, const char *mac,
               const char *vrf, const char *vn, int vn_uuid, const char *vm,
               int vm_uuid, const char *instance_ip, int instance_uuid);
void DelVmPort(const char *vmi, int intf_id, const char *ip, const char *mac,
               const char *vrf, const char *vn, int vn_uuid, const char *vm,
               int vm_uuid, const char *instance_ip, int instance_uuid);
void DeleteVmportEnv(struct PortInfo *input, int count, int del_vn, int acl_id = 0,
                     const char *vn = NULL, const char *vrf = NULL,
                     bool with_ip = false, bool with_ip6 = false);
void DeleteVmportFIpEnv(struct PortInfo *input, int count, int del_vn, int acl_id = 0,
                     const char *vn = NULL, const char *vrf = NULL);
void CreateVmportEnvInternal(struct PortInfo *input, int count, int acl_id = 0,
                     const char *vn = NULL, const char *vrf = NULL, 
                     const char *vm_interface_attr = NULL, bool l2_vn = false,
                     bool with_ip = false, bool ecmp = false,
                     bool vn_admin_state = true, bool with_ip6 = false,
                     bool send_nova_msg = true);
void CreateL3VmportEnv(struct PortInfo *input, int count, int acl_id = 0,
                     const char *vn = NULL, const char *vrf = NULL);
void CreateV6VmportEnv(struct PortInfo *input, int count, int acl_id = 0,
                       const char *vn = NULL, const char *vrf = NULL,
                       bool with_v4_ip = true);
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
void CreateVmportWithoutNova(struct PortInfo *input, int count, int acl_id = 0,
                             const char *vn = NULL, const char *vrf = NULL);
void CreateV6VmportWithEcmp(struct PortInfo *input, int count, int acl_id = 0,
                            const char *vn = NULL, const char *vrf = NULL,
                            bool with_v4_ip = true);
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
FlowEntry* FlowGet(std::string sip, std::string dip, uint8_t proto,
                   uint16_t sport, uint16_t dport, int nh_id,
                   uint32_t flow_handle);
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
                   const vr_vrf_stats_req &req);
bool VrfStatsMatchPrev(int vrf_id, const vr_vrf_stats_req &req);
bool RouterIdMatch(Ip4Address rid2);
bool ResolvRouteFind(const string &vrf_name, const Ip4Address &addr, int plen);
bool VhostRecvRouteFind(const string &vrf_name, const Ip4Address &addr, int plen);
void AddVmPortVrf(const char *name, const string &ip, uint16_t tag,
                  const string &v6_ip = "");
void DelVmPortVrf(const char *name);
uint32_t PathCount(const string vrf_name, const Ip4Address &addr, int plen);
bool VlanNhFind(int id, uint16_t tag);
void AddInstanceIp(const char *name, int id, const char* addr);
void AddServiceInstanceIp(const char *name, int id, const char* addr, bool ecmp,
                          const char *tracking_ip);
void AddSubnetType(const char *name, int id, const char* addr, uint8_t);
void AddActiveActiveInstanceIp(const char *name, int id, const char* addr);
void AddHealthCheckServiceInstanceIp(const char *name, int id,
                                     const char *addr);
void DelInstanceIp(const char *name);
extern Peer *bgp_peer_;
VxLanId* GetVxLan(const Agent *agent, uint32_t vxlan_id);
bool FindVxLanId(const Agent *agent, uint32_t vxlan_id);
bool FindMplsLabel(MplsLabel::Type type, uint32_t label);
MplsLabel *GetActiveLabel(MplsLabel::Type type, uint32_t label);
uint32_t GetFlowKeyNH(int id);
uint32_t GetFlowKeyNH(char *name);
bool FindNH(NextHopKey *key);
NextHop *GetNH(NextHopKey *key);
bool VmPortServiceVlanCount(int id, unsigned int count);
void AddEncapList(const char *encap1, const char *encap2, const char *encap3);
void AddEncapList(Agent *agent, const char *encap1, const char *encap2,
                  const char *encap3);
void DelEncapList();
void DelEncapList(Agent *agent);

void DelHealthCheckService(const char *name);
void AddHealthCheckService(const char *name, int id, const char *url_path,
                           const char *monitor_type);

void VxLanNetworkIdentifierMode(bool config);
void GlobalForwardingMode(std::string mode);
int MplsToVrfId(int label);
const NextHop* MplsToNextHop(uint32_t label);
void AddInterfaceRouteTable(const char *name, int id, TestIp4Prefix *addr,
                           int count);
void AddInterfaceRouteTable(const char *name, int id, TestIp4Prefix *addr,
                           int count, const char *nexthop);
void AddInterfaceRouteTable(const char *name, int id, TestIp4Prefix *addr,
                           int count, const char *nexthop, 
                           const std::vector<std::string> &communities);
void AddInterfaceRouteTableV6(const char *name, int id, TestIp6Prefix *addr,
                              int count);
void ShutdownAgentController(Agent *agent);
void AddAap(std::string intf_name, int intf_id,
            std::vector<Ip4Address> aap_list);
void AddEcmpAap(std::string intf_name, int intf_id, Ip4Address ip,
                const std::string &mac);
void AddAap(std::string intf_name, int intf_id, Ip4Address ip,
            const std::string &mac);
void AddAapWithDisablePolicy(std::string intf_name, int intf_id,
                             std::vector<Ip4Address> aap_list,
                             bool disable_policy);

class XmppChannelMock : public XmppChannel {
public:
    XmppChannelMock() : fake_to_("fake"), fake_from_("fake-from") { }
    virtual ~XmppChannelMock() { }
    bool Send(const uint8_t *, size_t, xmps::PeerId, SendReadyCb) {
        return true;
    }
    void Close() { }
    void CloseComplete() { }
    bool IsCloseInProgress() const { return false; }
    int GetTaskInstance() const { return 0; }
    MOCK_METHOD2(RegisterReceive, void(xmps::PeerId, ReceiveCb));
    MOCK_METHOD1(UnRegisterReceive, void(xmps::PeerId));
    MOCK_METHOD1(UnRegisterWriteReady, void(xmps::PeerId));
    const std::string &ToString() const { return fake_to_; }
    const std::string &FromString() const  { return fake_from_; }
    std::string StateName() const { return string("Established"); }

    xmps::PeerState GetPeerState() const { return xmps::READY; }
    const XmppConnection *connection() const { return NULL; }
    virtual XmppConnection *connection() { return NULL; }
    virtual bool LastReceived(uint64_t durationMsec) const { return false; }
    virtual bool LastSent(uint64_t durationMsec) const { return false; }

    virtual void RegisterRxMessageTraceCallback(RxMessageTraceCb cb) {
    }
    virtual void RegisterTxMessageTraceCallback(TxMessageTraceCb cb) {
    }
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
    virtual std::string AuthType() const {
        return "";
    }
    virtual std::string PeerAddress() const {
        return "";
    }

private:
    std::string fake_to_;
    std::string fake_from_;
};

BgpPeer *CreateBgpPeer(std::string addr, std::string name);
BgpPeer *CreateBgpPeer(const Ip4Address &addr, std::string name);
void DeleteBgpPeer(Peer *peer);
void FillEvpnNextHop(BgpPeer *peer, std::string name,
                     uint32_t label, uint32_t bmap);
void FlushEvpnNextHop(BgpPeer *peer, std::string name,
                      uint32_t tag);
BridgeRouteEntry *GetL2FloodRoute(const std::string &vrf_name);
void AddPhysicalDevice(const char *name, int id);
void DeletePhysicalDevice(const char *name);
void AddPhysicalInterface(const char *name, int id, const char* display_name);
void DeletePhysicalInterface(const char *name);
void AddLogicalInterface(const char *name, int id, const char* display_name, int vlan = 0);
void DeleteLogicalInterface(const char *name);
PhysicalDevice *PhysicalDeviceGet(int id);
PhysicalInterface *PhysicalInterfaceGet(const std::string &name);
LogicalInterface *LogicalInterfaceGet(int id, const std::string &name);
void EnableRpf(const std::string &vn_name, int vn_id);
void DisableRpf(const std::string &vn_name, int vn_id);
void EnableUnknownBroadcast(const std::string &vn_name, int vn_id);
void DisableUnknownBroadcast(const std::string &vn_name, int vn_id);
void AddInterfaceVrfAssignRule(const char *intf_name, int intf_id,
                               const char *sip, const char *dip, int proto,
                               const char *vrf, const char *ignore_acl);
bool Inet6TunnelRouteAdd(const Peer *peer, const string &vm_vrf, const Ip6Address &vm_addr,
                         uint8_t plen, const Ip4Address &server_ip, TunnelType::TypeBmap bmap,
                         uint32_t label, const string &dest_vn_name,
                         const SecurityGroupList &sg,
                         const PathPreference &path_preference);
void AddPhysicalDeviceVn(Agent *agent, int dev_id, int vn_id, bool validate);
void DelPhysicalDeviceVn(Agent *agent, int dev_id, int vn_id, bool validate);
void AddStaticPreference(std::string intf_name, int intf_id, uint32_t value);
bool VnMatch(VnListType &vn_list, std::string &vn);
void SendBgpServiceConfig(const std::string &ip,
                          uint32_t source_port,
                          uint32_t id,
                          const std::string &vmi_name,
                          const std::string &vrf_name,
                          const std::string &bgp_router_type,
                          bool deleted);
void AddAddressVrfAssignAcl(const char *intf_name, int intf_id,
                            const char *sip, const char *dip, int proto,
                            int sport_start, int sport_end, int dport_start,
                            int dport_end, const char *vrf, const char *ignore_acl);
void SendBgpServiceConfig(const std::string &ip,
                          uint32_t source_port,
                          uint32_t id,
                          const std::string &vmi_name,
                          const std::string &vrf_name,
                          const std::string &bgp_router_type,
                          bool deleted);
bool QosConfigFind(uint32_t id);
const AgentQosConfig* QosConfigGetByIndex(uint32_t id);
const AgentQosConfig* QosConfigGet(uint32_t id);
bool ForwardingClassFind(uint32_t id);
ForwardingClass *ForwardingClassGet(uint32_t id);

void AddGlobalConfig(struct TestForwardingClassData *data, uint32_t count);
void DelGlobalConfig(struct TestForwardingClassData *data, uint32_t count);
void VerifyForwardingClass(Agent *agent, struct TestForwardingClassData *data,
                           uint32_t count);
void VerifyQosConfig(Agent *agent, struct TestQosConfigData *data);
void AddQosConfig(struct TestQosConfigData &data);
void AddQosQueue(const char *name, uint32_t id, uint32_t qos_queue_id);
#endif // vnsw_agent_test_cmn_util_h
