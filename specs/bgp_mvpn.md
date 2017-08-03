# 1. Introduction
Provide BGP NGEN Mvpn support in contrail software.

# 2. Problem statement
Currently, multicast is supported using ErmVpn (Edge replicated multicast). This
solution is limited to a single virtual-network. i.e., senders and receivers
cannot span across different virtual-networks. Also, this solution is not inter
operable (yet) with any of the other known bgp implementations.

# 3. Proposed solution
Use NGEN-Mvpn design to provide inter-vn multicast capabilities by still using
ErmVpn for packets replication underneath in the data plane.

## 3.1 Alternatives considered
Some investigations were made to extend ermvpn itself to support inter-vn
multicast. There is no clean way to do this and it is also not inter operable
as well, as mentioned before.

## 3.2 API schema changes

Mvpn can be enabled/disabled in bgp afis and in virtual-networks.

```
diff --git a/src/schema/bgp_schema.xsd b/src/schema/bgp_schema.xsd
--- a/src/schema/bgp_schema.xsd
+++ b/src/schema/bgp_schema.xsd
@@ -289,6 +289,8 @@
         <xsd:enumeration value="inet-vpn"/>
         <xsd:enumeration value="e-vpn"/>
         <xsd:enumeration value="erm-vpn"/>
+        <xsd:enumeration value="inet-mvpn"/>
         <xsd:enumeration value="route-target"/>
         <xsd:enumeration value="inet6"/>
         <xsd:enumeration value="inet6-vpn"/>
diff --git a/src/schema/vnc_cfg.xsd b/src/schema/vnc_cfg.xsd
index 2804d8b..732282b 100644
--- a/src/schema/vnc_cfg.xsd
+++ b/src/schema/vnc_cfg.xsd
@@ -1363,6 +1363,8 @@ targetNamespace="http://www.contrailsystems.com/2012/VNC-CONFIG/0">
          <!-- Enable or disable Mirroring for virtual-network -->
          <xsd:element name='mirror-destination' type="xsd:boolean" default="false" required='optional'
              description='Flag to mark the virtual network as mirror destination network'/>
+         <!-- Enable or disable ipv4-multicast for virtual-network -->
+         <xsd:element name='ipv4-multicast' type="xsd:boolean" default="false" required='optional' description='Flag to enable ipv4 multicast service'/>
     </xsd:all>
 </xsd:complexType>

```

## 3.3 User workflow impact
In order to use Mvpn feature, users shall enable mvpn in bgp and in the virtual
networks as desired. In a peering SDN gateway such as MX (JUNOS) also, users
should configure Mvpn. In order to support multicast sender outside the cluster,
one must also configure necessary S-PMSI tunnel information inside the routing
instance as applicable.

## 3.4 UI changes
UI shall provide a way to configure/enable Mvpn for bgp and virtual-networks.
Specifically, mvpn for ipv4 (and later for ipv6) would be one of the families to
enable under bgp address families configuration. There shall be a knob under
each virtual-network as well, in order to selectively enable/disable mvpn
functionality.

## 3.5 Notification impact
####Describe any log, UVE, alarm changes

# 4. Implementation

## 4.1 Capability negotiation
When mvpn AFI is configured, BGP shall exchange Capability with MCAST_VPN NLRI
for IPv4 multicast vn routes. AFI(1)/SAFI(5). This is not enabled by default.

## 4.2 MvpnManager

