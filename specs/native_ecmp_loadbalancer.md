
# 1. Introduction
Add support for ecmp based loadbalancer in OpenContrail.

# 2. Problem statement
Currently OpenContrail supports L4-L7 proxy based loadbalancers. This is done by instantiating haproxy on a compute node. If there is no requirement for proxy based loadbalancers then ecmp based loadbalancing helps prevents traffic tromboning.
## 2.1 Use cases
In container orchestration systems such as Kubernetes there is a need to distribute traffic to PODS behind a service. Proxy based solutions are not distributed and add overhead for simple distribution of traffic to stateless PODS. Hence the native ECMP based solution is needed.

# 3. Proposed solution
OpenContrail supports ECMP based forwarding if the same IP address is announced by different endpoints. This is based on a hash of the five tuple:
* Source ip
* Source port
* Destination ip
* Desination port
* Protocol

## 3.1 Alternatives considered
Proxy solutions are potential alternatives.

## 3.2 API schema changes
New loadbalancer provider called "native" is being added to the loadbalancer object. The rest of the objects remain the same as before in the loadbalancer hierarchy.
* User creates a port for a virtual-ip.
* User provides the list of endpoint IPs as members for the VIP.


## 3.3 User workflow impact
#### Set the configuration for provider as "native"
	
* neutron net-create private-net
* neutron subnet-create --name private-subnet private-net 30.30.30.0/24
* neutron lbaas-loadbalancer-create $(neutron subnet-list | awk '/ private-subnet / {print $2}') --name lb1
* neutron lbaas-listener-create --loadbalancer lb1 --protocol-port 80 --protocol HTTP --name listener1
* neutron lbaas-pool-create --name pool1 --protocol HTTP --listener listener1 --lb-algorithm ROUND_ROBIN
* neutron lbaas-member-create --subnet private-subnet --address 30.30.30.10 --protocol-port 8080 mypool
* neutron lbaas-member-create --subnet private-subnet --address 30.30.30.11 --protocol-port 8080 mypool


## 3.4 UI changes
#### UI changes will be done to configure the loadbalancer

## 3.5 Notification impact
#### Logs, UVE and stats will be generated from the controller and agent.

# 4. Implementation
## 4.1 Assignee(s)
####  Yuvaraja Mariappan (Dev)


## 4.2 Work items
### Controller changes

* Controller now aggregates the configuration based on the provider. In the case of native provider controller allocates a new floating-ip object as the child of the instance-ip. It copies the IP address from the instance-ip into the floating-ip. Controller also copies the port translations from VIP to members in the floating-ip object. There is a flag to identify that port-translations are enabled on the floating-ip.
* In addition controller also attaches this floating IP to all the virtual machine interface objects of the endpoints/pool members.

#### Agent changes
* Agent acts on the new floating-ip object. Based on the port translation flag agent creates a port translation table for the floating ip. In addition agent traverses its parent instance-ip to get the corresponding routing table information.

# 5. Performance and scaling impact
## 5.1 API and control plane
There is no impact on API and control plane performance.

## 5.2 Forwarding performance
Forwarding performance should be better than proxy based solutions but this needs to be characterized during the test cycle.

# 6. Upgrade
#### Describe upgrade impact of the feature
The native loadbalancer is a new feature and hence does not have any upgrade impact.

# 7. Deprecations
####  Not applicable

# 8. Dependencies
####  Health check implementation for native loadbalancer depends on the following bluprint
https://blueprints.launchpad.net/opencontrail/+spec/service-health-check

# 9. Testing
## 9.1 Unit tests 
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References
