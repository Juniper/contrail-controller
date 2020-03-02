/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

#ifndef vnsw_agent_test_cmn_util_h
#define vnsw_agent_test_cmn_util_h

#include "test/test_init.h"

using namespace std;

#define NH_PER_VM 5

#define EXPECT_TRUE_RET(a) \
    do { EXPECT_TRUE((a)); if ((a) == false) ret = false; } while (false)
#define EXPECT_FALSE_RET(a) \
    do { EXPECT_FALSE((a)); if ((a) == true) ret = false; }  while (false)

class VmiSubscribeEntry;
static const int kProjectUuid = 101;

struct EncryptTunnelEndpoint {
    std::string ip;
};

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

struct TestTag {
    std::string name_;
    uint32_t uuid_;
    uint32_t id_;
};

boost::uuids::uuid MakeUuid(int id);
void DelXmlHdr(char *buff, int &len);
void DelXmlTail(char *buff, int &len);
void AddXmlHdr(char *buff, int &len);
void AddXmlTail(char *buff, int &len);
void LinkString(char *buff, int &len, const char *node_name1,
                   const char *name1, const char *node_name2, const char *name2,
                   const char* mdata = NULL);
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
const char *GetMetadata(const char *node1, const char *node2);
void AddLink(const char *node_name1, const char *name1, const char *node_name2,
             const char *name2, const char *mdata = NULL);
void DelLink(const char *node_name1, const char *name1, const char *node_name2,
             const char *name2, const char* mdata = NULL);
void AddLinkNode(const char *node_name, const char *name, const char *attr);
void AddNode(const char *node_name, const char *name, int id);
void AddNode(const char *node_name, const char *name, int id, const char *attr,
             bool admin_state = true);
void AddNode(Agent *agent, const char *node_name, const char *name, int id,
             const char *attr, bool admin_state = true);
void DelNode(const char *node_name, const char *name);
void DelNode(Agent *agent, const char *node_name, const char *name);
uint32_t PortSubscribeSize(Agent *agent);
bool PortSubscribe(VmiSubscribeEntry *entry);
bool PortSubscribe(const std::string &ifname,
                   const boost::uuids::uuid &vmi_uuid,
                   const boost::uuids::uuid vm_uuid,
                   const std::string &vm_name,
                   const boost::uuids::uuid &vn_uuid,
                   const boost::uuids::uuid &project_uuid,
                   const Ip4Address &ip4_addr, const Ip6Address &ip6_addr,
                   const std::string &mac_addr);
void PortUnSubscribe(const boost::uuids::uuid &u);
void IntfSyncMsg(PortInfo *input, int id);
void IntfCfgAddNoWait(int intf_id, const string &name, const string ipaddr,
                      int vm_id, int vn_id, const string &mac, uint16_t vlan,
                      const string ip6addr, int project_id);
void IntfCfgAddThrift(PortInfo *input, int id);
void IntfCfgAdd(int intf_id, const string &name, const string ipaddr,
                int vm_id, int vn_id, const string &mac, uint16_t vlan,
                const string ip6addr, uint8_t vhostuser_mode,
                int project_id = kProjectUuid);
void IntfCfgAdd(int intf_id, const string &name, const string ipaddr,
                int vm_id, int vn_id, const string &mac, const string ip6addr);
void IntfCfgAdd(PortInfo *input, int id);
void IntfCfgDel(PortInfo *input, int id);
void IntfCfgDelNoWait(int id);
void IntfCfgDel(int id);
NextHop *InetInterfaceNHGet(NextHopTable *table, const char *ifname,
                            InterfaceNHFlags::Type type, bool is_mcast,
                            bool policy);
NextHop *ReceiveNHGet(NextHopTable *table, const char *ifname, bool policy);
bool VrfFind(const char *name);
bool VrfFind(const char *name, bool ret_del);
VrfEntry *VrfGet(size_t index);
VrfEntry *VrfGet(const char *name, bool ret_del=false);
uint32_t GetVrfId(const char *name);
bool VnFind(int id);
VnEntry *VnGet(int id);
bool VxlanFind(int id);
VxLanId *VxlanGet(int id);
bool AclFind(int id);
AclDBEntry *AclGet(int id);
bool PolicySetFind(int id);
PolicySet* PolicySetGet(int id);
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
Interface *VhostGet(const char *name);
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
bool RouteFindMpls(const string &vrf_name, const Ip4Address &addr, int plen);
bool RouteFindMpls(const string &vrf_name, const string &addr, int plen);
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
InetUnicastRouteEntry* RouteGetLPM(const string &vrf_name, const Ip4Address &addr);
InetUnicastRouteEntry *RouteGetMpls(const string &vrf_name, const Ip4Address &addr, int plen);
InetUnicastRouteEntry *RouteGetV6(const string &vrf_name, const Ip6Address &addr, int plen);
Inet4MulticastRouteEntry *MCRouteGet(const string &vrf_name, const Ip4Address &grp_addr);
Inet4MulticastRouteEntry *MCRouteGet(const Peer *peer, const string &vrf_name, const Ip4Address &grp_addr, const Ip4Address &src_addr);
Inet4MulticastRouteEntry *MCRouteGet(const string &vrf_name, const string &grp_addr);
BridgeRouteEntry *L2RouteGet(const string &vrf_name, const MacAddress &mac);
BridgeRouteEntry *L2RouteGet(const string &vrf_name, const MacAddress &mac,
                             const IpAddress &ip_addr);
