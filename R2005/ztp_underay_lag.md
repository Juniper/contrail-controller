# 1. Introduction
During ZTP, if multiple links are detected between leaf and spine, then they should be bundled as AE interface.
BGP would be configured on AE interface.

# 2. Problem statement
Currently during ZTP, particularly during topology discovery, when  multiple links are present
between leaf and spine, each link is treated seperately and logical interfaces are created for each of those
interfaces. Hence multiple instances of BGP(underlay) are running between same leaf and spine.

# 3. Proposed solution
CEM will now discover multiple links existing between leaf and spine and AUTOMATICALLY (without requiring any input from the user),
configure one Aggregated Ethernet interface rather than individual interfaces. Create new physical interface and logical interface of type "lag".
IP addresses assigned to the leaf-spine links are assigned to the AE interface and NOT to the individual links. This would have a single BGP
session running between a leaf and spine.

Note: Assumption is all fabric links between one leaf and spine are of same speed i.e all fabric links are of speed 10/40/100G

# 4. Implementation

During ZTP, in topology discovery all point to point connected interfaces are identified using lldp. All parallel links between a leaf and spine
would be converted to single AE link. This would have ae-index comming from common pool used by overlay AE(VPG), as CEM should not get opverlapping
AE-index for overlay and underlay LAG. Single link would be configured as its configured today. In implementation virtual-port-group object would
be used for bundling physical interfaces.
CEM will support LACP and default minimum-links(value is 1) config for underlay AE interfaces.

# 5. API schema changes
No changes

# 7. UI Changes - ( will update this once the screens are ready)
No changes

# 8. Testing
1. Connect parallel links between leaf and spine. Following objects/configs need to be verified:
    a) On both leaf and spine device "ae<num>" physical and "ae<num>.0" logical interfaces should be created.
       All parallel links between them should be part of this bundle.
    b) In database VPG object, Physical and logical interface of type "lag" for both leaf and spine should be created.
       VPG object should have refs to actual physical interfaces discovered between leaf and spine.
    c) Instance IP is allocated to logical interface of type "lag"
2. Change from parallel links to single link between leaf and spine.
    a) On both leaf and spine device "ae<num>" physical and "ae<num>.0" logical interfaces should be deleted.
    b) In database Physical and logical interface of type "lag" for both leaf and spine should be deleted. VPG
       object should have single reference and reference parameters should not have "ae_num".
    c) Instance IP should be allocated to physical interface.

# 9. Documentation Impact
