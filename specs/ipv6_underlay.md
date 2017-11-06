
# 1. Introduction
Support IPv6 underlay in OpenContrail. All changes must be backwards compatible with IPv4 underlays. OpenContrail should be operable in IPv4-only, IPv6-only and dual-stack environments.

# 2. Problem statement
Large data centers today accommodate tens of thousands physical hosts running dozens of VMs or hundreds of containers. The connection matrix is dense and high-speed non-blocking switch fabrics facilitate it. In order to preserve hardware resources (e.g. TCAM), the infrastructure relies heavily on network aggregation at all levels of the switch fabrics. This hardly can be done with IPv4 at scale mentioned above. To workaround this, different techniques could be employed, such as overlays or carrier-grade NAT, but there are caveats. An alternative route is to implement a pure IPv6 fabric.


# 3. Proposed solution
IPv6 should enjoy the same level of support in OpenContrail as IPv4 when it comes to BGP peering, connecting vRouter Agents to control nodes and control nodes to config nodes. There are some exceptions, however:

* FreeBSD is left out of the scope for this blueprint unless someone volunteers to contribute and test IPv6 support for FreeBSD.

* GRE support for IPv6 is also left out of scope. This is intentional due to possible (yet unlikely) security issues [1].

Moreover, this blueprint doesn't specify how OpenContrail gains external connectivity over the IPv6 underlay network. Most hardware gateways require IPv4 underlays, so typically a software appliance will be used instead.

## 3.1 Alternatives considered
Leave things as they are. Requires no efforts, but leaves OpenContrail incompatible with IPv6-only environments. Those are rare yet the more we support, the more deployments we are likely to get.

## 3.2 API schema changes
Wherever IPv4 is accepted as a parameter value in the REST API, IPv6 should be accepted as well.

## 3.3 User workflow impact
A user can specify IPv6 address where only IPv4 addresses were accepted previously, that's it.
Bracketed ( [fd01::1] ) forms should be recognized, so the user can still use `ip:port` syntax unambiguously.

## 3.4 UI changes
Wherever IPv4 is accepted as a parameter value in the Web UI, IPv6 should be accepted as well.

## 3.5 Notification impact
Most of the time, IP addresses are sent as string values in UVEs, so no changes are supposedly needed. Wherever it is not true, IPv6 addresses should be accepted in the fields which were previously IPv4-only.

# 4. Implementation

IPv6 underlay support involves changes in the majority of the OpenContrail's components.

## 4.1 vRouter and vRouter-DPDK
vRouter and vRouter-DPDK rely on a single library (`dp-code/`) to perform the encapsulation. IPv6 is already supported there, except for GRE. Receive-side decapsulation paths are different: both already process IPv6 packets but don't check them for encapsulated payloads. NAT66 support is missing, which could be probably left as is until we want IPv6 Floating IPs (unlikely) or BGPaaS for IPv6 peers (to be decided). We can drop IPv6 with extensions headers to simplify the implementation. Command-line tools are to be tweaked as needed.

##4.2 vRouter Agent
vRouter Agent seems to be the most affected component. BGPaaS, metadata proxy, service instances manager and diagnostics packets handler (`diag/`) are IPv4. This isn't necessarily related to IPv6 underlay, but as we are making IPv6 a first-class citizen in Contrail, this may need to be fixed. On the lower level, IPv6-related fields are to be added to various data structures such as `TunnelNH`. The agent must also be able to connect to control nodes over IPv6. Changes to the xmpp and DNS libraries should solve this (see below). "Wrapped-in" services such as contrail-named (BIND) needs to be re-configured to listen on IPv6 addresses.

##4.3 Control plane C++ services
Most control plane C++ services rely on a set of libraries for network communication. `io/`, `http/`, `xmpp/` and `utils/` are examples. These libraries are IPv6-ready, with some tweaks needed to create v4 or v6 connections based on the endpoint's IP address type. Moreover, the `GetHostIp()` utility function should be able to return IPv6, including when it appears to be a preferred address on a dual-stack node.

##4.4 Configuration node services
Configuration node services are written in Python 2 with gevent. The latter and other libraries used (such as kazoo and kombu) are IPv6-ready. Config node services largely rely on string endpoint representations such as URLs which are resolved, providing transparent support for IPv6. Again, getting the host's IP address needs some tweaks as well as IP validation routines in api-server. The latter should accept IPv6 where IP address is to be provided, except in places where IPv4 is explicit (such as A records in Virtual DNS).

##4.5 Analytics and web ui
At the very minimum, web ui needs to accept, validate and display IPv6 properly. It also must be operable over an IPv6 network, yet nginx proxy can be used to work-around this. The same stands for HTTP analytics services; C++ services such as collector or qed obtain their IPv6 connectivity with changes to common libraries (see above). Sandesh as a transport protocol seems to be IPv6 ready, yet individual messages and their producers/consumers need to be tweaked.

##4.6 Miscellaneous
IPv4 addresses are commonly used as 32-bit unique values or identifiers in many networking protocols. When it comes to OpenContrail and BGP, Route Distinguisher attribute and Router Id are both valid examples.

However, using IPv4 addresses in such situations is more of a convention (yet an ubiquitous one) than a hard requirement. Moreover, for BGP, identifiers typically need to be unique only within the AS, or even between peers. BGP attributes typically provide a way to express themselves uniquely this way, such as Route Distinguisher Type 0/2. In other cases (Router Id) a plain value can be used as well. A configuration file option can be used to make the value unique across peers within the Contrail cluster. For values that only need to be unique within the router, a monotonically increasing counter can be used.

# 5. Performance and scaling impact
## 5.1 API and control plane
None

## 5.2 Forwarding performance
IPv6 has a larger header which means (slightly) larger overhead per packet. This may negatively affect the performance, but it is unlikely to be noticeable in non-DPDK usage scenarios.

# 6. Upgrade
No database schema changes are supposedly needed.

# 7. Deprecations
None

# 8. Dependencies
No dependencies

# 9. Testing
## 9.1 Unit tests
Existing unit tests for should be extended to cover IPv6-only endpoints scenarios. This is to be done as the feature is developed, and should be ready by 01/30/2018.
## 9.2 Dev tests
None
## 9.3 System tests
Test lab setup should be used to verify OpenContrail is operable in IPv6-only networks, and no regressions were introduced for IPv4. It is also important to carry out data plane performance test to measure the IPv6 overhead effect. These tests are to be finished by 02/15/2018.

# 10. Documentation Impact
The documentation should mention that IPv4 and IPv6 addresses can be used interchangeably.

# 11. References
1. [RFC 7676, Section 2](https://tools.ietf.org/html/rfc7676#section-2)