const NextHop* L2RouteToNextHop(const string &vrf, const MacAddress &mac);
EvpnRouteEntry *EvpnRouteGet(const string &vrf_name, const MacAddress &mac,
                             const IpAddress &ip_addr, uint32_t ethernet_tag);
const NextHop* RouteToNextHop(const string &vrf_name, const Ip4Address &addr,
                              int plen);
const NextHop* LPMRouteToNextHop(const string &vrf_name,
                              const Ip4Address &addr);
const NextHop* MCRouteToNextHop(const Peer *peer, const string &vrf_name,
                            const Ip4Address &grp_addr,
                            const Ip4Address &src_addr);
bool TunnelNHFind(const Ip4Address &server_ip);
bool TunnelNHFind(const Ip4Address &server_ip, bool policy, TunnelType::Type type);
bool EcmpTunnelRouteAdd(const BgpPeer *peer, const string &vrf_name,
                        const Ip4Address &vm_ip,
                       uint8_t plen, ComponentNHKeyList &comp_nh_list,
                       bool local_ecmp, const string &vn_name, const SecurityGroupList &sg,
                       const TagList &tag,
                       const PathPreference &path_preference, bool add_local_path = false);
bool EcmpTunnelRouteAdd(const BgpPeer *peer, const string &vrf_name,
                        const Ip4Address &vm_ip,
                       uint8_t plen, ComponentNHKeyList &comp_nh_list,
                       bool local_ecmp, const string &vn_name,
                       const SecurityGroupList &sg,
                       const TagList &tag,
                       const PathPreference &path_preference, EcmpLoadBalance& ecmp_load_balnce);
bool EcmpTunnelRouteAdd(Agent *agent, const BgpPeer *peer, const string &vrf_name,
                        const string &prefix, uint8_t plen,
                        const string &remote_server_1, uint32_t label1,
                        const string &remote_server_2, uint32_t label2,
                        const string &vn);
bool MplsVpnEcmpTunnelAdd(const BgpPeer *peer, const string &vrf,
                        const Ip4Address &prefix, uint8_t plen,
                        Ip4Address &remote_server_1, uint32_t label1,
                        Ip4Address &remote_server_2, uint32_t label2,
                        const string &vn);
bool MplsLabelInetEcmpTunnelAdd(const BgpPeer *peer, const string &vrf,
                        const Ip4Address &prefix, uint8_t plen,
                        Ip4Address &remote_server_1, uint32_t label1,
                        Ip4Address &remote_server_2, uint32_t label2,
                        const string &vn);
bool BridgeTunnelRouteAdd(const BgpPeer *peer, const string &vm_vrf,
                          TunnelType::TypeBmap bmap, const Ip4Address &server_ip,
                          uint32_t label, MacAddress &remote_vm_mac,
                          const IpAddress &vm_addr, uint8_t plen,
                          const std::string &rewrite_dmac,
                          uint32_t tag = 0, bool leaf = false);
bool BridgeTunnelRouteAdd(const BgpPeer *peer, const string &vm_vrf,
                          TunnelType::TypeBmap bmap, const Ip4Address &server_ip,
                          uint32_t label, MacAddress &remote_vm_mac,
                          const IpAddress &vm_addr, uint8_t plen,
                          uint32_t tag = 0, bool leaf = false);
bool BridgeTunnelRouteAdd(const BgpPeer *peer, const string &vm_vrf,
                          TunnelType::TypeBmap bmap, const char *server_ip,
                          uint32_t label, MacAddress &remote_vm_mac,
                          const char *vm_addr, uint8_t plen, uint32_t tag = 0,
                          bool leaf = false);
