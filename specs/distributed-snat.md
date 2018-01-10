
# 1. Introduction
Distributed SNAT feature allows virtual machines to reach IP fabric network
using existing forwarding infrastructure present for compute node connectivity.

# 2. Problem statement
Virtual machines currently can have connectivity to external network via
floating-ip, logical-router or by having underlay forwarding enabled, all
of which needs extra routing information being pushed to underlay routers
or SDN gateways. Logical-router which already implements source NAT is
centralized and has one service instance to which all traffic is forwarded
for NAT.

Contrail could use the existing routing infrastructure providing compute node
connectivity and extended the same and provide underlay connectivity to
virtual machine. This would be acheived by port address translation of
virtual machine traffic with IP address of compute node.

The following use cases will be supported:
1. Virtual networks having distributed SNAT enabled would be able to
   communicate with IP fabric network. Session has to be initiated from
   virtual machine, initiation of session from external network would
   not be supported
2. Virtual network could be connected to ip-fabric network via logical router,
   in which traffic from private virtual network would be subjected to
   distributed SNAT to reach ip-fabric network.

# 3. Proposed solution
IP address configured on compute node would be used as public address aiding
in providing external network reachability to virtual machine hosted on that
compute node. Traffic from virtual machine would be source NAT with compute IP
and a user configured pool of ports would be used for source port translation.
Reverse traffic would undergo destination NAT with destination port translation

## 3.1 Alternatives considered

## 3.2 API schema changes
A virtual network would have a flag to enable distribute SNAT.

Global-vrouter-config would have a list of protocol and port range
which would be used for port translation. User would have option to specify
single range of port or number of ports in latter case random ports from
ephemeral port range would be reserved.

Virtual network could be connected to ip-fabric network via a logical router
in which case also distributed SNAT feature would be used to provide access to
ip-fabric network. To achieve this a  new flag would be introduced in
routing instance which would be enabled in private network VRF
to handle this scneario.

## 3.3 User workflow impact

## 3.4 UI changes
Add option in global-vrouter-config to specify protcol and port range or
protocol and number of ports which contrail-vrouter-agent would use for port
translation
Add option in virtual-network to enable distributed snat.

## 3.5 Notification impact
Interface UVE would have a new field to specify that distributed SNAT
is enabled. Exisiting Flow UVE handle flow based information for sessions

# 4. Implementation
## 4.1 Work items
### 4.1.1 Config impact - API server
Implicitly set distributed SNAT flag in routing instance once virtual network
is enabled for distributed SNAT or is connected to ip-fabric network
via logical router.

### 4.1.2 Vrouter impact
1. Once global-vrouter config is received agent would reserve number of ports
   specified in config by explicitly binding.

2. Once VRF is enabled for distributed SNAT each interface in that network would
   have a implicitly added floating-ip(vhost IP) with port translation flag
   enabled, and this floating-ip would be least preferred.

3. Port allocation for a flow would be done as below.
   * Hash table would be maintained with data of hash entry containing
     bitmap of used ports
   * Hash value would be derived from destination IP and port, a free port in
     the hash entry would then be used for port translation
   * Size of hash table would be configurable higher the number
     less likely chance of port allocation failure
   * Once port is allocated existing floating-ip changes in agent handle rest.
     Floating-ip added as part of distributed SNAT would be the least preferred
     i.e if virtual-machine-interface has any explicit floating-ip with
     destination route prefix length greater or matching as the one in
     ip-fabric network then explicit floating-ip would be preferred, the same
     applies even if explicit floating-ip belongs to ip-fabric network

### 4.1.3 Provisioning changes
Hash table size should be set in agent configuration file while provisioning
```
==============================================================================
CONFIG PARAMETER                          VALUE
==============================================================================
[FLOWS].fabric_snat_hash_table_size       Size of port translation hash table
                                          default value = 4098
```

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
* Enable fabric SNAT flags and verify port translation happens
* Change global-vrouter config and ensure deleted port have
  corresponding flow deleted and new ports are bound

## 9.2 Dev tests
* Verify distributed SNAT works with vhost policy disabled or enabled

## 9.3 System tests

# 10. Documentation Impact

# 11. References
