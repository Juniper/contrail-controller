
# 1. Introduction

 - CONTRAIL – EVPN Support for Baremetal Devices
 - QFX Device Configuration Using Netconf Protocol

# 2. Problem statement

EVPN – Contrail:
-----------------
   -  Contrail enables users to use EVPN-VXLAN when the network includes both virtual and bare- metal devices.
      MX Series routers can use EVPN-VXLAN to provide both Layer 2 and Layer 3 connectivity for end stations
      within a Contrail virtual network (VN).

   -  Two types of encapsulation methods are used in virtual networks:

       *  MPLS-over-GRE (generic routing encapsulation) is used for Layer 3 overlay virtual
          network routing between Contrail and MX Series routers.

       *  EVPN-VXLAN is used for Layer 2 overlay virtual network connectivity between Virtual Machines on Contrail,
          Bare-metal servers attached to QFX Series switches, and their respective layer3 gateway configured on the QFX.
          Subsequently, inter-VXLAN routing between Virtual Machine and Bare-metal servers, and
          between Bare-metal servers on different VXLAN VNI, is performed on QFX.

   -  With Contrail, Layer 3 routing is preferred over Layer 2 bridging whenever possible.
      Layer 3 routing is used through virtual routing and forwarding (VRF) tables between Contrail
      vRouters and physical MX/QFX Series routers.

   -  When there are multiple VxLAN networks connected to a spine switch and if the
      hosts in the networks needs to talk to each other, we would have to export Route Targets (RTs)
      of every VN to every other VN. This will result in scalability issues because if
      you have 'm' networks with 'n' routes each, then the VRF of every VN will have
      'm*n' routes in it and system as a whole needs to store 'm ^ 2 * n' routes.
      This can exhaust the system resources quickly. The solution for this scaling issue is discussed in section 3G.

Software Requirements:
----------------------
    QFX 5100/5110 – Release Junos 17.2 and later versions.
    QFX 10K - Release Junos 17.2 and later versions.
    Contrail : R4.1 or later.
    Contrail Device Manager uses following software QFX sofware versions during development & testing:
     QFX10k :  jinstall-host-qfx-10-f-x86-64-17.2-20170516_17.2T.0-secure.tgz
         Build Availablle in : eng-shell: /volume/build/fsg/17.2T-yocto-tvp-10k-5e/17.2/current/sb-linux/sb_junos-app/ship-OCCAM64WRL7
     QFX5100 : jinstall-qfx-5-17.2-20170517.0-img.tgz
         Build Available in : /volume/build/fsg/RELEASE_172_THROTTLE-vjunos/17.2/current/sb-junos/ship


# 3. Proposed solution

Configuration Topologies:

   1) 10K Acting as Spine (L3 Gateway), 5K acting as Leaf (L2 Gateway), Hosts are connected to Leafs

          10K    10K (L3 Gateway)
           |  \ / |
           |  / \ |
           5K    5K  (L2)
           |      |
          Host   Host

   2) 10K Activing  as Spine (L3), 5K Acting as Leaf (l2 Gateway),  switch is connected to Leaf, and Hosts are connected to Switches (Not supported in 4.1)

          10K    10K (L3 Gateway)
           |  \ / |
           |  / \ |
           5K    5K  (L2 )
           |      |
            switch
            |    |
          Host   Host

   3) 10K Acting as Leaf (L3 Gateway), hosts are directly connected to 10K

          10K    10K (L3 Gateway)
           |  \ / |
           | /  \ |
          Host   Host


A) BGP Configuration (for both 10K & 5K):

   BGP configuration is very similar to MX Router BGP configuration, except "dynamic tunnels". QFX10K/5K does not support these in 17.2
   Following protocol configurations are supported:
      - BGP Peering (internal & external)
      - Hold timer, Auth, iBGP Export policy
      - Address Families
      - AS Configuration
      - router-id & dataplane ip configuration
      - loopback interface configuration
      - Supported Family types: evpn, router target only.

B) EVPN Configuration:

    • Configuring EVPN with VXLAN encapsulation handles Layer2 connectivity at the scale required
    • Configure extended-vni-list all to list the VXLAN Network Identifiers (VNIs) that are part of the EVPN/VXLAN MP-BGP domain
    • Configure ingress-replication to facilitate the provider tunnel to use unicast tunnels between routers to create a multicast distribution tree.
    • For L3 Spine only, configure default-gateway ("no-gateway-community")
    • For L3 Leaf only, configure default-gateway ("do-not-advertise")

    Sample Configuration:
        set protocols evpn vni-options vni 1 vrf-target target:10003:1
        set protocols evpn vni-options vni 2 vrf-target target:10003:2
        set protocols evpn encapsulation vxlan
        set protocols evpn extended-vni-list all
        set protocols evpn default-gateway no-gateway-community (For L3)

