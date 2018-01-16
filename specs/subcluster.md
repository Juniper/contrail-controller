# 1. Introduction
Subcluster to manage remote compute nodes away from the primary datacenter.

# 2. Problem statement
For various reasons datacenters could be distributed across the world. Some
of the common reasons are for disaster recovery, faster access etc...
Need to come up with a solution to manage these remote data centers called
subclusters, efficiently. 

# 3. Proposed solution
The proposed solution is at the remote sites, only control node services are
spawned, and ibgp meshed among themselves. These control nodes are provisioned
to BGP peer with SDN gateway, over which route exchange with primary control
nodes happen.

Compute nodes in the remote site are provisioned to connect to these control
nodes to receive config and exchange routes. Data communication among
workloads between these clusters happen through providers backbone through their
respective SDN gateways.

Both compute nodes are and control nodes are provisioned to push analytics data
to analytics nodes hosted on primary cluster. 

## 3.1 Alternatives considered
The existing alternate solution is cumbersome, where each cluster is an 
independent stand alone cluster and federated, but posing management challenges.

## 3.2 API schema changes
A new object subcluster is created which takes a list of links to local compute
nodes represented as vrouter objects and list of links to local control nodes
represented as bgp router objects, and an ASN as property. Each of this 
subcluster object represents a remote site. 

## 3.3 User workflow impact
None

## 3.4 UI changes
Option 1:
UI needs to show list of subcluster objects.
When a subcluster is selected, it should show list of associated vrouters and 
bgp routers local in that remote site and the ASN property.

Option 2:
UI needs to show list of subcluster objects.
While listing vrouters and bgp routers, just like project, a subcluster dropbox
list may be provided, which filters these objects based on subcluster object.

In phase 1, 
UI would implement new column against list of vrouters and bgp-routers
## 3.5 Notification impact
None

# 4. Implementation
## New Schema
```
+<xsd:element name="sub-cluster" type="ifmap:IdentityType"/>
+<xsd:element name="sub-cluster-asn" type="AutonomousSystemType"/>
+<!--#IFMAP-SEMANTICS-IDL
+     Property('sub-cluster-asn', 'sub-cluster' , 'required', 'CRUD',
+              'AS number of that cluster.') -->
+
+<xsd:element name="virtual-router-sub-cluster"/>
+<!--#IFMAP-SEMANTICS-IDL
+    Link('virtual-router-sub-cluster',
+         'virtual-router', 'sub-cluster', ['ref'], 'optional', 'CRUD',
+         'Reference to sub-cluster in virtual-router.') -->
+
+<xsd:element name="bgp-router-sub-cluster"/>
+<!--#IFMAP-SEMANTICS-IDL
+    Link('bgp-router-sub-cluster',
+         'bgp-router', 'sub-cluster', ['ref'], 'optional', 'CRUD',
+         'Reference to sub-cluster in bgp-router') -->
+
```
## Phase 1:
As part of phase 1, 

1) The above schema implementation would be done.
2) Provision script to provision subcluster.
3) vrouter and bgp-router provision scripts would be modified to take
subcluser as optional argument to link/delink them with subcluster object. 
4) New API would be implemented in api-lib to CRUD subcluster, and they would
be used by new provisioning scripts.
5) API server changes to handle the above requests and CRUD subcluster object
with the links and properties defined above.

## 4.1 Remote-site cross connect verification

#4.1.1 Cross sub-cluster site verification between compute and control nodes.

To catch any cross site connections between compute-node and control-node,
control-node will verify against the xmlns attribute in XMPP open message to
accept or reject connections.

<?xml version='1.0'?>
      <stream:stream
          from='juliet@im.example.com'
          to='im.example.com'
          version='1.0'
          xml:lang='en'
          xmlns='sub-cluster-id'
          xmlns:stream='http://etherx.jabber.org/streams'>
      </stream:stream>
</>

Both control-node and compute-node will be configured with sub-cluster string
parameter in their respective configuration files.

#4.1.2 Cross sub-cluster site verification between BGP peers.

As bgp peers are statically configured, we can verify this while
processing configuration.
Sub cluster control nodes are created as external-control-nodes

# 5. Performance and scaling impact
## 5.1 API and control plane

## 5.2 Forwarding performance
Not affected

# 6. Upgrade
Upgrade and primary and subcluster cannot be done independently with the current
ISSU sematics.
The recommended approach would be:
1) Spawn new version controller software with respective services in parallel
to existing services in both the clusters.
2) Point all the compute node analytics connectivity to the new version software
in the primary cluster.
3) Create a iBGP mesh among old and newer version control nodes with in the
cluster.
4) Upgrade compute nodes in the primary cluster and subclusters as how it is
done today and point them to the respective local control nodes in that cluster.
5) Once all the compute nodes are upgraded, follow the existing procedure of
ISSU on the primary cluster.
6) Follow similar procedure for the rollback.
However if the direct communication between old and newversion of applications
is relaxed, both cluster could be upgraded independently.

# 7. Deprecations
None

# 8. Dependencies
Remote cluster installation and lifetime management.

# 9. Testing
## 9.1 Unit tests
Opserver systemless tests will be added.
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact
None

# 11. Notes

1) A subcluster control nodes never directly peer with another subcluster control
nodes or primary control nodes.

2) A subcluster control node has to be created and is referred in virtual-router
 and bgp-router objects.

3) A subcluster object and the control nodes under it should have the same ASN.

4) ASN canot be modified in a subcluster object.

# 12. References
https://app.asana.com/0/335523438776724/428830908079139