bool Inet4TunnelRouteAdd(const BgpPeer *peer, const string &vm_vrf,
                         const Ip4Address &vm_addr,
                         uint8_t plen, const Ip4Address &server_ip, TunnelType::TypeBmap bmap,
                         uint32_t label, const string &dest_vn_name,
                         const SecurityGroupList &sg,
                         const TagList &tag,
                         const PathPreference &path_preference);
bool Inet4TunnelRouteAdd(const BgpPeer *peer, const string &vm_vrf, char *vm_addr,
                         uint8_t plen, char *server_ip, TunnelType::TypeBmap bmap,
                         uint32_t label, const string &dest_vn_name,
                         const SecurityGroupList &sg,
                         const TagList &tag,
                         const PathPreference &path_preference);
bool Inet4MplsRouteAdd(const BgpPeer *peer, const string &vm_vrf,
                         const Ip4Address &vm_addr,
                         uint8_t plen, const Ip4Address &server_ip, TunnelType::TypeBmap bmap,
                         uint32_t label, const string &dest_vn_name,
                         const SecurityGroupList &sg,
                         const TagList &tag,
                         const PathPreference &path_preference);
bool Inet4MplsRouteAdd(const BgpPeer *peer, const string &vm_vrf, char *vm_addr,
                         uint8_t plen, char *server_ip, TunnelType::TypeBmap bmap,
                         uint32_t label, const string &dest_vn_name,
                         const SecurityGroupList &sg,
                         const TagList &tag,
                         const PathPreference &path_preference);
bool TunnelRouteAdd(const char *server, const char *vmip, const char *vm_vrf,
                    int label, const char *vn);
bool AddArp(const char *ip, const char *mac_str, const char *ifname);
bool AddArpReq(const char *ip, const char *ifname);
bool DelArp(const string &ip, const char *mac_str, const string &ifname);
void *asio_poll(void *arg);
void AsioRun();
void AsioStop();
void AddVm(const char *name, int id);
void DelVm(const char *name);
void AddVrf(const char *name, int id = 0, bool default_ri = true);
void AddVrfWithSNat(const char *name, int id, bool default_ri, bool snat);
void DelVrf(const char *name);
void ModifyForwardingModeVn(const string &name, int id, const string &fw_mode);
void AddL2L3Vn(const char *name, int id);
void AddL2Vn(const char *name, int id);
void AddL3Vn(const char *name, int id);
void AddVn(const char *name, int id, bool admin_state = true);
void AddVn(const char *name, int id, int vxlan_id, bool admin_state = true);
void AddMirrorVn(const char *name, int id, int vxlan_id, bool admin_state = true);
void DelVn(const char *name);
void SetVnMaxFlows(const string &name, int id, uint32_t max_flows);
void SetVmiMaxFlows(std::string intf_name, int intf_id, uint32_t max_flows);
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
void AddTag(const char *name, int id);
void DelOperDBAcl(int id);
void AddFloatingIp(const char *name, int id, const char *addr,
                   const char *fixed_ip="0.0.0.0",
                   const char *direction = NULL,
                   bool port_map_enable = false,
                   uint16_t port_map1 = 0, uint16_t port_map2 = 0,
                   uint16_t port_map3 = 0, uint16_t port_map4 = 0);
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
void AddEncryptRemoteTunnelConfig(const EncryptTunnelEndpoint *endpoints, int count,
                                std::string encrypt_mode);
void AddMulticastPolicy(const char *name, uint32_t id, MulticastPolicy *msg,
                                int msg_size);
void DelMulticastPolicy(const char *name);
void AddLinkLocalConfig(const TestLinkLocalService *services, int count);
void DelLinkLocalConfig();
void AddFlowAgingTimerConfig(int tcp_timeout, int udp_timeout, int icmp_timeout);
void AddPortTranslationConfig();
void DeleteGlobalVrouterConfig();
void send_icmp(int fd, uint8_t smac, uint8_t dmac, uint32_t sip, uint32_t dip);
bool FlowStats(FlowIp *input, int id, uint32_t bytes, uint32_t pkts);
void AddLrPort(const char *vmi, int intf_id, const char *ip, const char *mac,
               const char *vrf, const char *vn, int vn_uuid, const char *vm,
               int vm_uuid, const char *instance_ip, int instance_uuid);
void DelLrPort(const char *vmi, int intf_id, const char *ip, const char *mac,
               const char *vrf, const char *vn, int vn_uuid, const char *vm,
               int vm_uuid, const char *instance_ip, int instance_uuid);
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
                    uint64_t pkts, uint64_t bytes, int nh_id,
                    uint32_t flow_handle = FlowEntry::kInvalidFlowHandle);
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
                  const string &v6_ip = "", bool swap_mac = false);
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
extern BgpPeer *bgp_peer_;
VxLanId* GetVxLan(const Agent *agent, uint32_t vxlan_id);
bool FindVxLanId(const Agent *agent, uint32_t vxlan_id);
bool FindMplsLabel(uint32_t label);
MplsLabel *GetActiveLabel(uint32_t label);
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
                           const char *monitor_type,
                           const char *service_type = "end-to-end");

