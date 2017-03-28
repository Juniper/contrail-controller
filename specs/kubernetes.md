
# 1. Introduction
Kubernetes (K8s) is an open source container management platform. It provides a portable platform across public and private clouds. K8s supports deployment, scaling and auto-healing of applications. More details can be found at: http://kubernetes.io/docs/whatisk8s/




# 2. Problem statement
There is a need to provide pod addressing, network isolation, policy based security, gateway, SNAT, loadalancer and service chaining capability in Kubernetes orchestratation. To this end K8s supports a framework for most of the basic network connectivity. This pluggable framework is called Container Network Interface (CNI). Opencontrail will support CNI for Kubernetes.
 

# 3. Proposed solution
Currently K8s provides a flat networking model wherein all pods can talk to each other. Network policy is the new feature added to provide security between the pods. Opencontrail will add additional networking functionality to the solution - multi-tenancy, network isolation, micro-segmentation with network policies, load-balancing etc. Opencontrail can be configured in the following mode in a K8s cluster:

* Cluster isolation

This is the default mode, and no action is required from the admin or app developer. It provides the same isolation level as kube-proxy. OpenContrail will create a cluster network shared by all namespaces, from where service IP addresses will be allocated.

* Namespace isolation mode

The cluster admin can configure a namespace annotation to turn on isolation. As a result, services in that namespace will not be accessible from other namespaces, unless security groups or network policies are defined explicitly.

* App isolation mode

In this finer-grain isolation mode, the admin or app developer can add the label "opencontrail.org/name" to the pod, replication controller and/or service specification, to enable micro-segmentation. As a result, virtual networks will be created for each pod/app tagged with the label. Network policies or security groups will need to be configured to define the rules for service accessibility.


# 4. Implementation
## 4.1 Contrail kubernetes manager
Opencontrail implementation requires listening to K8s API messages and create corresponding resources in the Opencontrail API database. New module called "contrail-kube-manager" will run in a container to listen to the messages from the K8s api-server. A new project will be created in Opencontrail for each of the namespaces in K8s.

## 4.2 Contrail CNI plugin

## 4.3 Loadbalancer for K8s service
Each service in K8s will be represented by a loadbalancer object. The service IP allocated by K8s will be used as the VIP for the loadbalancer. Listeners will be created for the port on which service is listening. Each pod will be added as a member for the listener pool. contrail-kube-manager will listen for any changes based on service labels or pod labels to update the member pool list with the add/updated/delete pods.  

Loadbalancing for services will be L4 non-proxy loadbalancing based on ECMP. The instance-ip (service-ip) will be linked to the ports of each of the pods in the service. This will create an ECMP next-hop in Opencontrail and traffic will be loadbalanced directly from the source pod.

## 4.4 Security groups for K8s network policy

Network policies can be applied in a cluster configured in isolation mode, to define which pods can communicate with each other or with other endpoints.
The cluster admin will create a Kubernetes API NetworkPolicy object. This is an ingress policy, it applies to a set of pods, and defines which set of pods is allowed access. Both source and destination pods are selected based on labels. The app developer and the cluster admin can add labels to pods, for instance “frontend” / “backend”,  and “development” / “test” / “production”. Full specification of the Network Policy can be found here:

http://kubernetes.io/docs/user-guide/networkpolicies/

Contrail-kube-manager will listen to Kubernetes NetworkPolicy create/update/delete events, and will translate the Network Policy to Contrail Security Group objects applied to Virtual Machine Interfaces. The algorithm will dynamically update the set of Virtual Machine Interfaces as pods and labels are added/deleted.

## 4.5 DNS
Kubernetes(K8S) implements DNS using SkyDNS, a small DNS application that responds to DNS requests for service name resolution from Pods. On K8S, SkyDNS runs as a Pod.  


# 5. Performance and scaling impact

## 5.2 Forwarding performance

# 6. Upgrade

# 7. Deprecations

# 8. Dependencies
* Native loadbalancer implementation is needed to support service loadbalancing. https://blueprints.launchpad.net/juniperopenstack/+spec/native-ecmp-loadbalancer
* Health check implementation

# 9. Testing
## 9.1 Unit tests
## 9.2 Dev tests
## 9.3 System tests

# 10. Documentation Impact

# 11. References
