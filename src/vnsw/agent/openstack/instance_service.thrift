/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */

namespace java net.juniper.contrail.contrail_vrouter_api

typedef list<i16> tuuid

struct Port {
    1:required tuuid port_id,
    2:required tuuid instance_id,
    3:required string tap_name,
    4:required string ip_address,
    5:required tuuid vn_id,
    6:required string mac_address,
    7:optional string display_name,
    8:optional string hostname,
    9:optional string host; 
   10:optional tuuid vm_project_id; 
   11:optional i16 vlan_id; 
}

typedef list<Port> PortList

struct Subnet {
    1: required string prefix;
    2: required i16 plen;
}
typedef list<Subnet> SubnetList

struct VirtualGatewayRequest {
    1: required string interface_name;
    2: required string routing_instance;
    3: required SubnetList subnets;
    4: optional SubnetList routes;
}

typedef list<VirtualGatewayRequest> VirtualGatewayRequestList

service InstanceService {
    bool AddPort(1:required PortList port_list),
    bool KeepAliveCheck(),
    bool Connect(),
    bool DeletePort(1:required tuuid port_id),

    bool AddVirtualGateway(1:required VirtualGatewayRequestList vgw_list),
    bool DeleteVirtualGateway(1:required list<string> vgw_list),
    // ConnectForVirtualGateway can be used by stateful clients. It audits the
    // virtual gateway configuration. Upon a new ConnectForVirtualGateway
    // request, one minute is given for the configuration to be redone. 
    // Any older virtual gateway configuration remaining after this time is 
    // deleted.
    bool ConnectForVirtualGateway(),
    // Audit timeout of one minute can be modified using this, timeout in msec
    bool AuditTimerForVirtualGateway(1:required i32 timeout),

    bool TunnelNHEntryAdd(1:required string src_ip, 2:required string dst_ip, 3:string vrf_name),
    bool TunnelNHEntryDelete(1:required string src_ip, 2:required string dst_ip, 3:string vrf_name),

    bool RouteEntryAdd(1:required string ip_address, 2:required string gw_ip, 
                       3:string vrf_name, 4:string label),
    bool RouteEntryDelete(1:required string ip_address, 2:required string vrf_name),

    bool AddHostRoute(1:required string ip_address, 2:string vrf_name),
    bool AddLocalVmRoute(1:required string ip_address, 2:required string intf_uuid,
                         3:string vrf_name, 4:string label),
    bool AddRemoteVmRoute(1:required string ip_address, 2:required string gw_ip,
                          3:string vrf_name, 4:string label),

    bool CreateVrf(1:required string vrf_name),
}
