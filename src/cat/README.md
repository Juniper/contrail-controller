# Control-node + Agent Testing (CAT) Framework

Example test is in controller/src/cat/test/basic_test.go

## Run following commands to execute the tests.

```
cd $SANDBOX
./src/contrail-api-client/generateds/generateDS.py -f -o controller/src/cat/types -g golang-api src/contrail-api-client/schema/ietf-l3vpn-schema.xsd
./src/contrail-api-client/generateds/generateDS.py -f -o controller/src/cat/types -g golang-api src/contrail-api-client/schema/bgp_schema.xsd
./src/contrail-api-client/generateds/generateDS.py -f -o controller/src/cat/types -g golang-api src/contrail-api-client/schema/vnc_cfg.xsd
cd controller/src/cat/test
../../../../third_party/go/bin/go test
```

## Capabilities that has been implemented so far

1. Instantiate and terminate arbitrary number of mock control-nodes
2. Instantiate and terminate arbitrary number of mock agents
2. Instantiate and terminate arbitrary number of CRPDs
4. XMPP peering among control-nodes and agents
5. BGP peering among control-nodes
6. BGP peering among control-nodes and CRPDs
7. Ability to add a mock port/VMI to agent (partial)

## Configuration

Following configuration elements can be added to the mock db file before
launching control-nodes.

1  global-system-config

2  domain

3  project

4  bgp-router

5  virtual-router

6  virtual-network

7  routing-instance

8  virtual-machine

9  virtual-machine-interface

10 network-ipam

11 instance-ip

## Future work to done

1. Add support for configuration updates by mocking rabbitmq messaging
     At present, this can still be done by config reinit (SIGHUP) to control-nodes.
3. Integration with dpdk
4. Integration with a contrail-collector (Analytics) for flows and uves

etc.
