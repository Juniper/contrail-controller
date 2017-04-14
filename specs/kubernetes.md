
# 1. Introduction
Kubernetes (K8s) is an open source container management platform. It provides a portable platform across public and private clouds. K8s supports deployment, scaling and auto-healing of applications. More details can be found at: http://kubernetes.io/docs/whatisk8s/




# 2. Problem statement
There is a need to provide pod addressing, network isolation, policy based security, gateway, SNAT, loadalancer and service chaining capability in Kubernetes orchestratation. To this end K8s supports a framework for most of the basic network connectivity. This pluggable framework is called Container Network Interface (CNI). Opencontrail will support CNI for Kubernetes.
 

# 3. Proposed solution
Currently K8s provides a flat networking model wherein all pods can talk to each other. Network policy is the new feature added to provide security between the pods. Opencontrail will add additional networking functionality to the solution - multi-tenancy, network isolation, micro-segmentation with network policies, load-balancing etc. Opencontrail can be configured in the following mode in a K8s cluster:

3.1 Cluster isolation

Kubernetes imposes the following fundamental requirement on any networking implementation:

All Pods can communicate with all other containers without NAT

This is the default mode, and no action is required from the admin or app developer. It provides the same isolation level as kube-proxy. OpenContrail will create a cluster network shared by all namespaces, from where service IP addresses will be allocated.

This in essence is the "default" mode in Contrail networking model in Kubernetes.
In this mode, ALL pods in ALL namespaces that are spawned in the Kubernetes cluster will
be able to communicate with each other. The IP addresses for all the pods will be allocated
from a pod subnet that the Contrail Kubernetes manager is configured with.

NOTE:
System pods spawned in Kube-system namespace are NOT run in the Kubernetes Cluster. Rather they run in the underlay. Networking for these pods is not handled by Contrail.

3.1.1 Implementation

Contrail achieves this inter-pod network connectivity by configuring all the pods in a single Virtual-network. When the cluster is initialized, Contrail creates a virtual-network called "cluster-network".

In the absence of any network segmentation/isolation configured, ALL pods in ALL namespaces get assigned to "cluster-network" virtual-network.

3.1.2   Pods

In Contrail, each POD is represented as a Virtual-Machine-Interface/Port.

When a pod is created, a vmi/port is allocated for that POD. This port is made a member of the default virtual-network of that Kubernetes cluster.

3.1.3   Pod subnet:

The CIDR to be used for IP address allocation for pods is provisioned as a configuration to
contrail-kube-manger. To view this subnet info:

Login to contrail-kube-manager docker running on the Master node and see the "pod_subnets" in configuration file:  /etc/contrail/contrail-kubernetes.conf

3.2 Namespace isolation mode

In addition to default networking model mandated by Kubernetes, Contrail support additional, custom networking models that makes available the many rich features of Contrail to the users of the Kubernetes cluster. One such feature is network isolation for Kubernetes namespaces.

A Kubernetes namespace can be configured as “Isolated” by annotating the Kubernetes namespace metadata with following annotation:

“opencontrail.kubernetes.isolated” : “true”

Namespace isolation is intended to provide network isolation to pods.
The pods in isolated namespaces are not reachable to pods in other namespaces in the cluster.

Kubernetes Services are considered cluster resources and they remain reachable to all pods, even those that belong to isolated namespaces. Thus Kubernetes service-ip remains reachable to pods in isolated namespaces.

If any Kubernetes Service is implemented by pods in isolated namespace, these pods are reachable to pods from other namespaces through the Kubernetes Service-ip.

A namespace annotated as “isolated” has the following network behavior:

a.  All pods that are created in an isolated namespace have network reachability with each other.
b.  Pods in other namespaces in the Kubernetes cluster will NOT be able to reach pods in the isolated namespace.
c.  Pods created in isolated namespace can reach pods in other namespaces.
d.  Pods in isolated namespace will be able to reach ALL Services created in any namespace in the kubernetes cluster.
e.  Pods in isolated namespace can be reached from pods in other namespaces through Kubernetes Service-ip.

3.2.1 Implementation:

For each namespace that is annotated as isolated, Contrail will create a Virtual-network with name:  “<Namespace-name>-vn”

3.2.2   Pods:

A Kubernetes pod is represented as vmi/port in Contrail. These ports are mapped to the virtual-network created for the corresponding isolated-namespace.

3.2.3   Kubernetes Service Reachability:

Pods from an isolated namespace should be able to reach all Kubernetes in the cluster.

Contrail achieves this reachability by the following:

1.  All Service-IP for the cluster is allocated from a Service Ipam associated with the default cluster virtual-network.
2.  Pods in isolated namespaces are associated with a floating-ip from the default cluster-network. This floating-ip makes is possible for the pods in isolated-namespaces to be able to reach Services and Pods in the non-isolated namespaces.

3.3 App isolation mode

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

9. Debugging

