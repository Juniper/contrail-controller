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
BGPaaS configuration can be enabled on a set of virtual-machine-interfaces.
Although, at any given time, only one of the VMIs will carry the active BGPaaS
session packets. As part of configuration BGPasS object, one can also configure
ServiceHealthCheckType property for bgp-as-a-service configuration object.

## 3.1 Alternatives considered
BFD based health check can be configured via ServiceHealthCheckType property
for any service interface. This feature extends its applicability to BGPaaS
sessions. There is no other suitable alternative which provides this
functionality to BGPaaS sessions

## 3.2 API schema changes
```
diff --git a/src/schema/vnc_cfg.xsd b/src/schema/vnc_cfg.xsd
index 36f583f..e463c9a 100644
--- a/src/schema/vnc_cfg.xsd
+++ b/src/schema/vnc_cfg.xsd
@@ -2173,6 +2173,18 @@ targetNamespace="http://www.contrailsystems.com/2012/VNC-CONFIG/0">
      Property('bgpaas-suppress-route-advertisement', 'bgp-as-a-service', 'optional', 'CRUD',
               'True when server should not advertise any routes to the client i.e. the client has static routes (typically a default) configured.') -->

+<xsd:element name='bgpaas-health-check' type='ServiceHealthCheckType'/>
+    <xsd:annotation>
+        <xsd:documentation>
+            This is used to enable health check for BGPaaS session over the
+            virtual machine interface over which the session is active.
+        </xsd:documentation>
+    </xsd:annotation>
+<!--#IFMAP-SEMANTICS-IDL
+     Property('bgpaas-health-check', 'bgp-as-a-service', 'optional', 'CRUD',
+              'Health Check service for BGPaaS sessions') -->
+
 <xsd:element name="virtual-router" type="ifmap:IdentityType"/>
 <xsd:element name="global-system-config-virtual-router"/>
 <!--#IFMAP-SEMANTICS-IDL
```

## 3.3 User workflow impact
####Describe how users will use the feature.

## 3.4 UI changes
UI shall provide a way to configure health-check for BGPaaS sessions. Similar
to how health checks can be enabled for service interfaces, user should be able
to configure BFD based health-check service for BGPaaS sessions. More details
on this shall be provided once schema changes are reviewed and approved.

## 3.5 Notification impact
When ever VMI is marked down by health-checker, appropriate logs and alarms
shall be generated. More details on the exact format of this is still TBD.

# 4. Implementation
When ever agent marks BGPaaS as active over a VMI (by enabling the flows
towards control-node), if health-check is configured, it shall enable health
check as well, over that interface for the BGPaaS session destination address.
This includes ping, http or BFD based health checks, as configured.

If health-check fails at any time (including during initial setup time), the
VMI is marked down and all routes originated over that VMI are withdrawn from
the the control-node. This would cause all BGP routes advertised over the
BGPaaS session to control-node as unusable since that path (next-hop) becomes
unresolved.

In the meanwhile, BGPaaS is expected to switch over to some other VMI making
that as the active VMI. At that time, new flows are setup over that VMI and
new health-checks are again initiated over the new active VMI. Earlier health
checks are also deleted accordingly as the old vmi is no longer active.

For BFD based health checks, agent shall use <ip-address, VMI-index> as the
BFD session key during configuration. In this case, VMI index is the interface
index of the VMI over which BGPaaS is currently active. If BFD packets are
received over any other interface, BFD server is expected to drop those packets.
Note: Even though the BFD session itself will most likely be a multi-hop type,
session is still treated as a single-hop session by using ifindex as part of
the key. This is so because at any give time, BGPaaS is expected to be active
on only one of the VMIs. We do not expect to receive BFD packets over any other
non active VMI.

# 5. Performance and scaling impact
The impact is considered analogous to general health check based services.

##5.2 Forwarding performance
Forwarding performance can get affected as CPUs are shared for both ends of the
BFD session as well as for all other data traffic forwarding.

# 6. Upgrade
HealthCheck infrastructure (including BFD) is treated as integral part of the
agent. Hence all aspects of agent upgrade are equally applicable to health
check for BGPaaS sessions

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
