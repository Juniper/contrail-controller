
# 1. Introduction
Introduce IPv6 reverse zone (`ip6.arpa`) generation for OpenContrail's Virtual DNS.

# 2. Problem statement
OpenContrail's Virtual DNS creates a reverse DNS zone (`in-addr.arpa`) per each IPv4 subnet associated with the IPAM object attached to the vDNS instance.
Doing the same for an IPv6 subnet is left as a TODO item.

# 3. Proposed solution

## 3.1 Alternatives considered
None

## 3.2 API schema changes
In PTR records validation, `ip6.arpa` is recognized as a valid name suffix.

## 3.3 User workflow impact
A user gains the ability to resolve IPv6 addresses allocated to VMs. To do so, they issue a PTR request to OpenContrail vDNS server, either indirectly (via vRouter Agent trap) or directly, if external visibility is enabled in vDNS.

Reverse zone generation for IPv4 is not affected.

## 3.4 UI changes
A user is able to create PTR records in `ip6.arpa` zone. No other visible changes planned.

## 3.5 Notification impact
None

# 4. Implementation

##4.1 Contrail-dns
IPv4-only data structures, such as `boost::asio::ip::address_v4` should be changed to generic ones such as `boost::asio::ip::address` which can hold both IPv4 and IPv6 address. Moreover, utility functions to generate reverse zone names per arbitrary allocation prefix length need to be developed.

It also makes sense to support creating NS records in reverse zones, including `ip6.arpa`. This happens in the `DnsManager::SendRecordUpdate()` method.

##4.2 vRouter Agent
vRouter Agent should create IPv6 PTR records where dynamic updates are enabled and a VM has an IPv6 address: this happens in the `DnsProto::SendUpdateEntry()` (currently a TODO item, as mentioned above).

# 5. Performance and scaling impact
## 5.1 API and control plane
No impact

## 5.2 Forwarding performance
No impact

# 6. Upgrade
No database schema changes are needed. Dynamic records upgrade is transparent. If a user has static DNS records configured, it is up to them to add the corresponding PTR record if needed.

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
Existing unit tests should be expanded to cover reverse IPv6 zones names generation and alike. This is to be done by 01/30/2018.
## 9.2 Dev tests
## 9.3 System tests
Prove that OpenContrail can reverse-resolve IPv6-enabled VMs in the test lab. Due date is 02/15/2018.

# 10. Documentation Impact
None

# 11. References
None
