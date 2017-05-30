# 1. Introduction
Contrail BGP implementation uses the following extended communities
  - Route Target
  - Encapsulation
  - Security Group
  - Origin VN
  - MAC Mobility
  - Load Balance
Allowing controller to be federated, by seamlessly stretching virtual networks,
policies and security groups etc across multiple remote data center locations
However these extended communities are always enabled to propagate and doesn't
have provision for administrator to disable.

# 2. Problem statement
while the solution works great for most of the cases, it works on the basis of
an assumptions, using non overrlapping AS or vR addresses.
Which is not the case always, thus advertisement of such CVs to CBB RRs can
create spurious extranets (route leaking between customer VRFs) or can cause
traffic hit or black hole.

# 3. Proposed solution
Provide a configuration knobs for administrator to disable propagation of
extended communities selectively for a peer.
Where if administrator opts to disable security group extended community for a
peer, Contrail controller will skip sending security group extended community
from all the routes propagated to this peer.
However for the case of route target it will allow only disabling of internally
generated route targets that falls in range of AS:8000000 and onwards, where
user configured route targets under virtual network will still be propagated to
this peer.

## 3.1 Alternatives considered
Following alternatives were considered
 - Filter these extended CVs on SDN GW
 - use non-overlapping vR addresses across AIC
 - use route aggregation on SDN GW

## 3.2 API schema changes
BgpRouterParams to have additional place holder to hold list of disabled or
filtered extended communities.

## 3.3 User workflow impact
User can explicitly disable/filter any of the supported extended community for
a BGP router/peer, while creating or updating a BGP router

## 3.4 UI changes
To the BGP router wizard add a new multiselect view to take input for list of
extended community filters.

## 3.5 Notification impact
There were no changes made in logs, UVEs or alarms.

# 4. Implementation
## 4.1 Work items
#### Config Schema
As per section 3.2

#### Control-node
Control node will handle filtering or the BGP extended communities based on the
associated configuration per peer.
A change of configuration will be handled by flaping the peer connection, upon
re-connect extended community propagation based on new configuration will take
effect

#### Contrail-UI
As per section 3.5

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
## 9.3 System tests
Validate extended community propagation between two contrail clusters

# 10. Documentation Impact
Details of new configuration option and its effects needs to be added to user
documentation

# 11. References

[bgp-design](http://juniper.github.io/contrail-vnc/bgp_design.html)

[adding-bgp-knob-to-opencontrail](http://www.opencontrail.org/adding-bgp-knob-to-opencontrail/)
