# 1. [Introduction](https://github.com/Juniper/contrail-controller/blob/master/specs/graceful_restart.md)

In Release 3.2, limited support to Graceful Restart (GR) and Long Lived
Graceful Restart (LLGR) helper modes to contrail-controller was provided.
This document describes the complete GR/LLGR feature planned in contrail
software in R4.0 and following releases.

# 2. Problem statement
In a contrail cluster, when ever contrail-control or contrail-vrouter-agent
module(s) restarts, network traffic flows can get affected based on the actual
failure and deployment scenario.

1. In the usual case, where in multiple contrail-control nodes are deployed
   for HA and redundancy, one contrail-control going down does not affect the
   flowing traffic. However, if all contrail-control modules go down, traffic
   can be severely affected until at least one of the contrail-control comes
   back and remains operational.

   In order to address this scenario, a feature called
   [Contrail-Vrouter Head-Less mode](http://www.juniper.net/techpubs/en_US/contrail2.21/topics/concept/using-headless-vrouter-vnc.html)
   mode was introduced. This though alleviates the problem to some extent, it
   does not render entire cluster fully operational. e.g. North-South traffic
   between vrouters and SDN gateway will remain down until one of the
   contrail-control becomes operational.

2. contrail-vrouter-agent restarts bring in a different set of issues.
   In releases past R4.0, when agent restarts, it always resets the vrouter
   module in the kernel during start. This affects all existing flows traffic
   until the flows are re-programmed. Also, when agent restarts, it allocates
   new set of labels and interface indices. This causes a churn in
   control-plane as well and affects flows in ingress vrouter nodes as well.

3. During upgrade scenarios such as ISSU, not only is the agent restarted, but
   the kernel module vrouter is also removed and v2 version of it is inserted.
   This can cause further or longer interruption to traffic flows.

This feature aims to minimize traffic loss and keep normalcy in a contrail
cluster in each of the scenarios described above.

# 3. Proposed solution
There are two key pieces in GR.

1. When a contrail-module (gracefully) restarts, then we should be able to
    avail GR helper functionality provided by its peers.

2. When a peer (bgp and/or xmpp) restarts, provide GR helper mode in order to
   minimize impact to the network. This is achieved using the standard mark and
   sweep approach to manage the learned (stale) information from the restarting
   peer.

[Contrail-Vrouter Head-Less mode](http://www.juniper.net/techpubs/en_US/contrail2.21/topics/concept/using-headless-vrouter-vnc.html) was introduced as
a resilient mode of operation for Agent. When running in Headless mode, agent
will retain the last "Route Paths" from Contrail-Controller. The "Route Paths"
are held till a new stable connection is established to one of the
Contrail-Controller. Once the XMPP connection is up and is stable for a
pre-defined duration, the "Route Path" from old XMPP connection are flushed.

When Headless mode is used along with graceful-restart helper mode in
contrail-control, vrouter can forward east-west traffic between vrouters for
current and new flows (for already learned routes) even if all control-nodes go
down and remain down in the cluster. If graceful restart helper mode is also
used in SDN gateways (such as JUNOS-MX), north south traffic between MX and
Vrouters can also remain uninterrupted in headless mode. This particular aspect
is not available in releases < 3.2.

## 3.1 Alternatives considered
As mentioned above, vrouter-agent headless mode solves part of one of the
problems. But it is not a complete solution and does not cover all applicable
operational scenarios.

## 3.2 API schema changes
GR/LLGR configuration resides under global-system-config configuration section
***[Configuration parameters](https://github.com/Juniper/contrail-controller/blob/master/src/schema/vnc_cfg.xsd#L885)***

## 3.3 User workflow impact
In order to use this feature, graceful-restart and/or long-lived-graceful-restart can be enabled using Web UI or using
[provision_control](https://github.com/Juniper/contrail-controller/blob/8a9f9d5c5bab09f276ae558f4aeafc575d5f12af/src/config/utils/provision_control.py#L177)
script. e.g.

```
/opt/contrail/utils/provision_control.py --api_server_ip 10.84.13.20 --api_server_port 8082 --router_asn 64512 --admin_user admin --admin_password c0ntrail123 --admin_tenant_name admin --set_graceful_restart_parameters --graceful_restart_time 300 --long_lived_graceful_restart_time 60000 --end_of_rib_timeout 30 --graceful_restart_enable --graceful_restart_bgp_helper_enable --graceful_restart_xmpp_helper_enable
```

When BGP Peering with JUNOS, JUNOS must also be explicitly configured for
gr/llgr. e.g.

```
set routing-options graceful-restart
set protocols bgp group a6s20 type internal
set protocols bgp group a6s20 local-address 10.87.140.181
set protocols bgp group a6s20 keep all
set protocols bgp group a6s20 family inet-vpn unicast graceful-restart long-lived restarter stale-time 20
set protocols bgp group a6s20 family route-target graceful-restart long-lived restarter stale-time 20
set protocols bgp group a6s20 graceful-restart restart-time 600
set protocols bgp group a6s20 neighbor 10.84.13.20 peer-as 64512

```

GR helper modes can be enabled via schema. They can be disabled selectively in
a contrail-control for BGP and/or XMPP sessions by configuring gr_helper_disable
in /etc/contrail/contrail-control.conf configuration file. For BGP, restart time
shall be advertised in GR capability, as configured (in schema). e.g.

```
/usr/bin/openstack-config /etc/contrail/contrail-control.conf DEFAULT gr_helper_bgp_disable 1
/usr/bin/openstack-config /etc/contrail/contrail-control.conf DEFAULT gr_helper_xmpp_disable 1
service contrail-control restart
```

When ever GR/LLGR configuration is enabled/disabled all BGP and/or XMPP agent
peering sessions are flipped. This can cause a brief disruption to the traffic
flows.

## 3.4 UI changes
Contrail Web UI can be used to enable/disable GR and/or LLGR configuration.
Various timer values as well as GR helper knobs can be tweaked under
[BGP Options tab](images/GracefulRestartConfigurationSnapShot.png) in
configuration section.

## 3.5 Notification impact
#### Describe any log, UVE, alarm changes
* contrail-control GR information [PeerCloseRouteInfo](https://github.com/Juniper/contrail-controller/blob/master/src/bgp/bgp_peer.sandesh#L49) and [PeerCloseInfo](https://github.com/Juniper/contrail-controller/blob/master/src/bgp/bgp_peer.sandesh#L57) are sent as part of control-node UVEs.

# 4. Implementation

## 4.1 contrail-controller Work items
Most of the contrail-control changes were done in R3.2 tracked by [bug 1537933](https://bugs.launchpad.net/juniperopenstack/+bug/1537933)

### 4.1.1 GR Helper Mode

When ever a bgp peer (or contrail-vrouter-agent) session down is detected, all
routes learned from the peer are deleted and also withdrawn immediately from
advertised peers. This causes instantaneous disruption to traffic flowing
end-to-end even when routes are kept inside vrouter kernel module (in data
plane) intact. GracefulRestart and LongLivedGracefulRestart features help to
alleviate this problem.

When sessions goes down, learned routes are not deleted and also not withdrawn
from advertised peers for certain period. Instead, they are kept as is and just
marked as 'stale'. Thus, if sessions come back up and routes are relearned, the
overall impact to the network is significantly contained.

### 4.1.2 End-of-Config and End-of-Rib Marker

GR process can be terminated sooner than later when End-of-Rib marker
is received by the helper. This helps in reducing traffic black-holing as stale
information is purged quickly (as opposed to do so based on a timer, though this
timer value can be tuned via configuration)

1. contrail-control should send End-of-Config marker when all configuration has
   been sent to the agent (over XmppChannel)
2. agent should send End-of-Rib marker when End-Of-Config marker is received,
   all received configuration has been processed and all originated routes has
   been advertised to contrail-control
3. contrail-control should send End-of-Route marker to agent after all routes
   an agent is interested is in has been advertised to it

When logic to send/receive a particular maker is not implemented, a timer can
be used to deduce the same. This timer should be configurable in order to tune
based on deployment scenarios.

### 4.1.3 Feature highlights
* Support to advertise GR and LLGR capabilities in BGP (By configuring non-zero
  restart time)
* Support for GR and LLGR helper mode to retain routes even after sessions go
  down (By configuring helper mode)
* With GR is in effect, when ever a session down event is detected and close
  process is triggered, all routes (across all address families) are marked
  stale and remain eligible for best-path election for GracefulRestartTime
  duration (as exchanged)
* With LLGR is in effect, stale routes can be retained for much longer time
  than however long allowed by GR alone. In this phase, route preference is
  brought down and best paths are recomputed. Also LLGR_STALE community is
  tagged for stale paths and re-advertised. However, if NO_LLGR community is
  associated with any received stale route, then such routes are not kept and
  deleted instead
* After a certain time, if session comes back up, any remaining stale routes
  are deleted. If the session does not come back up, all retained stale routes
  are permanently deleted and withdrawn from advertised peers
* GR/LLGR feature can be enabled for both BGP based and XMPP based peers
* GR/LLGR configuration resides under global-system-config configuration section

## 4.2 contrail-vrouter-agent Work items

# 5. Performance and scaling impact
## 5.1 API and control plane
No specific performance implication is expected on control plane scaling due to
GR/LLGR feature. Memory usage can remain high when the helper mode is in effect
as the routes learned by the peers are kept even when the session gets closed
(until the timer expires or the session comes back up and sends end-of-rib)

## 5.2 Forwarding performance
#### Scaling and performance for API and forwarding

# 6. Upgrade
#### Describe upgrade impact of the feature
* control-plane upgrade (ISSU) is not impacted when GR is enabled because during
ISSU, v2 contrail-control forms a bgp peering with v1 contrail-control during
the time of the upgrade. Once upgrade is complete, this peering is de-configured
and hence any GR possibly in effect in v1 contrail-control or in v2
contrail-control is destroyed.

* agent-upgrade does impact this feature. During ISSU, agents may flip peering
from v1 control-node to v2 control-node or v1 agent remains connected to
v1 control-node and v2 agent remains connected to v2 control-node (TBD). In
any case, session must be closed non-graceful when switching over from v1
to v2, or vice-versa during roll-back because of downgrade.

#### Schema migration/transition
N/A

# 7. Deprecations
N/A

# 8. Dependencies
#### Describe dependent features or components.

# 9. Testing
## 9.1 Unit tests
* [Unit Test](https://github.com/Juniper/contrail-controller/blob/master/src/bgp/test/graceful_restart_test.cc)

## 9.2 Dev tests
## 9.3 System tests

* [SystemTest plan](https://github.com/Juniper/contrail-test/wiki/Graceful-Restart)

# 10. Documentation Impact

# 11. References
* GracefulRestart for BGP (and XMPP) follows [RFC4724](https://tools.ietf.org/html/rfc4724) specifications
* LongLivedGracefulRestart feature follows [draft-uttaro-idr-bgp-persistence](https://tools.ietf.org/html/draft-uttaro-idr-bgp-persistence-03) specifications
* [Feature BluePrint](https://blueprints.launchpad.net/juniperopenstack/+spec/contrail-control-graceful-restart)
* [SystemTest plan](https://github.com/Juniper/contrail-test/wiki/Graceful-Restart)
* [Unit Test](https://github.com/Juniper/contrail-controller/blob/master/src/bgp/test/graceful_restart_test.cc#L1180)

#12. Caveats
* GR/LLGR feature with a peer comes into effect either to all negotiated
  address-families or to none. i.e, if a peer signals support to GR/LLGR only
  for a subset of negotiated address families (Via bgp GR/LLGR capability
  advertisement), then GR helper mode does not come into effect for any family
  among the set of negotiated address families
* GR/LLGR is not supported for multicast routes
* GR/LLGR helper mode may not work correctly for EVPN routes, if the restarting
  node does not preserve forwarding state
