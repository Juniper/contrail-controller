contrail-controller
===================

Contrail Virtual Network Controller

The Contrail Controller repository contains the code for the configuration management, analytics and control-plane components of the Contrail network virtualization solution.

The data-plane component (aka vrouter) is avaialable in a separate code repository (http://github.com/Juniper/contrail-router).

The configuration management component is located under 'src/config'. It provides a REST API to an orchestration system and translates the system configuration as an [IF-MAP](http://www.trustedcomputinggroup.org/files/resource_files/2888CAD9-1A4B-B294-D0ED95712B121FEF/TNC_IFMAP_v2_1r15.pdf) database.

The configuration schema used by the contrail controller is defined under src/schema. A [code generation tool](http://github.com/Juniper/contrail-generateds) is used to convert the schema into accessor methods used by the API clients (src/api-lib), the API server as well as the control-plane components.

The control-node daemon code is located under (src/{bgp,control-node,ifmap,xmpp}). It implements the operational state database and iteroperates with networking equipment as well as the compute-node agents. The protocol used between the control-node and the compute-node agents is documented as an [IETF draft](http://tools.ietf.org/html/draft-ietf-l3vpn-end-system-01). This component contains the network reachability (a.k.a. routing) information in the system which is transient and can potentially have a higher rate of change than the configuration state.

The compute-node agent (src/vnsw) is a deamon than runs on every
compute node and programs the data-plane in the host operating system.


Data gathered from all these components is collected into a logically centralized database (src/{analytics,opserver}).