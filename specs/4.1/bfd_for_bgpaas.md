# 1. Introduction
Provide Bidirectional Forwarding and Detection (BFD) protocol based health check
for BGP as a Service (BGPaaS) sessions

# 2. Problem statement
BFD based health check([6]) feature addresses providing BFD based health check
mechanism for any tenant addresses over a particular Virtual Machine Interface
(VMI). However, this cannot be directly used for BGPaaS session, as BGPaaS
session shares same tenant destination address over a set of VMIs with only one
being active at any given time.

In this feature, [6] is complemented with support to run BFD based health checks
for BGPaaS sessions as well.

# 3. Proposed solution
BGPaaS configuration can be enabled on a set of virtual-machine-interfaces,
although at any given time, only one of the VMIs will carry the active BGPaaS
session packets. As part of configuration of BGPasS object, one can also
configure ServiceHealthCheckType property for bgp-as-a-service configuration
object.

## 3.1 Alternatives considered
None

## 3.2 API schema changes
```
diff --git a/src/schema/vnc_cfg.xsd b/src/schema/vnc_cfg.xsd
index 8ea168f..1722673 100644
--- a/src/schema/vnc_cfg.xsd
+++ b/src/schema/vnc_cfg.xsd
@@ -2200,6 +2200,17 @@ targetNamespace="http://www.contrailsystems.com/2012/VNC-CONFIG/0">
      Property('bgpaas-suppress-route-advertisement', 'bgp-as-a-service', 'optional', 'CRUD',
               'True when server should not advertise any routes to the client i.e. the client has static routes (typically a default) configured.') -->

+<xsd:element name="bgpaas-health-check" type='ServiceHealthCheckType'/>
+    <xsd:annotation>
+        <xsd:documentation>
+            This is used to enable periodic health-check operation over the
+            active BGPaaS vmi.
+        </xsd:documentation>
+    </xsd:annotation>
+<!--#IFMAP-SEMANTICS-IDL
+     Property('bgpaas-health-check', 'bgp-as-a-service', 'optional', 'CRUD',
+              'Health-Check attributes') -->
+
 <xsd:element name="virtual-router" type="ifmap:IdentityType"/>
 <xsd:element name="global-system-config-virtual-router"/>
 <!--#IFMAP-SEMANTICS-IDL
```

## 3.3 User workflow impact
Users would first configure a service-health-check type object and then associate
this object with BGPaaS object as desired.

## 3.4 UI changes
UI shall provide a way to configure health-check for BGPaaS sessions. Similar
to how health checks can be enabled for service interfaces, user should be able
to configure BFD based health-check service for BGPaaS sessions. More details
on this shall be provided once schema changes are reviewed and approved.

## 3.5 Notification impact
When ever BFD-for-BGP session is marked down by health-checker, appropriate logs
and alarms shall be generated. More details on the exact format of this is
still TBD.

# 4. Implementation
When ever agent marks BGPaaS as active over a VMI (by enabling the NAT-ed flows
towards control-node), if health-check is configured, it shall enable health
check as well, over that interface for the BGPaaS session destination address.
This includes ping, http or BFD based health checks, as configured. However for
BGPaaS, only BFD based health-check is supported.

If health check is enabled for BGPaaS, agent shall configure a new BFD session
over the active VMI to the BGPasS destination address. Local address used shall
correspond to the local address of the NAT-ed BGP flow (e.g. 192.168.0.1 or
192.168.0.2 as applicable). During switch-over from one interface to another,
agent shall also delete the previously configured BFD session.

For BFD based health checks, agent shall use <local-ip, remote-ip, VMI-index>
as the BFD session key during configuration. In this case, VMI index is the
interface index of the VMI over which BGPaaS is currently active. If BFD
packets are received over any other interface, BFD server is expected to drop
those packets.

# 5. Performance and scaling impact
The impact is considered analogous to general health check based services.

## 5.2 Forwarding performance
Forwarding performance can get affected as CPUs are shared for both ends of the
BFD session as well as for all other data traffic forwarding.

# 6. Upgrade
HealthCheck infrastructure (including BFD) is treated as integral part of the
agent. Hence all aspects of agent upgrade are equally applicable to health check
for BGPaaS sessions

# 7. Deprecations
None

# 8. Dependencies
None

# 9. Testing
## 9.1 Unit tests
BFD based health check mechanism has goals to add sufficient unit tests.
Specific unit tests to cover BGPaaS must also be covered as applicable.

## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References

1. BFD RFC [RFC5880](https://tools.ietf.org/html/rfc5880)
2. Seamless BFD [RFC7880](https://tools.ietf.org/html/rfc7880)
3. BFD [source](https://github.com/Juniper/contrail-controller/tree/master/src/bfd)
4. Health-Check in [Agent](https://github.com/Juniper/contrail-controller/blob/master/src/vnsw/agent/oper/health_check.cc)
5. Feature [BluePrint](https://blueprints.launchpad.net/juniperopenstack/+spec/bfd-for-bgpaas)
6. [BFD based health check](https://blueprints.launchpad.net/juniperopenstack/+spec/bfd-over-vmis)
