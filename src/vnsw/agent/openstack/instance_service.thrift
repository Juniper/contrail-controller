/*
 * Copyright (c) 2013 Juniper Networks, Inc. All rights reserved.
 */
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

service InstanceService {
    bool AddPort(PortList port_list),
    bool KeepAliveCheck(),
    bool Connect(),
    bool DeletePort(tuuid port_id),

    bool TunnelNHEntryAdd(1:required string src_ip, 2:required string dst_ip, 3:string vrf_name),
    bool TunnelNHEntryDelete(1:required string src_ip, 2:required string dst_ip, 3:string vrf_name),

    bool RouteEntryAdd(1:required string ip_address, 2:required string gw_ip, 
    	               3:string vrf_name, 4:string label),
    bool RouteEntryDelete(1:required string ip_address, 2:required string vrf_name),

    bool AddHostRoute(1:required string ip_address, 2:optional string vrf_name),
    bool AddLocalVmRoute(1:required string ip_address, 2:required string intf_uuid,
    	                 3:optional string vrf_name, 4:optional string label),
    bool AddRemoteVmRoute(1:required string ip_address, 2:required string gw_ip,
    	  	          3:optional string vrf_name, 4:optional string label),

    bool CreateVrf(1:required string vrf_name),
}