void VxLanNetworkIdentifierMode(bool config, const char *encap1 = NULL,
                                const char *encap2 = NULL,
                                const char *encap3 = NULL);
void GlobalForwardingMode(std::string mode);
void AddFlowExportRate(int cfg_flow_export_rate);
void AddBgpaasPortRange(const int port_start, const int port_end);
void DelBgpaasPortRange();
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
            const std::string &mac, uint32_t plen = 32);
void AddAapWithDisablePolicy(std::string intf_name, int intf_id,
                             std::vector<Ip4Address> aap_list,
                             bool disable_policy);
void AddAapWithMacAndDisablePolicy(const std::string &intf_name, int intf_id,
                                   Ip4Address ip, const std::string &mac,
                                   bool disable_policy);
bool getIntfStatus(PhysicalInterface *pintf, const string& intf_name);
void delTestPhysicalIntfFromMap(PhysicalInterface *pintf,
        const string& intf_name);

class XmppChannelMock : public XmppChannel {
public:
    XmppChannelMock() : fake_to_("fake"), fake_from_("fake-from") {
        for (uint8_t idx = 0; idx < (uint8_t)xmps::OTHER; idx++) {
            registered_[idx] = false;
        }
    }
    virtual ~XmppChannelMock() { }
    bool Send(const uint8_t *, size_t, xmps::PeerId, SendReadyCb) {
        return true;
    }
    void Close() { }
    int GetTaskInstance() const { return 0; }
    void RegisterReceive(xmps::PeerId id, ReceiveCb cb) {
        registered_[id] = true;
    }
    void UnRegisterReceive(xmps::PeerId id) {
        bool delete_channel = true;
        registered_[id] = false;
        for (uint8_t idx = 0; idx < (uint8_t)xmps::OTHER; idx++) {
            if (registered_[idx])
                delete_channel = false;
        }
        if (delete_channel)
            delete this;
    }
    MOCK_METHOD1(UnRegisterWriteReady, void(xmps::PeerId));
    const std::string &ToString() const { return fake_to_; }
    const std::string &FromString() const  { return fake_from_; }
    std::string StateName() const { return string("Established"); }