9.1 Pod IP Address Info:

    The following command can be used to determine the ip address assigned to a pod:

    kubectl get pods --all-namespaces -o wide

    Example:

    [root@k8s-master ~]# kubectl get pods --all-namespaces -o wide
    NAMESPACE     NAME                                 READY     STATUS    RESTARTS   AGE       IP              NODE
    default       client-1                             1/1       Running   0          19d       10.47.255.247   k8s-minion-1-3
    default       client-2                             1/1       Running   0          19d       10.47.255.246   k8s-minion-1-1
    default       client-x                             1/1       Running   0          19d       10.84.31.72     k8s-minion-1-1

9.2 Check Pods reachability:

    To verify that pods are reachable to each other, we can run ping among pods:

    kubectl exec -it <pod-name> ping <dest-pod-ip>

    Example:

    [root@a7s16 ~]# kubectl get pods -o wide
    NAME                        READY     STATUS    RESTARTS   AGE       IP              NODE
    example1-2960845175-36xpr   1/1       Running   0          43s       10.47.255.251   b3s37
    example2-3163416953-pldp1   1/1       Running   0          39s       10.47.255.250   b3s37

    [root@a7s16 ~]# kubectl exec -it example1-2960845175-36xpr ping 10.47.255.250
    PING 10.47.255.250 (10.47.255.250): 56 data bytes
    64 bytes from 10.47.255.250: icmp_seq=0 ttl=63 time=1.510 ms
    64 bytes from 10.47.255.250: icmp_seq=1 ttl=63 time=0.094 ms

9.3 Verify that default virtual-network for a cluster is created:

    In the Contrail GUI, verify that a virtual-network named “cluster-network” is created in your project.

9.4 Verify a virtual-network is created for an isolated namespace:

    In the Contrail-GUI, verify that a virtual-network with the name format: “<namespace-name>-
    vn” is created.

9.5 Verify that Pods from non-isolated namespace CANNOT reach Pods in isolated namespace.

    1.  Get the ip of the pod in isolated namespace.
    [root@a7s16 ~]# kubectl get pod -n test-isolated-ns -o wide
    NAME                        READY     STATUS    RESTARTS   AGE       IP              NODE
    example3-3365988731-bvqx5   1/1       Running   0          1h        10.47.255.249   b3s37

    2.  Ping the ip of the pod in isolated namespace from a pod in another namespace:
    [root@a7s16 ~]# kubectl get pods
    NAME                        READY     STATUS    RESTARTS   AGE
    example1-2960845175-36xpr   1/1       Running   0          15h
    example2-3163416953-pldp1   1/1       Running   0          15h

    [root@a7s16 ~]# kubectl exec -it example1-2960845175-36xpr ping 10.47.255.249
            --- 10.47.255.249 ping statistics ---
     2 packets transmitted, 0 packets received, 100% packet loss

9.6 Verify that Pods in isolated namespace can reach Pods in in non-isolated namespaces.

    1.  Get the ip of the pod in non-isolated namespace.

    [root@a7s16 ~]# kubectl get pods -o wide
    NAME                        READY     STATUS    RESTARTS   AGE       IP              NODE
    example1-2960845175-36xpr   1/1       Running   0          15h       10.47.255.251   b3s37
    example2-3163416953-pldp1   1/1       Running   0          15h       10.47.255.250   b3s37

    2.  Ping the ip of the pod in non-isolated namespace from a pod in isolated namespace:

    [root@a7s16 ~]# kubectl get pods -o wide
    NAME                        READY     STATUS    RESTARTS   AGE       IP              NODE
    example1-2960845175-36xpr   1/1       Running   0          15h       10.47.255.251   b3s37
    example2-3163416953-pldp1   1/1       Running   0          15h       10.47.255.250   b3s37
    [root@a7s16 ~]# kubectl exec -it example3-3365988731-bvqx5 -n test-isolated-ns ping 10.47.255.251
    PING 10.47.255.251 (10.47.255.251): 56 data bytes
    64 bytes from 10.47.255.251: icmp_seq=0 ttl=63 time=1.467 ms
    64 bytes from 10.47.255.251: icmp_seq=1 ttl=63 time=0.137 ms
    ^C--- 10.47.255.251 ping statistics ---
    2 packets transmitted, 2 packets received, 0% packet loss
    round-trip min/avg/max/stddev = 0.137/0.802/1.467/0.665 ms

9.7 How to check if a Kubernetes namespace is isolated.

    Use the following command to look at annotations on the namespace:

    Kubectl describe namespace <namespace-name>

    If the annotations on the namespace has the following statement, then the namespace is isolated.

    “opencontrail.kubernetes.isolated” : “true”

        [root@a7s16 ~]# kubectl describe namespace test-isolated-ns
        Name:       test-isolated-ns
        Labels:     <none>
        Annotations:    opencontrail.kubernetes.isolated : true     Namespace is isolated
        Status:     Active

# 10. Testing
## 10.1 Unit tests
## 10.2 Dev tests
## 10.3 System tests

# 11. Documentation Impact

# 12. References
