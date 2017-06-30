
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

Upto R4.0, IP fabric network (present in the default project) has no IPAM
associated with it. Support to spawn VMs in the IP fabric network is not
available. There is no possibility of applying security groups for interfaces
in IP fabric network, DHCP / DNS requests from such interfaces are not
supported.

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

Two IPAM addressing schemes will be supported for IP fabric:
* Common subnet mode having a set of subnet prefixes (mode currently supported).
To scale up underlay routing, block allocation per vrouter will be supported,
such that address blocks are advertised instead of individual addresses.
* Prefix per vrouter mode (new mode).
Every vrouter/compute node gets its own prefix. IP address to VMI allocation
happens after scheduling decision for VM/container is made. While this can be
supported for K8S and Mesos without restrictions, This scheme is not very
useful for Openstack, since address is needed before scheduling decision.
If user assigns an address and dictates the scheduling decision, this can be
used in Openstack.

When a VMI is created in the IP fabric network, vrouter exports an L3VPN route
for the same in the ip-fabric:ip-fabric routing instance, with the vrouter as
nexthop (along with MPLS label, policy tags, security group tags etc). An Inet
route is exported in the ip-fabric:__default__ routing instance, with the
vrouter as nexthop. Vrouters use the ip-fabric routing instance to apply policy
and the default routing instance to forward traffic.

Compute node will also peer with TORs and will have a mechanism such that its
publishes the routes whose (vrouter) nexthops are hanging off the TOR. It
is expected that TOR propagates these routes to the rest of the underlay network. 
When using the prefix per vrouter mode, TOR may also be configured with static
routes pointing to the compute nodes, instead of peering with the control node.

Vhost interface is also added in default routing instance. Policy and security
groups can be applied on this interface as well, so that traffic from the
applications / services running on the host can be subjected to all policy
decisions possible in Contrail.

## 3.1 Alternatives considered

## 3.2 API schema changes
Schema would be updated to provide configuration for control node to share
relevant routes with TORs (in case control node peers with TOR).

## 3.3 User workflow impact
Provisioning will create VMI for vhost interface. Creation of IPAM for IP fabric
network, policy / security group configurations for vhost interface, spawning
VMs / containers in IP fabric network can now be done.

## 3.4 UI changes
Allow the possibility of above mentioned workflows.

## 3.5 Notification impact
Existing UVEs will now extend to the IP fabric.


# 4. Implementation
## 4.1 Work items
* Config impact - API server will create additional routing instance in IP
fabric network for storing forwarding routes. Schema shall allow vrouter to be
the parent of vhost VMI objects.

* Control node impact - Publishing relevant routes to TOR.

* Vrouter impact - handling new IP fabric VRF, support for VMs in IP fabric
network, ensuring data and control use appropriate VRFs to make policy
decisions for IP fabric traffic.

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