C) Configure interfaces with corresponding VLAN Members & Associated VNI:

    vlans {
       data {
         vlan-id none;
         l3-interface irb.82;   # For L3
       }
       v1 {
          vlan-id none;
          vxlan {
               vni 1;
          }
       }
       v6 {
          vlan-id none;
          vxlan {
             vni 2;
          }
      }
    }

    # Note: ingress-node-replication is not needed

D) Switch Options (for both L2 & L3):
    - Configure the policy-statements under policy-options to set a specific route preference using communities
    - Configure the source interface for a VXLAN tunnel
    - Configure an import and export policy, include the vrf-import statement
    - Configure route-distinguisher to distinguish one set of routes (one VRF) from another

    Sample Configuration:

        policy-options {
            policy-statement IMP {
            term 1 {
                from community com6;
                then accept;
            }
            term 2 {
                from community com5;
                then accept;
            }
            then reject;
         }
         community com5 members target:10003:1;
         community com6 members target:10003:2;
       }

      switch-options {
           vtep-source-interface lo0.0;
           route-distinguisher 64520:1;
           vrf-import IMP;
           vrf-export IMP;
      }

E) Interfaces Config:

    - For L3, configure IRB interfaces (one for each bridge domain) and place them in customer RI
    - 10K Acting as L3 Spine Gateway :
        * Configure "proxy-macip-advertisement" on 10K IRB interfaces (if 10K is activng as Spine), this enables the switch that functions
          as a Layer 3 gateway in an Ethernet VPN-Virtual Extensible LAN (EVPN-VXLAN)
          with integrated routing and bridging (IRB) interfaces that advertise the MAC and IP routes (MAC+IP type 2 routes) for hosts in the topology.
        * Configure ip, virtual-gateway-ip for each irb
        * If hosts are connected to switch, and switch inturn is connected to L3 Spine via L2 Leaf, then on the Spine we will need to configure
          MAC address for each irb (virtual-gatewa-ip mapped to mac)

    - 10K  Acting as L3 Leaf gateway:
        * for each irb, allocate ip and static mac (Confirmation needed)

    - on 10K, lo0 interface should be configured with a inet family unit (from the contrail point of view this is bogus ip, ??)
      and add this interface in customer facing routing instance.

    - configure lo0.0 as vtep-source-interface (both 10K & 5K, this is same as MX configuration)

F) Routing Instances
    - Configure vrf type routing instances for each client network, allocate irb interfaces for each vlan associated to this network and place in RI.
    - allocate lo0 interface for each client network, and place it in routing instance. Need to allocate for each lo0 interface, this ip can be allocated
      from implicit vn's subnet.
    - allocate ip for each irb, and add a static route next hop pointing to irb interface
    - Note: #irb interfaces are needed only for client networks only if there is logical router associated to network
    - if VXLAN routing flag is not enabled for the project, dont program VNs on 10K

G) The concept of a Logical Router (LR) is already present in Contrail as part
   of the SNAT solution where a LR is created for the private VNs to talk to the
   Public cloud. We will use the LR entity here to facilitate VxLAN Routing.


            ----------------             -----------               --------
            |               |           |           |    routes   |       |
            |    Logical    |---------->|   VN-Int  | <---------> |  QFX  |
            |    Router     |           |           |             |       |
            |               |            -----------               --------
            -----------------            RT-Int containing
                |        |               RI1, RI2,....
                |        |
                v        v
             -------    -------
            |       |  |       |
            |  VN1  |..|  VN2  |
            |       |  |       |
             -------    -------
                RT1       RT2
                (Export RT-Int)

   A new knob is introduced to enable Inter-VxLAN routing (say enable_vxlan_routing)
   at the project level. When this knob is enabled for a project, an internal system
   VN (VN-Int) is created for every LR in the project. The VNI for this internal VN
   can be configured as part of the LR configuration or auto-generated if it is not specified.

   Now, all the routes in the VNs that are connected to the LR will be
   exported to the RT-Int of VN-Int. The VN-Int RI will have cumulative routes
   for all the networks connected to the LR. The advantage of this approach is
   that the routes need not be repopulated for every VN multiple times but needs to be
   done only once for the VN-Int which helps in scalability.

   The VN-Int (RT-Int) can be configured in the QFX switch so that these routes
   can be leaked to the QFX IRB interface.

Contrail API & Device Manager Enhancements:
-------------------------------------------
   - QFX is modelled as PhysicalRouter in Contrail DB. Prior to 4.0,  A Physical Router has attributes "Vendor", "Product".
   - Separate plugins will be implemented for QFX 5K & 10K
   - Logical Router can be extended to Physical Router, configure route targets/interfaces based on this association on QFX
   - add an explicit role (spine/leaf) for the physical-router. Some knobs (e.g. proxy-mac-ip-advertisement) should
     only be configured on spine switches (they should not be configured on leafs, even if the LR is extended to a leaf QFX10K)

