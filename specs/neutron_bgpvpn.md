# 1. Introduction
Add the support to the new OpenStack Neutron API extension named **Networking BGP VPN** to Contrail which was added to supporting inter-connections between L3VPNs or E-VPNs and Neutron resources, (i.e. Networks, Routers and Ports).
This extension uses the [plugin Neutron framework](https://wiki.openstack.org/wiki/Neutron/ServiceTypeFramework) and defines a new resource named *bgpvpn* that contains all [attributes](http://docs.openstack.org/developer/networking-bgpvpn/api.html#bgpvpn-resource) needed to declare a BGP VPN connection. Then, that connection can be associated/disassociated to networks or/and routers by users.

# 2. Problem statement
A typical use-case is the following:
* a tenant already having a BGP IP VPN (a set of external sites) setup outside the datacenter, and they want to be able to trigger the establishment of connectivity between VMs and these VPN sites.
* Another similar need is when E-VPN is used to provide an Ethernet interconnect between multiple sites.

# 3. Proposed solution
Extend the Contrail schema with that new BGP VPN resource and use the schema transformer to apply BGP VPN's route targets to the associated network resources (ie. routing instance).

## 3.1 Alternatives considered
N/A

## 3.2 API schema changes
Add new BGP VPN resource to Contrail data model named `bgpvpn`:

Resource attributes  | Operations | Type
-|-|-
Project Owner | CR | Parent reference
Type | CR | Property `VpnType` string enum `['l2', 'l3']`
Route targets | CRU | ListProperty `RouteTargetList`
Import targets | CRU | ListProperty `RouteTargetList`
Export targets | CRU | ListProperty `RouteTargetList`
Networks | CRU | `virtual-network` back reference
Routers | CRU | `logical-router` back reference

For the ` bgpvpn` `type` attribute, only the type `l3` can be used for `bgpvpn` associated to `logical routers`. About the `virtual network` association authorizations depend on the `forwarding mode` set on the `virtual network` as describe below:

Forwarding mode | Authorized VPN types
-|-
`l2_l3` | `l2` and/or `l3`
`l2` | `l2` only
`l3` | `l3` only

`forwarding mode` update on a `virtual network` can fail depends of the `bgpvpn` `type` associated to it.

## 3.3 User workflow impact
That feature was designed for the OpenStack Neutron API. Only the cloud administrator can create, allocate to a tenant, update and delete the `bgpvpn` resource. User can only list them (eventually update the name) and define association with network resource (ie. `virtual networks` and `logical router`).

On the contrail side, we will add that resource without any constraints on resource manipulation.

## 3.4 UI changes
N/A

## 3.5 Notification impact
N/A

# 4. Implementation

## 4.1 Assignee(s)
### Developers
* Édouard Thuleau

### Reviewers
* Sachin Bansal

## 4.2 Work items
### Contrail schema
Add the new `bgpvpn` resource to the Contrail data model as describe in 3.2

### VNC API server
Add some checks on the association of a BGP VPN resource to the other resource to avoid any ambiguity like it's describe in the [BGP VPN documentation](http://docs.openstack.org/developer/networking-bgpvpn/api.html#association-constraints).

### Schema transformer
Add a new ST class to support that new `bgpvpn` resource and add code in classes `VirtualNetworkST` and `LogicalRouterST` to merge BGP VPN's route targets.

### Neutron BGP VPN driver
Write a driver for the Neutron BGP VPN extension based on that new `bgpvpn` resource.

# 5. Performance and scaling impact
## 5.1 API and control plane
Insignificant impact on the config and control.

## 5.2 Forwarding performance
Add some easy routing stuff on vrouter to leak routes between VRFs accordingly to defined route targets.

# 6. Upgrade
First implementation was done by Orange/Cloudwatt based on the Contrail key/value exposed through the VNC API which was a proof of concept first implementation.

We could easily add a mechanism to detect if used Contrail API support the new `bgpvpn` resource, if not fallback to the first driver and propose a migration script (or an automatic migration which detects `bgpvpn` resources in kv store when driver is initializing).

# 7. Deprecation
* No deprecation on the Contrail API.
* Deprecates the first Neutron BGP VPN Contrail driver.

# 8. Dependencies
N/A

# 9. Testing
## 9.1 Unit tests
Add unit tests to the added code on the config side.

## 9.2 Dev tests

## 9.3 System tests

# 10. Documentation Impact

# 11. References
* Blueprint: https://blueprints.launchpad.net/opencontrail/+spec/bgpvpn
* Neutron BGP VPN extension documentation: http://docs.openstack.org/developer/networking-bgpvpn/index.html
