
# 1. Introduction
Kubernetes (K8s) is an open source container management platform. It provides a portable platform across public and private clouds. K8s supports deployment, scaling and auto-healing of applications. More details can be found at: http://kubernetes.io/docs/whatisk8s/

# 2. Problem statement
In Kuberntes 1.3, cluster federation was introduced and there were many improvement done in kubernetes 1.5. In Kubernetes 1.6, RBAC feature is introduced as Beta feature. Also with Kubernetes 1.7, network-policy is made a GA feature. In contrail 4.1, these features will be supported.


# 3. Proposed solution

## 3.1.1 Network-policy
With Kubernetes 1.7, NetworkPolicy has been moved from `extensions/v1beta1` to the new `networking.k8s.io/v1` API group. The structure remains unchanged from the beta1 API. The `net.beta.kubernetes.io/network-policy` annotation on Namespaces to opt in to isolation has been removed as well. Contrail-kube-manager needs to support these changes. Contrail has introduced the firewall policy framework. The framework simplified creation of a policy and application of the policy to Virtual-Machines, Containers and Pods. We plan to move network-policy implementation from Security-Group to Application Policy Set (APS).

## 3.1.2 RBAC
Mapping of RBAC policies between Kubernetes and Contrail.

## 3.1.3 Federation
Federation makes it easy to manage multiple clusters by synchronizing resources across multiple clusters and providing cross cluster discovery. Cluster federation enables high availabilty by spreading load across clusters. It also helps customers migrate applications across clusters avoiding vendor lock-in.

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

# 12. References
