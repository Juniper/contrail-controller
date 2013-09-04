This directory contains the code that manipulates the IF-MAP database in the control-node and in the agent. IF-MAP is used to convey the
configuration state of the network as a graph describing entities (Identifiers in IF-MAP speak) and their relationships.

The control-node listens to a single IF-MAP server and uses that information to populate its database. It then selectivly updates
the compute-node agents that are connected to it by sending them only the subset of the IF-MAP graph that these agents need.
IF-MAP information is encoded in the same XMPP channel used to send routing information.

Agents subscribe to information that is relevant to the "virtual-machines" that are executing on that specific compute-node.
The IFMapExporter class running in the control-node is then responsible to determine the subgraph of information that needs to be
transmitted to that agent. That is achived by the IFMapGraphWalker class. Just as with the BGP engine, entries are first enqueued
for transmission and then encoded when the receiver is ready to receive the message.
