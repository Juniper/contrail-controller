# 1. Introduction
Contrail control-node can peer with set of PE nodes which may have a multihome
CE device(multihomed to mentioned set of PE). This set of PE are in all-active
multihoming mode. EVPN route originating because of this CE will be exported to
contrail with two paths(via set of PE).
Contrail should be able to load balance between the multiple links.

# 2. Problem statement
Though contrail-control supports this scenario but contrail-vrouter-agent does
not. It picks first nexthop from the nexthop list received in evpn route. This
results in skewed traffic flow.

#### Use case
Support multi-homed TOR.

# 3. Proposed solution
Honor all the nexthop sent for evpn route from control-node and compose ecmp out
of same.

## 3.1 Alternatives considered
NA

## 3.2 API schema changes
NA

## 3.3 User workflow impact
NA

## 3.4 UI changes
NA
## 3.5 Notification impact
There were no changes made in logs, UVEs or alarms.


# 4. Implementation
## 4.1 Work items
#### Config Schema
NA

#### Control-node
NA

#### Control-vrouter-agent
Agent will accept multiple nexthops appearing in xmpp messages.
As this is a feature implementing ECMP evpn routes from multihoming CE, no
locally generated (e.g. multiple VMI with same mac on compute) will be
considered for ECMP. In other words agent will not publish any ECMP evpn routes
and will only act on control-node published routes.

##### Proxy Arp
EVPN routes will provide the mac/IP binding for arp. This will remain same as of
regular arp entries programmed by EVPN.
There is an inherent assumption in vrouter(data plane) to not look at binding
when nexthop is ecmp composite. This will be modified to pick up the mac from
binding and not rely on type of nexthop.

##### Tunnel tracking
Flows will be used for tracking the tunnels as in L3 ECMP.
Every flow will carry one ecmp index computed using SIP, DIP, sport, dport and
protocol type. Reverse flow is also programmed at same so that source and
destination never change for a flow in forward/reverse direction.

Selection logic of ecmp composite NH is described here:
https://github.com/Juniper/contrail-controller/wiki/Flow-processing

#### Contrail-UI
NA

# 5. Performance and scaling impact
## 5.1 API and control plane
No impact

## 5.2 Forwarding performance
No impact

# 6. Upgrade
No impact

# 7. Deprecations
There are no deprecations when this change is made.

# 8. Dependencies
There are no dependencies for this feature.

# 9. Testing
## 9.1 Unit tests
Unit Test Cases will be added to validate added code

## 9.2 Dev tests
* Verify ECMP NH is formed for bridge/evpn routes when more than one nexthop is
seen.
* Verify flows and forwarding.
* Verify traffic is distributed across multiple NH.

## 9.3 System tests
* Verify traffic flow with multihomed CE (say two PE). Contrail will peer with
* both gateway. Traffic to CE should be distributed.

# 10. Documentation Impact
NA

# 11. References
NA
