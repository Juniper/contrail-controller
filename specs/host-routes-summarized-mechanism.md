
#1. Introduction
Default treatment of Contrail advertised VM host routes will be to summarize
them at the WAN boundary. We require the ability to announce host routes to
specific VMs, which can be achieved by indicating it with the help of a BGP
standard community value. This CV will be used within a route policy, that will
allow paths with CV to propagate as-is where as the paths without CV will rely
on an aggregate prefix.

#2. Problem statement
Contrail needs to allow propagation of BGP standard communities for specific
IP addresses, which needs to be used as an indication to propagate route path
as-is.

#3. Proposed solution
Allow user to configure BGP standard communities for routes by attaching it to
specific IP address, which will be stamped to the route exported for this IP
address indicating additional information to external routers

##3.1 Alternatives considered
None

##3.2 API schema changes
CommunityAttributes will be added as an element under IP address (instance-ip,
floating-ip etc.), to have list of BGP standard communities associated to
IP address.

##3.3 User workflow impact
User can use VNC APIs or Heat templates to configure list of BGP standard
communities to instance ip, floating ip.

##3.4 UI changes
TBD, as of now this will be supported with VNC APIs and Heat templates

##3.5 Notification impact
There were no changes made in logs, UVEs or alarms.

#4. Implementation
##4.1 Work items
####Config Schema
As per section 3.2

####Control-node
None, control-node already handles encoding and decoding of BGP standard
communities.

####Contrail-vrouter-agent
Read the list of community values attached to the instance ip or floating ip
and attach it while exporting route corresponding to it.

#5. Performance and scaling impact
##5.1 API and control plane
No impact

##5.2 Forwarding performance
No impact

#6. Upgrade
No impact

#7. Deprecations
None

#8. Dependencies
None

#9. Testing
##9.1 Unit tests
Unit Test Cases will be added to validate added code

##9.2 Dev tests
Validate user added community propagation with the route exported from
contrail-vrouter-agent

##9.3 System tests
Feature test with a Gateway router

#10. Documentation Impact
User documentation needs to cover support for BGP standard communities with
specific IP address

#11. References
[bgp-design](http://juniper.github.io/contrail-vnc/bgp_design.html)

[adding-bgp-knob-to-opencontrail](http://www.opencontrail.org/adding-bgp-knob-to-opencontrail/)