    xmps::PeerState GetPeerState() const { return xmps::READY; }
    const XmppConnection *connection() const { return NULL; }
    virtual XmppConnection *connection() { return NULL; }
    virtual bool LastReceived(time_t duration) const { return false; }
    virtual bool LastSent(time_t duration) const { return false; }

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
    bool registered_[xmps::OTHER];
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
void AddVirtualPortGroup(const char *name, int id, const char *display_name);
void DeleteVirtualPortGroup(const char *name);
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
bool Inet6TunnelRouteAdd(const BgpPeer *peer, const string &vm_vrf,
                         const Ip6Address &vm_addr,
                         uint8_t plen, const Ip4Address &server_ip, TunnelType::TypeBmap bmap,
                         uint32_t label, const string &dest_vn_name,
                         const SecurityGroupList &sg, const TagList &tag,
                         const PathPreference &path_preference);
void AddPhysicalDeviceVn(Agent *agent, int dev_id, int vn_id, bool validate);
void DelPhysicalDeviceVn(Agent *agent, int dev_id, int vn_id, bool validate);
void AddStaticPreference(std::string intf_name, int intf_id, uint32_t value);
bool VnMatch(VnListType &vn_list, std::string &vn);
void AddControlNodeZone(const std::string &name, int id);
void DeleteControlNodeZone(const std::string &name);
std::string GetBgpRouterXml(const std::string &ip,
                            uint32_t &source_port,
                            uint32_t &dest_port,
                            const std::string &bgp_router_type);
std::string AddBgpRouterConfig(const std::string &ip,
                        uint32_t source_port,
                        uint32_t dest_port,
                        uint32_t id,
                        const std::string &vrf_name,
                        const std::string &bgp_router_type);
void DeleteBgpRouterConfig(const std::string &ip,
                           uint32_t source_port,
                           const std::string &vrf_name);
std::string AddBgpServiceConfig(const std::string &ip,
                                uint32_t source_port,
                                uint32_t dest_port,
                                uint32_t id,
                                const std::string &vmi_name,
                                const std::string &vrf_name,
                                const std::string &bgp_router_type,
                                bool is_shared);
void DeleteBgpServiceConfig(const std::string &ip,
                          uint32_t source_port,
                          const std::string &vmi_name,
                          const std::string &vrf_name);
void AddAddressVrfAssignAcl(const char *intf_name, int intf_id,
                            const char *sip, const char *dip, int proto,
                            int sport_start, int sport_end, int dport_start,
                            int dport_end, const char *vrf, const char *ignore_acl,
                            const char *svc_intf_type = NULL);
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
void DelQosConfig(struct TestQosConfigData &data);
void AddQosQueue(const char *name, uint32_t id, uint32_t qos_queue_id);
uint32_t AllocLabel(const char *name);
void FreeLabel(uint32_t label);
void AddBridgeDomain(const char *name, uint32_t id, uint32_t isid,
                     bool mac_learning = true);
void AddVmportBridgeDomain(const char *name, uint32_t vlan_tag);
void AddTag(const char *name, uint32_t uuid, uint32_t id,
            const std::string type = "");
void AddFirewallPolicyRuleLink(const std::string &node_name,
                               const std::string &fp,
                               const std::string &fr,
                               const std::string &id);
void DelFirewallPolicyRuleLink(const std::string &node_name,
                               const std::string &fp,
                               const std::string &fr);
void AddPolicySetFirewallPolicyLink(const std::string &node_name,
                                  const std::string &ps,
                                  const std::string &fp,
                                  const std::string &id);
void DelPolicySetFirewallPolicyLink(const std::string &node_name,
                                  const std::string &ps,
                                  const std::string &fp);
void AddAddressGroup(const char *name, uint32_t id,
                     TestIp4Prefix *prefix, uint32_t count);
bool BridgeDomainFind(int id);
BridgeDomainEntry* BridgeDomainGet(int id);
void AddFwRuleTagLink(std::string fw_rule, TestTag *tag, uint32_t count);
void DelFwRuleTagLink(std::string fw_rule, TestTag *tag, uint32_t count);
void AddFirewall(const std::string &name, uint32_t id,
                 const std::string &src_ag, const std::string &dst_ag,
                 const std::string &action,
                 const std::string direction="<>");
void AddFirewall(const std::string &name, uint32_t id,
                 const std::vector<std::string> &match,
                 TestTag *src, uint32_t src_count,
                 TestTag *dst, uint32_t dst_count,
                 const std::string action, const std::string direction="<>",
                 const std::string hbs="false");
void AddServiceGroup(const std::string &name, uint32_t id,
                     const std::vector<std::string> &protocol,
                     const std::vector<uint16_t> &port);
void CreateTags(TestTag *tag, uint32_t count);
void DeleteTags(TestTag *tag, uint32_t count);
void AddGlobalPolicySet(const std::string &name, uint32_t id);
void AddPhysicalDeviceWithIp(int id, std::string name, std::string vendor,
                             std::string ip, std::string mgmt_ip,
                             std::string protocol, Agent *agent);
void DelPhysicalDeviceWithIp(Agent *agent, int id);
void AddLocalVmRoute(Agent *agent, const std::string &vrf_name,
                     const std::string &ip, uint32_t plen,
                     const std::string &vn, uint32_t intf_uuid,
                     const Peer *peer);
void AddVlan(std::string intf_name, int intf_id, uint32_t vlan);
void AddLrVmiPort(const char *vmi, int intf_id, const char *ip,
               const char *vrf, const char *vn,
               const char *instance_ip, int instance_uuid);
void DelLrVmiPort(const char *vmi, int intf_id, const char *ip,
               const char *vrf, const char *vn,
               const char *instance_ip, int instance_uuid);
void SetIgmpConfig(bool enable);
void ClearIgmpConfig(void);
void SetIgmpVnConfig(std::string vn_name, int vn_id, bool enable);
void SetIgmpIntfConfig(std::string intf_name, int intf_id, bool enable);

void DeleteVxlanRouting();
void AddLrRoutingVrf(int lr_id);
void DelLrRoutingVrf(int lr_id);
void AddLrBridgeVrf(const std::string &vmi_name, int lr_id,
                    const char *lr_type = NULL);
void DelLrBridgeVrf(const std::string &vmi_name, int lr_id);
void CreateTransparentV2ST(const char *service_template, bool mgmt,
            bool left, bool right);
void DeleteServiceTemplate(const char *service_template);
void CreateServiceInstance(const char *service_instance,
            const char *mgmt, const char *mgmt_ip,
            const char *left, const char *left_ip,
            const char *right, const char *right_ip);
void DeleteServiceInstance(const char *service_instance);

#endif // vnsw_agent_test_cmn_util_h
