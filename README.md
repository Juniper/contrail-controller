contrail-controller
===================

Test
# Contrail Virtual Network Controller

This software is licensed under the Apache License, Version 2.0 (the "License");
you may not use this software except in compliance with the License.
You may obtain a copy of the License at http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.

### Overview

The Contrail Controller repository contains the code for the configuration management, analytics and control-plane components of the Contrail network virtualization solution.

* The data-plane component (aka vrouter) is available in a separate code repository (http://github.com/Juniper/contrail-vrouter).

* The configuration management component is located under `src/config`. It provides a REST API to an orchestration system and translates the system configuration as an [IF-MAP](http://www.trustedcomputinggroup.org/files/resource_files/2888CAD9-1A4B-B294-D0ED95712B121FEF/TNC_IFMAP_v2_1r15.pdf) database.
 
* The configuration schema used by the contrail controller is defined under `src/schema`. A [code generation tool](http://github.com/Juniper/contrail-generateds) is used to convert the schema into accessor methods used by the API clients (`src/api-lib`), the API server as well as the control-plane components.

* The control-node daemon code is located under `src/{bgp,control-node,ifmap,xmpp}`. It implements the operational state database and interoperates with networking equipment as well as the compute-node agents. The protocol used between the control-node and the compute-node agents is documented as an [IETF draft](http://tools.ietf.org/html/draft-ietf-l3vpn-end-system-01). This component contains the network reachability (a.k.a. routing) information in the system which is transient and can potentially have a higher rate of change than the configuration state.

* The compute-node agent (`src/vnsw`) is a deamon than runs on every
  compute node and programs the data-plane in the host operating system.

Data gathered from all these components is collected into a logically centralized database (`src/{analytics,opserver}`).

### Contributing code
* Sign the [CLA](https://na2.docusign.net/Member/PowerFormSigning.aspx?PowerFormId=cf81ffe2-5694-4ad8-9d92-334fc57a8a7c)
* Submit change requests via gerrit at http://review.opencontrail.org.

[![ga](https://www.google-analytics.com/__utm.gif?utmac=UA-44166833-1&utmp=contrail-controller%2FREADME.md&utmdt=README.md)](https://www.google-analytics.com)