## 3.1 Alternatives considered

## 3.2 API schema changes
   - Logical Router can be extended to Physical Router, configure route targets/interfaces based on this association on QFX
   - add an explicit role (spine/leaf) for the physical-router. Some knobs (e.g. proxy-mac-ip-advertisement) should
     only be configured on spine switches (they should not be configured on leafs, even if the LR is extended to a leaf QFX10K)
   - Device Schema Changes: https://review.opencontrail.org/31806

## 3.3 User workflow impact
   - Design physical topology and configure Contrail VNC Database using VNC APIs or Contrail Web UI based on the topology
   - QFX physical routers with proper credentials need to be configured
   - Configure BGP peers
   - Virtual Networks & Logical routers need to be extended to Physical Routers
   - Configure interfaces, vlans & ipams

## 3.4 UI changes
   - Allow to extend LR to physical router
   - Allow to configure QFX product name
   - Allow to configure physical router role : spine or leaf
   - Allow VNI to be configured for the Logical Router if auto configuration is disabled
     in the Global System config.

## 3.5 Notification impact
   - Log if there is any error while pushing the config, or if configured device parameters are not match with actual device parameters.

# 4. Implementation
## 4.1 Work items
   - Introduce plugin kind of architecture for configuring different network devices, one for each device type as needed.
   - De-couple XML Generation/NetConf integration from DM process.
   - Plug-and-play architecture: Any external device mgmt module can be plugged into DM during load time/compile time.
   - DM provides generic abstract interface methods, every external plugin has to implement them.
   - Example interfaces:
     - Register Plugin (When DM starts up), with vendor/product name; Based on product name, DM will use appropriate plug-in for push.
     - device_connect(), device_disconnect(), is_connected(), device_send(), device_get(), push_conf()
     - DM loads all plugins at start-up time, plugins self register with DM. They supply their product/vendor names.
     - DM implements state machine for connection mgmt, ultimate connections are managed by plugins.
   - Plugin is designed as follows:
       - MX Plugin: DeviceConf (Abstract Base Class with interfaces to be used by DM) > JuniperConf(Common Base of Juniper Devices) > MxConf
       - QFX 5K Plugin: DeviceConf (Abstract Base Class) > JuniperConf(Common Base for Juniper Devices) > QfxConf > QfxConf5K
       - QFX 10K Plugin: DeviceConf (Abstract Base Class) > JuniperConf(Common Base for Juniper Devices) > QfxConf > QfxConf10K

   - With this approach, we could support easily external device management handlers/openconfig easily without modifying Core DM implementation.
   - Add Device Schema for QFX configuration elements
   - Transalate VNC config to device config and push the config, log messages as needed.
   - Schema Transofrmer changes
     1. Schema changes
        a. Change VNC schema to accomodate vxlan_routing as a property in the Project object.
        b. "Configured VNI" property for Logical Router which is assigned to the Internal VN
           (VN-Int) if auto generate for VNI is disabled in Global System Config.
     2. Create one internal VN for every LR in the project.
     3. Export all the RTs of the VNs connected to the LR to the internal VN's VRF.
     4. Disallow disabling enable_vxlan_routing if the Project has at least one LR.
     5. When the LR is deleted, clean up internal VN associated with the LR.

## 4.2 Open Issues
   - Need to explore if "route-distingusher-id" configuration is really needed at switch-options level as discussed in 3.D
      - This is needed
   - Need to explore if "vrf-export" policy is really needed
      - this is needed as per deployments
   - Need to explore if "vlan-id" None cane be configured for each vlan in vxlan domain
      - vlan-id None is not supported in 17.2, support is available 17.3 onwards
   - vrf-target can be set to "auto"  at switch-options level, if this is set, AS number should be common across all "Spine" L3 gateways.
     AS number must be configured at global-routing-options level.

## 4.3 QFX 5K Full working configuration
   - To be added

## 4.4 QFX 10K Full working configuration
   - To be added

# 5. Performance and scaling impact
## 5.1 API and control plane
#### Scaling and performance for API and control plane

## 5.2 Forwarding performance
#### Scaling and performance for API and forwarding

# 6. Upgrade
   - None

# 7. Deprecations
   - None

# 8. Dependencies
  -  When there are multiple VxLAN networks connected to a spine switch and if the hosts in the networks
     needs to talk to each other, we would have to export Route Targets (RTs) of every VN to every other VN which results into a scalability issue.
  -  The feature Inter-VxLAN routing with Contrail overlay solves the scalability issue as discussed

# 9. Testing
## 9.1 Unit tests
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References

