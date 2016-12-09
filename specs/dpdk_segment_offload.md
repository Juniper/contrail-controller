<h1> 1. Introduction </h1>
TCP based workloads greatly benefit when segmentation/reassembly of TCP segments are done by the lower most layers like MAC layer or the NIC itself. In case of Tx, the application can send large sized TCP segments of upto 64K to the TCP stack, which transparently passes them to the lowers layers of the stack. The segmentation is generally done in the MAC layer, which adds the remaining headers like IP/TCP to each MSS sized payload and sends it to the NIC. In some cases NIC itself can do all these instead of the MAC layer. The overhead of header addition and packet processing for these smaller MSS sized packets by all layers of TCP/IP stack is avoided and there-by boosts the performance. Likewise, for Rx, the NIC or MAC layer can assemble the smaller MSS sized segments and form a larger sized segment before handing it over to the IP layer there-by avoiding processing of headers at each layer of the networking stack.

Terminology wise, when the NIC does the segmentation, it is called TCP send offload (TSO)/TCP receive offload (TRO) and when segmentation is done in software, it is called Generic send offload (GSO)/Generic Receive offload (GRO).

In case of virtualized environments, offload further improves VMExits which result due to the avoidance of large number of packet exchanges between the host and guest.

<h1>2. Problem Statement </h1>
Vrouter performs Tx and Rx TCP segmentation offload in the kernel mode currently. It largely leverages kernel APIs for achieving this. However, wrt DPDK, the library lacks segmentation support/API’s and hence DPDK based vrouter cannot do the offloads. For achieving line rate performance of TCP based workloads on 10G NIC’s, we need DPDK based vrouter to support offloads.

<h1>3. Proposed Solution </h1>
The proposed solution is illustrated in the diagram below except that the actual segmentation is done in dpdk-vrouter instead of the NIC.

<h2>3.1 Alternatives considered</h2>
None
<h2>3.2 API schema changes</h2>
None
<h2>3.3 User workflow impact</h2>
None
<h2>3.4 UI changes</h2>
None
<h2>3.5 Notification impact</h2>
None

<h2>3.6 Block diagram</h2>
![Image of Segmentation](http://image.slidesharecdn.com/20140928gsoeurobsdcon2014-150111071210-conversion-gate01/95/software-segmentation-offloading-for-freebsd-by-stefano-garzarella-5-638.jpg?cb=1420982015)

<h1>4. Implementation </h1>
<h2>4.1 Work-items for GSO </h2>

1.  The GSO feature needs to be advertised to the guest. This enables Tx offload by default in the guest.
2.  The vrouter lcore on the host which listening on the guest's virtio ring dequeues the packet and gets information about the MSS.
3.  The vrouter lcore needs to construct a chained MBUF with each element in the chain having a size of MSS bytes and sends it to another lcore based on the hash using it's corresponding DPDK ring.
4.  The destination lcore needs to dequeue the packet from the dpdk ring and then add and adjust all the headers to each element in the chained mbuf. Basically it performs the GSO functionality.
5.  Need to support IPv4 and IPv6 headers formats.
6.  Need to support UDP and VxLAN encapsulations.

<h2>4.2 Work-items for GRO </h2>

1.  GRO feature needs mergeable buffers and the same needs to be implemented and advertised to the guest. The guest then can receive 64K sized packets.
2.  The core logic of GRO is an adaptation of Free-BSD LRO implementation.
3.  A per-core hash table needs to be implemented which stores the mbufs received from the fabric.  
    * IP fragmentation of TCP packets is not handled. It is assumed that MSS is adjusted such that IP fragmentation is avoided.
4.  When a packet is received by an lcore, the core GRO logic needs to be implemented.
5.  Need to support IPv4 and IPv6 header formats
6.  Need to support UDP and VxLAN encapsulations

<h1>5. Performance and scaling impact</h1>
Coming soon..

<h1>6. Upgrade</h1>
Upgrade is seamless. No impact.

<h1>7. Deprecations</h1>
None

<h1>8. Dependencies</h1>
None

<h1>9. Testing</h1>
<h2>9.1 Unit tests</h2>
Coming soon..
<h2>9.2 Dev tests</h2>
Coming soon..
<h2>9.3 System tests</h2>
Coming soon..

<h1>10. Documentation Impact</h1>
None

<h1>11. References</h1>
Coming soon..

