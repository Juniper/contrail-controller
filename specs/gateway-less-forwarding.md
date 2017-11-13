
# 1. Introduction
For simple Enterprise use cases and public cloud environment, it is
advantageous to directly route packets using IP fabric network without a
need for SDN gateway.

# 2. Problem statement
The primary reason for contrail in this mode is to manage distributed
security policy for workloads or baremetal servers.
* Network Policy support for IP-fabric
* Security groups for VMs / Containers on IP fabric
* Security groups for vhost0 interface, to protect compute node or baremetal
server applications
* Support for service chaining may be required, if policy dictates that
traffic goes thru a service chain.

Upto R4.0, there was no option for underlay forwarding. From R4.1, a virtual
network can be marked for IP fabric based forwarding (without tunneling). When
two VNs having such configuration communicate, traffic will be forwarded
directly using underlay.

The following use cases will be supported:
1. Virtual networks having IP subnet which is a subset of the IP fabric network
or a different subnet and using IP fabric network as the provider network. VMs /
containers from these VNs communicate within their VNs, with IP fabric VN and
with other VNs which also have IP fabric as their provider network based on
policy configured, using only underlay (no tunneling).

2. Virtual networks having IP fabric VN as their provider network communicating
with other VNs which do not have any provider network configured based on
policy configured, using overlay (with tunneling).

3. Vhost communication with other compute vhosts and with VMs / containers in
IP fabric network or other VNs having IP fabric network as they provider network
based on policy configured, using underlay (no tunneling).

4. Vhost communication with VMs in any virtual network based on policy
configuration, using overlay (with tunneling).

# 3. Proposed solution
IP fabric network will have two routing instances associated:
* Default routing instance (ip-fabric:__default__, which is already present
in all earlier releases as well). This will be used for all forwarding decisions
by the data path.
*  A new routing instance (ip-fabric:ip-fabric) will be created. This will
carry L3VPN routes for end points in IP fabric. Network policy and security
groups will be applied based on these entries.

IP fabric network can now have an IPAM associated, having its subnets. IPAM for
IP fabric will always use flat subnet mode. In this mode, same subnet can be
shared with multiple virtual networks. The IP fabric IPAM will have the covering
subnet, with other virtual networks using blocks from this subnet.

Two IPAM addressing schemes will be supported for VNs having IP fabric
forwarding configured:
* Common subnet mode having a set of subnet prefixes (mode currently supported).
* Prefix per vrouter mode (new mode).
To scale up underlay routing, block allocation per vrouter will be supported,
such that address blocks are advertised instead of individual addresses.
Every vrouter/compute node gets its own prefix. IP address to VMI allocation
happens after scheduling decision for VM/container is made. While this can be
supported for K8S and Mesos without restrictions, this scheme is not very
useful for Openstack, since address is needed before scheduling decision.
If user assigns an address and dictates the scheduling decision, this can be
used in Openstack.

When a VMI is created in the IP fabric network, vrouter exports an L3VPN route
for the same in the ip-fabric:ip-fabric routing instance, with the vrouter as
nexthop (along with MPLS label, policy tags, security group tags etc). An Inet
route is exported in the ip-fabric:__default__ routing instance, with the
vrouter as nexthop. Vrouters use the ip-fabric routing instance to apply policy
and the default routing instance to forward traffic.

Control node will also peer with TORs and will have a mechanism such that its
publishes the routes whose (vrouter) nexthops are hanging off the TOR. It
is expected that TOR propagates these routes to the rest of the underlay network. 
When using the prefix per vrouter mode, TOR may also be configured with static
routes pointing to the compute nodes, instead of peering with the control node.

Vhost interface is also added in default routing instance. Policy and security
groups can be applied on this interface as well, so that traffic from the
applications / services running on the host can be subjected to all policy
decisions possible in Contrail.

IP Fabric network will be L3-only network and vrouter only looks at the routing
table for all forwarding decision.

## ARP Handling
ARP requests in the IP Fabric network and in VNs having IP Fabric network as
provider network will be handled as follows.

1. VM to VM communication (on same compute or on different compute nodes) :
Respective vrouters respond to ARP requests from the VMs with vrouter's MAC.
Agent resolves the ARP for other compute nodes to fill the nexthop corresponding
to remote VMs. 

2. Vhost connectivity to VM on same compute node :
Vrouter responds with vhost MAC (its own MAC) for ARP requests from vhost. ARP
requests from VM will be responded with vrouter's MAC.

Each subnet in these networks (VNs using IP Fabric as the provider network)
will have a subnet route in the compute host pointing to vhost interface.
There would be an L3 route in the Fabric default Vrf for each
VM, with nexthop pointing to its VMI. Traffic is forwarded to the VM based on
this route. The nexthop would be an L3 interface nexthop with source mac being
vrouter’s MAC.

When vhost and VN are using different subnets, ARP request from vhost has
VM's IP as destination IP and vhost IP as source IP. Vrouter responds to ARP
request with vhost MAC. 

3. Vhost connectivity to VM on different compute node :
Vrouter responds with vhost MAC (its own MAC) for ARP requests from vhost. ARP
requests from VM will be responded with vrouter's MAC.

Route in the default VRF points to a tunnel nexthop destined to the compute node
hosting the VM. The compute node which has the VM hosted, has an L3 route for
the VM with nexthop pointing to its VMI.

VM’s ARP request is also always responded to with vrouter’s MAC.

4. Vhost connectivity to another compute node :
Similar to (3) above.

## Broadcast / Multicast
Broadcast or multicast traffic from VMs in the virtual networks having
IP Fabric network as provider network will be forwarded using overlay. DHCP
requests from these VMs will be served by vrouter agent.

## 3.1 Alternatives considered

## 3.2 API schema changes
A virtual network can have provider network configured using a link from the VN
to IP Fabric VN.

vRouter specific IP allocation pool can be created. If an instance IP is created
with a link to a vrouter and vrouter is linked with a flat subnet IPAM, then the
instance IP is allocated an address from the vrouter specific allocation pool.

## 3.3 User workflow impact
Provisioning will create VMI for vhost interface. Creation of virtual networks
with IP fabric forwarding, policy / security group configurations for vhost
interface can now be done.

## 3.4 UI changes
Allow the possibility of above mentioned workflows.

## 3.5 Notification impact
Existing UVEs will now extend to VNs using IP fabric forwarding.


# 4. Implementation
## 4.1 Work items
* Config impact - API server will create additional routing instance in IP
fabric network for storing forwarding routes. Schema shall allow vrouter to be
the parent of vhost VMI objects. Also, handling of vrouter specific allocation
pool will be added.

* Control node impact - Publishing relevant routes to TOR.

* Vrouter impact - handling new IP fabric VRF, support for virtual networks with
IP fabric forwarding, ensuring data and control use appropriate VRFs to make
policy decisions for IP fabric traffic.

* UI impact - Allow IPAM creation in IP fabric network, vhost interface policy
configurations etc.

* Provisioning impact - Create vhost VMI objects.

# 5. Performance and scaling impact
## 5.1 API and control plane
No additional impact expected.

## 5.2 Forwarding performance
No additional impact expected.

# 6. Upgrade
No impact.

# 7. Deprecations
None.

# 8. Dependencies

# 9. Testing
## 9.1 Unit tests
Revisit existing UTs to add similar IP fabric scenarios.

## 9.2 Dev tests
* Policy on vhost interface.
* VMs in IP fabric network.
* Policies between IP fabric network and other virtual networks.

## 9.3 System tests

# 10. Documentation Impact

# 11. References
