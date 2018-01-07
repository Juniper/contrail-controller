# 1. Introduction
Kubernetes (K8s) is an open source container management platform. It provides a portable platform across public and private clouds. K8s supports deployment, scaling and auto-healing of applications. More details can be found at: http://kubernetes.io/docs/whatisk8s/

# 2. Problem statement
1. Reachability to public cloud services for pods.
2. Service or Ingress reachablity from outside of the cluster in isolated namespaces and service chaining support.
3. kubernetes-1.9 network-policy support.
4. kubernetes-1.9 RBAC support.

# 3. Proposed solution
## 3.1.1 Reachability to public cloud services for pods
In 4.1, pod can reach any service in ip-fabric networks as long as the compute node has the vrouter. When pod reaches to a service in another compute node, it leaves source compute node with encapsulation. If the destination compute node does not have the vrouter, the destination node drops the packets since it does not know how to handle the encapsulated packet. So, services would not be reached by pods. It is same as with public cloud infrastructure where there is no vrouter. If the pods have to reach those services, it has to be part of the underlay network [no encapsulation/de capsulation]. It can be achieved by contrail feature ip-fabric-forwarding.

Contrail feature ip-fabric-forwarding will make overlay networks as part of the underlay(ip-fabric) network. So, there is no need for the encapsulation/decapsulation. In ip-fabric-forwarding, for each destination, only one route would be exported in the current infrastructure. So ecmp will always have one route. Since kubernetes service is implemented as ecmp loadbalancer in contrail, service cannot be part of the ip-fabric-forwarding. So, default cluster-network would be separated as cluster-pod-network and cluster-service-network. Cluster-pod-network will have ip-fabric-forwarding by default. Configuration option would be provided to disable ip-fabric-forwarding in cluster-pod-network. Cluster-service-network would be in overlay. Contrail network policy would be created between cluster-pod-network and cluster-service-network for the reachablity between pods and services.

## 3.1.2 Service or Ingress reachablity from outside cluster in isolated namespaces and service chaining support
Even though it is an isolated namespace, service ip is allocated from the cluster-network. So, by default service from one network can reach service from another network. security groups in isolated namespace stops the reachability from other networks which also prevents traffic from outside of the cluster. In order to give access to external entity, the security group would be changed to allow all which defeats the isolation purpose. To address this, two networks would be created in the isolated namespaces. One is for pods and one is for services. Service uses the same service-ipam which will be made as a flat-subnet like pod-ipam. Since each network would have its own vrf, security groups are not needed to control incoming traffic. So, it will be removed.
   As services would have its own network, it will give the option to create service-chaining.

## 3.1.3 kubernetes-1.9 network-policy support
With Kubernetes 1.9, Network Policy has been in `networking.k8s.io/v1` API group. The structure remains unchanged from the beta1 API. The `net.beta.kubernetes.io/network-policy` annotation on Namespaces to opt in to isolation has been removed as well. Contrail-kube-manager needs to support these changes. Contrail has introduced the firewall policy framework. The framework simplified creation of a policy and application of the policy to Virtual-Machines, Containers and Pods. kubernetes network-policy implementation will be moved from Security-Group to Application Policy Set (APS).

## 3.1.4 kubernetes-1.9 RBAC support
Mapping of RBAC policies between Kubernetes and Contrail.

# 3.2.1 Implementation

# 4. Implementation

# 5. Performance and scaling impact

## 5.1 API and control plane
None

## 5.2 Forwarding performance

# 6. Upgrade

# 7. Deprecations
None

# 8. Dependencies

# 9. Debugging

# 10. Testing
## 10.1 Unit tests
## 10.2 Dev tests
## 10.3 System tests

# 11. Documentation Impact
