
# 1. Introduction
To provide health check support for transparent service chains.
This document describes the design and implementation details of Contrail
components to achieve this.

# 2. Problem statement
Support segment-based health-check for transparent service chains

# 3. Proposed solution

## 3.1 Summary of segment-based health check solution
Segment-based health-check is used to verify health of single instance in
transparent service chain. User is expected to create service-health-check
object with type as segment-based and attach it to either left or right
interface of service-instance whose health has to be verfied. If
service-health-check object is attached to left interface of service-instance,
then health-check packet will be injected to left interface. When packet comes
out of right interface a reply packet is injected on right interface. Both
request and replies are injected by vrouter-agent. If vrouter-agent does not
receive replies within the configured timeout, we consider the health-check
request/reply packet has failed to travel. If health-check requests fail
continuously for user configured retries, then service-instance is considered
unhealthy and the service vlan routes of left and right interface are removed.
Similar behavior applies when service-health-check object is attached on right
interface except that health-check packet is transmitted from right to left
interface.

## 3.2 Limitation
1. Only ICMP/ping packet will be supported as health-check packet.
2. Segment-based health-check will verify only health of service-instance and
   NOT health of forwarding path.

# 4. Implementation

### 4.1.1 Schema
Schema has service-health-check object which has a property of type
service-health-check-properties. The service-health-check-properties will be
modified to support a new health-check-type for segment-based health-check.
The timeout, delay and retries are already configurable under existing
service-health-check-properties. Since we support only ping, properties
applicable to other protocols are not supported for segment-based health-check.

### 4.1.2 Control Node
When contrail-vrouter-agent detects failure of service-instance and removes
service-vlan routes from the interface attached to service-health-check,
control-node removes service-vlan routes of other interface of service-instance.
For example if service-health-check is attached to left interface of
service-instance and contrail-vrouter-agent detects failure of that instance,
contrail-vrouter-agent will remove service-vlan routes of left-interface. Using
this as trigger control-node will remove service-vlan routes of right interface.

The other change in control-node is to white-list port-tuple configuration so
that agent can receive it to detect the other end of service-instance.
Vrouter-agent gets to know one end of service-instance via the attachment point
of service-health-check object and other end via port-tuple configuration.

### 4.1.3 Vrouter Agent
Contrail-vrouter-agent will send health-check packet when segment-based
service-health-check object is attached to left or right interface of
service-instance. It is responsible for verifying whether the packet it sent
reaches back to it or not. Agent is also responsible for replying to
health-check request packets with replies in the reverse direction.
Agent will build health-check packet (ICMP packet) with source IP as service-ip
of the interface's (to which service-health-check object is attached) VN and
destination IP as service-ip of the VN of other interface of service-instance.
To figure out the destination IP agent has to figure out the other end (the
interface to which service-health-check object is NOT attached). For this,
vrouter-agent will start parsing port-tuple configuration.

For example lets assume that health-check packet is attached to left interface
of service-instance. Agent injects ping packet (ICMP echo request packet with
source IP as service-IP of left interface's VN and destination IP as service-IP
of right interface's VN) to the left interface. Agent expect the ping packet to
reach it back via right interface. When agent receives echo request packet, it
will reply with echo reply packet on right interface.
The request and replies are co-related using identifier/sequence field of ICMP
header. Vrouter-agent sends 'max-retries' number of health-check packets with an
interval of 'delay' and waits for 'timeout' period for each reply to arrive. If
no replies arrive for any of 'max-retries' number of requests, then
vrouter-agent declares the service-instance as dead and retracts service-vlan
routes added for left-interface. This in turn will trigger the control-node to
remove service-vlan routes added to the right interface.

Since packets are sent using native IPs of left and right interfaces and route
retraction is done only for service vlans, when a dead service-instance comes
back, vrouter-agent will be able to detect the aliveness of service-instance
and add routes of service-vlan back. This will trigger control-node to add
service-vlan routes for other end of service-instance.

### 4.1.4 Vrouter
Assuming packets were injected by vrouter-agent on left interface and packets
come out of right interface, vrouter should disable creation of flows for these
packets even when policy bit is enabled on right interface. Vrouter will trap
these packets to vrouter-agent which will be used by vrouter-agent to
co-relate packets it has sent/received and decide the fate of
service-vlan routes.