[bgp_mvpn.h](https://github.com/rombie/contrail-controller/blob/mvpn_spec/src/bgp/bgp_mvpn.h)

[bgp_mvpn.cc](https://github.com/rombie/contrail-controller/blob/mvpn_spec/src/bgp/bgp_mvpn.cc)

[mvpn_table.h](https://github.com/rombie/contrail-controller/blob/mvpn_spec/src/bgp/mvpn/mvpn_table.h)

[mvpn_table.cc](https://github.com/rombie/contrail-controller/blob/mvpn_spec/src/bgp/mvpn/mvpn_table.cc)

[mvpn_route.h](https://github.com/rombie/contrail-controller/blob/mvpn_spec/src/bgp/mvpn/mvpn_route.h)

[mvpn_route.cc](https://github.com/rombie/contrail-controller/blob/mvpn_spec/src/bgp/mvpn/mvpn_route.cc)

MvpnManager
    1. There shall be one instance per vrf.mvpn.0 table
    2. Maintains list of auto-discovered mvpn [bgp] neighbors
    3. Manages locally originated mvpn auto discovery routes such as
       Type-1 (AD), and Type-2 AD.
    4  Manages locally originated Type-4 Leaf AD routes (In response to received
       Type-3 S-PMSI Routes)
    3. Listens to all change to vrf.mvpn.0 table
    5. Handles initialization or cleanup when mvpn configuration is added or
       deleted in a virtual-network
    6. Provides data for inspection at run time via Introspect

MvpnProjectManager
    1. There shall be one instance per <project>.mvpn.0
    2. Maintains an std::map<MvpnState::SG, MvpnState *>. This map contains the
       current Mvpn state of a particular <S,G> multicast route. Among many
       things, it holds pointers to all Mvpn routes of the corresponding <S,G>
       such Leaf-AD Routes, applicable GlobalErmVpnRoute, etc.
    3. This MvpnState structure inside the map is ref-counted and stored inside
       the DB-State of the referred routes. This ensures that referred routes
       never get deleted until DB State goes away and vice-versa.
    4. Listens to all changes to <project>.ermvpn.0 and notifies all type-4 leaf
       ad routes applicable if GlobalErmvpnRoute changes (add/change/delete)

## 4.3 Concurrency Model

All mvpn and ermvpn routes for a particular group are always handled serially
under the same db task. This is achieved by using group as the only field to
hash and map a route to a particular DB table partition. This would simplify the
design and let MvpnManager, MvpnProjectManager, ErmVpnManager and
MvpnTable::Replicate() call directly into each other (using well defined APIs)
in a thread safe manner without the need to acquire a mutex.

Note: Care must be taken to ensure data consistency when ever Mvpn neighbor
information is accessed. This information is constructed off Type-1 and Type-2
routes which do not contain any S,G information. Hence they do not necessarily
all into the same partition as other routes which are specific to an <S,G>. This
is done by protecting neighbor map using a mutex. Callers are always provided a
copy of the MvpnNeighbor structure.

## 4.4 General Mvpn Routes Flow

```
Route-Type      CreateWhen         Primary   ReplicateWhen Secondary
================================================================================
Without Inclusive PMSI
T1 AD          Configure Mvpn      vrf.mvpn  RightAway   bgp.mvpn, vrf[s].mvpn
T1 AD          Receive via bgp     bgp.mvpn  RightAway   vrf[s].mvpn
                                                        Send only to I-BGP Peers

Without Inclusive PMSI
T2 AD          Configure Mvpn      vrf.mvpn  RightAway   bgp.mvpn, vrf[s].mvpn
T2 AD          Receive via bgp     bgp.mvpn  RightAway   vrf[s].mvpn
                                                        Send only to E-BGP Peers

Source-Active AD
T5 S-Active AD Receive via xmpp    vrf.mvpn  RightAway   bgp.mvpn, vrf[s].mvpn
T5 S-Active AD Receive via bgp     bgp.mvpn  RightAway   vrf[s].mvpn

SharedTreeJoin
T6 C-<*, G>    Receive via xmpp    vrf.mvpn  Src-Active  bgp.mvpn, vrf[s].mvpn
               PIM-Register                  is present
T6 C-<*, G>    Receive via bgp     bgp.mvpn  RightAway   vrf[s].mvpn

SourceTreeJoin
T7 C-<S, G>    Receive via xmpp    vrf.mvpn  Source is   bgp.mvpn, vrf[s].mvpn
                                             resolvable
T7 C-<S, G>    Receive via bgp     bgp.mvpn  RightAway   vrf[s].mvpn
                                                         Send T3 S-PMSI

With Leaf Info required
T3 S-PMSI      T6/T7 create in vrf vrf.mvpn  RightAway   bgp.mvpn, vrf[s].mvpn
T3 S-PMSI      Receive via bgp     bgp.mvpn  RightAway   vrf[s].mvpn

With PMSI (Ingress Replication + GlobalTreeRootLabel + Encap: MPLS over GRE/UDP)
T4 Leaf-AD    T3 replicated in vrf vrf.mvpn  GlobalErmRt bgp.mvpn, vrf[s].mvpn
                                             available   Update GlobalErmRt with
                                                         Input Tunnel Attribute
T4 Leaf-AD     Receive via bgp     bgp.mvpn  RightAway   vrf[s].mvpn
               or local replication                 Send xmpp update for ingress
                                                    vrouter with PMSI info

```

## 4.5 Phase-1 Events (For Release 4.1)

In Phase-1 receivers always inside the contrail cluster and sender always
outside the cluster. Support for Source specific multicast <C-S,G> only
(No support for C-<*, G> ASM). No support for Inclusive I-PMSI either.

```
Route-Type     CreateWhen          Primary   ReplicateWhen Secondary
================================================================================
Without Inclusive PMSI
T1 AD          Configure Mvpn      vrf.mvpn  RightAway   bgp.mvpn, vrf[s].mvpn
T1 AD          Receive via bgp     bgp.mvpn  RightAway   vrf[s].mvpn
                                                        Send only to I-BGP Peers

Without Inclusive PMSI
T2 AD          Configure Mvpn      vrf.mvpn  RightAway   bgp.mvpn, vrf[s].mvpn
T2 AD          Receive via bgp     bgp.mvpn  RightAway   vrf[s].mvpn
                                                        Send only to E-BGP Peers

SourceTreeJoin
T7 C-<S, G>    Receive via xmpp    vrf.mvpn  Source is   bgp.mvpn, vrf[s].mvpn
                                             resolvable

With Leaf Info required
T3 S-PMSI      Receive via bgp     bgp.mvpn  RightAway   vrf[s].mvpn

With PMSI (Ingress Replication + GlobalTreeRootLabel + Encap: MPLS over GRE/UDP)
T4 Leaf-AD    T3 replicated in vrf vrf.mvpn  GlobalErmRt bgp.mvpn, vrf[s].mvpn
                                             available   Update GlobalErmRt with
                                                         Input Tunnel Attribute
```

Note: Whenever a route is replicated in bgp.mvpn.0, it is expected to be sent to
all remote bgp peers (subjected to route-target-filtering) and imported into
vrf[-s].mvpn.0 based on the export route-target of the route and import route
target of the table.

Mvpn work flow is essentially handled based on route change notification.
MvpnManager listens to route change notifications to mvpn table. Primary paths
are created and managed by MvpnManager. Secondary paths are created and managed
inside the virtual function MvpnTable::RouteReplicate(). MvpnProjectManager
listens to route change notifications to <project>.ermvpn.0 table and takes
appropriate actions on all mvpn routes applicable for that <S,G>.

## 4.6 Auto Discovery (Type-1 AD and Type-AD Routes)

Each MvpnManager of vrf.mvpn.0 originates Type-1 and Type-2 A-D Routes inside
vrf.mvpn.0 (When ever mvpn is configured/enabled in the VN) (Note: There is no
Inclusive PMSI information encoded when these routes are created/advertised)

Self control-node IP address, router-id and asn are used where ever originator
information is encoded.

Type-1 AD prefix format
```
1:RD:OriginatorIpAddr  (RD in asn:vn-id or router-id:vn-id format)
  1:self-control-node-router-id:vn-id:originator-control-node-ip-address
```

export route target is export route target of the of the routing-instance. These
routes would get imported to all mvpn tables whose import route-targets list
contains this exported route-target (Similar to how vpn-unicast routes get
imported) (aka JUNOS auto-export)

Type-2 AD prefix format

```
2:RD:SourceAs
  1:self-control-node-router-id:vn-id:source-as
```

These routes first get replicated to bgp.mvpn.0 and then get advertised to all
BGP neighbors with whom mvpn AFI is exchanged as part of initial capability
negotiation. This is the bgp based classic mvpn-site auto discovery.

Note: Intra-AS route is only advertised to IBGP neighbors and Inter-AS route
is advertised to only e-bgp neighbors. Since, those neighbors are already part
of distinct peer groups on the outbound side, simple hard-coded filtering can be
applied to get this functionality.

Auto discovered Mvpn neighbors are stored inside map<IpAddress, MvpnNeighbor *>
in each MvpnManager object. (i.e, per virtual-network). Access to this map shall
be protected using a mutex. Clients get a copy of the MvpnNeighbor structure,
upon call to MvpnManager::findNeighbor(). This allows one to update the
neighbors map inline, in RouteListner() method after Type-1/Type-2 routes
are replicated/deleted in vrf[s].mvpn.o and are notified.

## 4.7 Source Tree Join C-<S,G> Routes learning via XMPP/IGMP (Type 7 Join)

Agent sends over XMPP, IGMP joins over VMI as C-<S,G> routes and keeps track of
map of all S-G routes => List of VMIs (and VRFs) (for mapping to tree-id). These
C-<S,G> routes are added to vrf.mvpn.0 table with source protocol XMPP as Type7
route in vrf.mvpn.0. This shall have zero-rd as the source-root-rd and 0 as the
root-as asn (since these values are NA for the primary paths)

Format of Type 7 <C-S, G> route added to vrf.mvpn.0 with protocol local/Mvpn
```
  7:<zero-router-id>:<vn-id>:<zero-as>:<C, G>
```

MvpnManager who is a listener to this route, upon route change notification

1. Register /32 Source address is for resolution in PathResolver.
2. Create/Update DB-State in MvpnManager::RouteStatesMap inside the project
   (S,G) specific MvpnManagerPartition object.

When ever this address is resolvable (or otherwise), the Type-7 route is
notified and the route replicator tries to replicate the path.

o If the route is resolvable (over BGP), next-hop address and rt-import extended
  rtarget community associated with the route is retrieved from the resolver
  data structure (which holds a copy of the information necessary such as
  inside the path-attributes).

  If the next-hop (i.e, Multicast Source) is resolvable, then this Type-7 path
  is replicated into all vrfs applicable, including bgp.mvpn.0.

o If the route is not resolvable any more, then any already replicated Type-7
  path is deleted (By simply returning NULL from MvpnTable::RouteReplicate())

Format of replicated path for Type 7 C-<S, G> replicated path is as shown below.
```
  7:<source-root-rd>:<root-as>:<C, G>
  7:source-root-router-id:vn-id:<root-as>:<C, G>
  export route-target should be rt-import route-target of route towards source
  (as advertised by ingress PE)
```

Note: If the resolved nexthop is not an Mvpn BGP neighbor, then no join must
be sent out. Hence, this Type-7 join route is replicated into bgp.mvpn.0 only
if there is a Type1/Type2 route received from the resolved nexthop bgp neighbor.

On the other hand, if received Type1/Type2 routes get withdrawn, all replicated
type-7 (and type-4 ?) routes for that PE must also be no longer replicated and
instead be deleted from the secondary tables.

If Type1/Type2 routes come in later (auto-discovery happens afterwards), then
all type-7 routes must be replicated and advertised again to the newly
discovered mvpn bgp neighbor.

Concurrency: Since BGP AD related events do not carry any C,G information,
we cannot safely walk all the C,Gs from one partition. Instead, we can just
launch mvpn db tables walk and notify all Type-7 and Type-4 routes. Inside
RouteReplicate(), new type-7/type-4 paths shall be replicated or already
replicated type-7/type-4 paths shall get deleted based on BGP Mvpn auto
discovered neighbor presence/absence.

Q: Instead, should we maintain a set of all type-7 and type-4 paths (on a per
partition basis)? Changes to auto discovery states are quite rare in nature,
though. Hence it seems this scenario can be handled simply by walking the entire
vrf.mvpn.0 table.

Once replicated into bgp.mvpn.0, the secondary path will be advertised to all
other mvpn neighbors. (Route Target Filtering will ensure that it is only
sent to the ingress PE). For phase 1 this route only needs to be replicated to
bgp.mvpn.0 (Sender is always outside the contrail cluster). There is no need
to replicate this route to any other vrf locally. This is required when we add
support for sender in one virtual-network and receivers in another with in the
contrail cloud.

Any change to the reachability to Source shall be dealt as delete of old Type-7
secondary path and add of new Type-7 secondary path.

Note: This requires advertising IGMP Routes as XMPP routes into different table
<project>.mvpn.0 (in addition to vrf.mvpn.0). Hence requires changes to agents.
Agents should refcount and advertise <S,G> once to <project>.mvpn.0, as new
receivers join (or old receivers leave).

## vrf_index

In bgp.mvpn.0 table, all mvpn prefixes across all VNs reside. Hence the routes
are made distinct by prepending with appropriate route distinguisher. The format
of this would be <router-id>:<vrf-id>. Router-Ids could be self (control-node)
router-id or a remote peer router-id as appropriate. the VRF-ID though must be
the unique virtual network id associated with that router-id. For remote peers,
this id can be retrieved from the Type-1/Type-2 auto-discovery routes. Locally,
this is allocated during RoutingInstance construction.

## 4.8 Nexthop resolver for source

As mentioned in previous section, when C-<S, G> route is received and installed
in vrf.mvpn.0 table (protocol: XMPP), Source needs to be resolved in vrf.inet.0
table in order reach out to the ingress PE. Hence, Type-7 paths added in
BgpXmppChannel::ProcessMvpnItem() are always marked for "ResolutionRequested".

This ensures that route resolution is started when those routes are added to the
BgpTable automatically, by the current implementation. When Type-7 routes are
deleted, current code automatically stops resolution requests as well.

PathResolver code should be slightly modified though in order to

1. Support longest prefix match
2. Add resolved paths into rt only conditionally. mpvn does not need explicit
   resolved paths in the route. It only needs unicast route resolution.
3. When unicast RPF Source is [un-]resolved, the Type-7 join route is notified.
   Inside MvpnTable::Replicate(), Type-7 C-<S,G> route can be replicated into
   bgp.mvpn.0 table with the correct RD and export route target (or delete the
   route if it was replicated before and the Source is no longer resolvable, or
   if resolution parameters change). Check must be also made to ensure that
   resolved nexthop is also an active mvpn bgp neighbor.

When this <S,G> Type-7 route sent by egress PE is successfully imported into
vrf.mvpn.0 (by matching auto-generated rt-import route-target) by the ingress
PE and if provider-tunnel is configured, then ingress PE should generate Type-3
C-<S, G> S-PMSI AD Route into vrf.mvpn.0 table. From here, this would replicated
to bgp.mvpn.0 and is advertised to remote (egress) PEs.

e.g. Ingress (Sender) PE S-PMSI configuration in JUNOS

```
set routing-instances vrf provider-tunnel selective group 232.2.0.0/16 source 1.2.3.4/32 ingress-replication label-switched-path
```

## 4.9 C-<*, G> Routes learning via Agents/IGMP

This is not targeted for Phase 1. Please refer to Section 4.3 for general Routes
flow for these ASM routes.

## 4.10 Type-3 S-PMSI

```3:<root-rd>:<C-S,G>:<Sender-PE-Router-Id> (Page 240)```

Target would be the export target of the vrf (so it would get imported into
vrf.mvpn.0 in the egress pe)

PMSI Flags: Leaf Information Required 1 (So that egress can initiate the join
for the pmsi tunnel) (Similar to RSVP based S-PMSI, Page 251) TunnelType is
expected to be only Ingress-Replication and Label is always expected to be 0.

This route is originated when ever Type-6/Type-7 Customer Join routes are
received and replicated in vrf.mvpn.0 (at the ingress). Origination of this
route in control-node is not required for Phase 1.

## 4.11 Type-4 PMSI Tunnel advertisement from egress (Leaf AD)


```
4:<S-PMSI-Type-3-Route-Prefix>:<Receive-PE-Router-ID>
Route target: <Ingress-PE-Router-ID>:0
PMSI Tunnel: Tunnel Type: Ingress-Replication and label:Tree-Label

```

When Type-3 route is imported into vrf.mvpn.0 table at the egress, (such as
control-node), the secondary Type-3 route is notified.

MvpnManager, upon notification for any Type-3 route shall do the following.

On Add/Change

1. Originate a new Type-4 route in vrf.mvpn.0 table corresponding to the newly
   arrived Type-3 route and add this to the S-G specific map inside associated
   MvpnProjectManager object.
2. No PMSI Tunnel attribute is encoded (yet) to this Type-4 path
3. Notify the route

On Delete, any Type-4 path originated is retrieved from the DB state and
deleted.

This type-4 path is replicated into bgp and other tables if PMSI information can
be computed (in MvpnTable::RouteReplicate()). PMSI Information is retrieved via
api ErmVpnManager::GetGlobalRouteForestNodePMSI(). Specifically, Mvpn needs the
input label and IP address of the MCast Forwarder (Vrouter) of the forest node
of the global (level-1) ermvpn tree. If this PMSI info is present, the type-4
leaf-ad route is replicated into bgp and other tables as appropriate.

Also, GlobalErmVpnRoute is notified so that the forest node xmpp route can be
updated with appropriate input tunnel information (Provided by Mvpn module,
which is the IP address present in the prefix of this originated leaf-ad route)

If PMSI information cannot be retrieved, (e.g. the global level-1 ermvpn tree is
not computed yet), then type-4 path is not replicated into other tables.

On the other hand, when global tree is computed or updated, MvpnProjectManager
listener gets called. In this callback,

o Walk the list of all type-4 paths already originated for this S-G and notify.

o RouteReplicate() of Type-4 path would run like how it did before and can now
  [un-]replicate the type-4 path based on the [un-]availability of the PMSI
  information.

MvpnManager shall provide an API to ErmVpnManager to encode input tunnel attr
for the forest node of the (self) local tree.

MvpnManager::GetInputTunnelAttr(ErmVpnRoute *global_ermvpn_tree_route) const;

ErmVpn manager shall call this API when ever forest node route (olist) is
computed. MvpnManager can find this info from the associated db state. If the
route is indeed replicated successfully, then the ingress PE router address can
be provided as input tunnel attribute (which is part of the Type-4 route prefix)

Reference: Page 254

? It goes into correct vrf only because the prefix itself is copied ?

## 4.12 Source AS extended community

This is mainly applicable for inter-as mvpn stitched over segmented tunnels.
This is not targeted for Phase 1.

## 4.13 Multicast Edge Replicated Tree

Build one tree for each unique C-<S, G> combination under each tenant/project.
Note: <S,G> is expected to be unique within a tenant (project) space.

Tree can be built completely using existing ErmVpn feature.

Master control-node which builds level-1 tree shall also be solely responsible
for advertising mvpn routes. Today, this is simply decided based on control-node
with the lowest router-id.

Agent shall continue to advertise routes related to BUM traffic over xmpp to
vrf.ermvpn.0 table. However, all mvpn routes (which are based on IGMP joins or
leaves) are always advertised over XMPP to <project/tenant>.mvpn.0 table.

Control-node shall build an edge replicated multicast tree like how it does so
already. PMSI Tunnel information is gathered from the forest node of the locally
computed tree. (bottom-most and right-most leaf node in the replication tree)

When advertising Type3 Leaf AD Route (Section 4.7), egress must also advertise
PMSI tunnel information for forwarding to happen. Mvpn Manager shall use the
root node ingress label as the label value in PMSI tunnel attribute

When ever tree is recomputed (and only if the forest node changes), Type4 route
is updated and re-advertised as necessary. If a new node is selected as root,
Type-4 route can be simply updated and re-advertised (implicit withdraw and
new update)

ErmVpn Tree built inside the contrail cluster is fully bi-directional and self
contained. Vrouter would flood the packets within the tree only so long as the
packet was originated from one of the nodes inside the tree (in the oif list)

This is no longer true when a single node of the tree is stitched with the SDN
gateway using ingress replication. The stitched node should be programmed to
accept multicast packets from SDN gateway (over the GRE/UDP tunnel) and then
flood them among all the nodes contained inside the tree. This is done by
encoding a new "Input Tunnel Attribute" to the xmpp route sent to the agent.
This attribute shall contain the IP address of the tunnel end point (SDN GW)
as well as the Tunnel Type (MPLS over GR/MPLS over UDP) as appropriate.

Vrouter should relax its checks and indeed accept received multicast packets
from this SDN gateway even though that incoming interface may not be part of the
oif list.

## 4.14 Internal Dependencies

vrf.inet.0 table must be available when Type-7 routes are added to vrf.mvpn.0
so that route resolution for the source address can be started. At present,
BgpXmppChannel module defers all tables subscribe/unsubscribe and routes
addition/deletion from the agent until the RoutingInstance and the tables are
created (via configuration processing). This dependency logic must be extended
to span across all families (tables).


This design also relies on the presence of <project/tenant> specific virtual
network. This routing-instance is created based on the configuration. No
assumption shall be made in the order of processing of this configuration
processing. Instead, MvpnManager module shall handle the scenario in which
<project>.mvpn.0 may not be present at certain time and may come in later.

# 5. Performance and scaling impact

##5.2 Forwarding performance

# 6. Upgrade
This feature requires automatic creation and management of project or tenant
specific virtual-network in the configuration. Care must be taken to ensure that
old configuration which would not have such a virtual-network continues to work
correctly.

# 7. Deprecations
####If this feature deprecates any older feature or API then list it here.

# 8. Dependencies

## 8.1

1. Project specific virtual-network to manage forwarding of multicast data
2. IGMP support in contrail vrouter agent
3. Advertisement of IGMP routes over XMPP to vrf.mvpn.0 and <project>.mvpn.0
4. Ingress Replication over MPLS-over-GRE/MPLS-over-UDP tunnels in SDN (JUNOS)

# 9. Testing
## 9.1 Unit tests

## 9.2 Dev tests

## 9.3 System tests

# 10. Documentation Impact

# 11. References
1. [Multicast in MPLS/BGP IP VPNs](https://tools.ietf.org/html/rfc6513)
2. [BGP Encodings and Procedures for Multicast in MPLS/BGP IP VPNs](https://tools.ietf.org/html/rfc6514)
3. [Ingress Replication Tunnels in Multicast VPN](https://tools.ietf.org/html/rfc7988)
4. [Extranet Multicast in BGP/IP MPLS VPNs](https://tools.ietf.org/html/rfc7900)
5. [BGP/MPLS IP Virtual Private Networks (VPNs)](https://tools.ietf.org/html/rfc4364)
