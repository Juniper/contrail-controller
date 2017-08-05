
# 1. Introduction
To provide health check support for transparent service chains.
This document describes the design and implementation details of Contrail
components to achieve this.

# 2. Problem statement
Support segment-based health-check for transparent service chains

# 3. Proposed solution
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

## 3.1 Alternatives considered
None

## 3.2 API schema changes
Schema has service-health-check object which has a property of type
service-health-check-properties. The service-health-check-properties will be
modified to support a new health-check-type for segment-based health-check.
The timeout, delay and retries are already configurable under existing
service-health-check-properties. Since we support only ping, properties
applicable to other protocols are not supported for segment-based health-check.

## 3.3 User workflow impact
User is expected to create service-health-check object with health-check-type
as segment-based and attach it to either left or right interface of
service-instance.

## 3.4 UI changes
In Configure->Services->Health Check, when we Create a new Health Check service,
a window for Health Check creation opens. In this window a new option by name
segment-based should be provided in the drop-down list for Health Check Type.
Also when segment-based, health check type is selected the following
restrictions should be enforced
(a) Protocol apart from Ping should not be configurable.
(b) Monitor target should be greyed out(not configurable) for segment-based
    health check type

## 3.5 Notification impact
Interface UVE will carry details of list of service-health-check objects
associated with it. For each service-health-check object, the following
information is sent

struct VmHealthCheckInstance {
    /** Name of Health check service */
    1: string name;
    /** UUID of Health check service */
    2: string uuid;
    /** Contains 'true' if the health check service is currently running and
     * 'false' otherwise
     */
    3: bool is_running;
    /** Contains "Active" if the health check service is active and "InActive"
     * otherwise
     */
    4: string status;
}

This information will be sent every 30 seconds. If there is no change in
the above information since previous send, then it won't be sent.

# 4. Implementation

## 4.1 Work items

### 4.1.1 Control Node changes
When contrail-vrouter-agent detects failure of service-instance, it removes
service-vlan routes from both left and right interfaces of service instance.
Control-node uses this information to stop re-originating (and to remove already
re-originated routes) to other service-instances of that service chain. This is
required to prevent traffic black-holing

The other change in control-node is to white-list port-tuple configuration so
that agent can receive it to detect the other end of service-instance.
Vrouter-agent gets to know one end of service-instance via the attachment point
of service-health-check object and other end via port-tuple configuration.

### 4.1.2 Vrouter Agent changes
Contrail-vrouter-agent will send health-check packet when segment-based
service-health-check object is attached to left or right interface of
service-instance. It is responsible for verifying whether the packet it sent
reaches back to it or not. Agent is also responsible for replying to
health-check request packets with replies in the reverse direction.
Agent will build health-check packet (ICMP packet with some data in ICMP
payload) with source IP as service-ip of the interface's (to which
service-health-check object is attached) VN and destination IP as service-ip of
the VN of other interface of service-instance. To figure out the destination IP
agent has to figure out the other end (the interface to which
service-health-check object is NOT attached). For this, vrouter-agent will start
parsing port-tuple configuration.

For example lets assume that health-check packet is attached to left interface
of service-instance. Agent injects ping packet (ICMP echo request packet with
source IP as service-IP of left interface's VN and destination IP as service-IP
of right interface's VN) to the left interface. Agent expect the ping packet to
reach it back via right interface. When agent receives echo request packet, it
will reply with echo reply packet on right interface. The request and replies
are co-related using payload of ICMP packet. Vrouter-agent sends 'max-retries'
number of health-check packets with an interval of 'delay' and waits for
'timeout' period for each reply to arrive. If no replies arrive for any of
'max-retries' number of requests, then vrouter-agent declares the
service-instance as dead and retracts service-vlan routes added for both left
and right interfaces. This in turn will trigger the control-node to retract
re-originated routes from other service-instances in that service chain.

Even when agent has retracted routes from both left and right interfaces, it
keeps sending health-check packets periodically. When agent receives
health-check replies successfully, it adds the retracted routes back on both
interfaces which triggers control-node to start re-originating routes to other
service-instances on that service chain.

### 4.1.3 Vrouter changes
Assuming packets were injected by vrouter-agent on left interface and packets
come out of right interface, vrouter should disable creation of flows for these
packets even when policy bit is enabled on right interface. Vrouter will trap
these packets to vrouter-agent which will be used by vrouter-agent to
co-relate packets it has sent/received and decide the fate of
service-vlan routes.

## 4.2 Limitations
1. Only ICMP/ping packet will be supported as health-check packet.
2. Segment-based health-check will verify only health of service-instance and
   NOT health of forwarding path.

# 5. Performance and scaling impact
## 5.1 API and control plane
#### Scaling and performance for API and control plane

## 5.2 Forwarding performance
#### Scaling and performance for API and forwarding

# 6. Upgrade
None

# 7. Deprecations
None

# 8. Dependencies
This is dependent on transparent service chaining feature. This features checks
health of transparent service instance. If the transparent service instance is
dead, this feature will retract service-vlan routes of left and right interfaces
of transparent service-instance

# 9. Testing
## 9.1 Unit tests
    (a) Create service-health-check object and verify it is populated in
        agent's oper tables
    (b) Create transparent SI, Create service-health-check object and attach it
        to left interface of SI and verify interface table of agent to have
        correct values for left interface of SI
    (c) Repeat the test for right interface of SI
    (d) Verify that agent sends health-check packet on attachment to left VMI
    (e) Verify that agent sends health-check packet on attachment to right VMI
    (f) Verify that agent is able to parse health-check request packet and
        increment appropriate stats
    (g) Verify that agent is able to parse health-check reply packet and
        increment appropriate stats

## 9.2 Dev tests
    (a) Create transparent SI, Create service-health-check object and attach it
        to left interface of SI. Disable bridging of packets from left interface
        of SI to right. Verify that service-vlan routes of left and right
        interfaces are removed.
    (b) Repeat the test-case (a) for right interface and verify route
        retraction for both interfaces.

## 9.3 System tests
    (a) Create multiple transparent service-instances in a chain. Create
        multiple service-health-check objects and associate one object to each
        of the left interfaces of service-instances. Disable bridging on all
        SIs and verify service-vlan route retraction happens on all SIs.
    (b) Repeat the above test-case for right interface of SI
    (c) Using service scaling, create multiple service-instances. Create
        service-health-check object and associate it to one of the left
        interface of service-instances. When bridging is disabled on one SI,
        verify service-vlan routes only for that SI are retracted. Also verify
        that other paths of SI are still functional and no route retraction has
        happened for them.
    (d) Repeat the above test-case for right interface of SI


# 10. Documentation Impact

# 11. References
