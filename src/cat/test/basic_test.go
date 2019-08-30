/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

package main

import (
    "cat"
    "cat/agent"
    "cat/config"
    "cat/controlnode"
    log "github.com/sirupsen/logrus"
    "fmt"
    "testing"
)

type ConfigMapType map[string]*config.ContrailConfig

func TestConnectivityWithConfiguration(t *testing.T) {
    tests := []struct{
        desc string
        controlNodes int
        agents int
    }{{
            desc: "MultipleControlNodes",
            controlNodes: 3,
            agents: 0,
     },{
             desc: "SingleControlNodeSingleAgent",
             controlNodes: 1,
             agents: 1,
     },{
             desc: "SingleControlNodeMultipleAgent",
             controlNodes: 1,
             agents: 3,
     },{
             desc: "MultipleControlNodeSingleAgent",
             controlNodes: 2,
             agents: 1,
     },{
            desc: "MultipleControlNodeMultipleAgent",
            controlNodes: 2,
            agents: 3,
    }}

    for _, tt := range tests {
        t.Run(tt.desc, func(t *testing.T){
            obj, err := cat.New()
            if err != nil {
                t.Fatalf("Failed to create CAT object: %v", err)
            }
            var control_nodes []*controlnode.ControlNode
            var agents []*agent.Agent
            control_nodes, agents, err = setup(obj, tt.desc, tt.controlNodes, tt.agents)
            if err != nil {
                t.Fatalf("Failed to create control-nodes and agents: %v", err)
            }

            if err := verifyControlNodesAndAgents(control_nodes, agents); err != nil {
                t.Fatalf("Failed to verify control-nodes and agents: %v", err)
            }

            for i := range control_nodes {
                if err := control_nodes[i].Restart(); err != nil {
                    t.Fatalf("Failed to restart control-nodes: %v", err)
                }
            }
            if err := verifyControlNodesAndAgents(control_nodes, agents); err != nil {
                t.Fatalf("Failed to verify control-nodes and agents after restart: %v", err)
            }
            if err := obj.Teardown(); err != nil {
                t.Fatalf("CAT objects cleanup failed: %v", err)
            }
        })
    }
}

func setup(cat *cat.CAT, desc string, nc, na int) ([]*controlnode.ControlNode, []*agent.Agent, error) {
    log.Debugf("%s: Creating %d control-nodes and %d agents\n", desc, nc, na);
    var configMap ConfigMapType = make(ConfigMapType)
    control_nodes := []*controlnode.ControlNode{}

    for i := 0; i < nc; i++ {
        cn, err := cat.AddControlNode(desc, fmt.Sprintf("control-node%d", i+1), fmt.Sprintf("127.0.0.%d", i+1), fmt.Sprintf("%s/db.json", cat.SUT.Manager.RootDir), 0)
        if err != nil {
            return nil, nil, err
        }
        control_nodes = append(control_nodes, cn)
    }
    generateConfiguration(cat, configMap, control_nodes)

    agents := []*agent.Agent{}
    for i := 0; i < na; i++ {
        ag, err := cat.AddAgent(desc, fmt.Sprintf("Agent%d", i+1), control_nodes)
        if err != nil {
            return nil, nil, err
        }
        agents = append(agents, ag)
    }
    if err := verifyControlNodesAndAgents(control_nodes, agents); err != nil {
        return nil, nil, err
    }

    if err := verifyConfiguration(control_nodes); err != nil {
        return nil, nil, err
    }

    if err := addVirtualPorts(agents, configMap); err != nil {
        return nil, nil, err
    }
    return control_nodes, agents, nil
}

func verifyControlNodesAndAgents(control_nodes []*controlnode.ControlNode, agents[]*agent.Agent) error {
    for i := range control_nodes {
        if err := control_nodes[i].CheckXMPPConnections(agents, 30, 1); err != nil {
            return fmt.Errorf("%s to agents xmpp connections are down: %v", control_nodes[i].Name, err)
        }
        if err := control_nodes[i].CheckBGPConnections(control_nodes, 30, 5); err != nil {
            return fmt.Errorf("%s to control-nodes bgp connections are down: %v", control_nodes[i].Name, err)
        }
    }
    return nil
}

func createVirtualNetwork(fqNameTable *config.FQNameTableType, uuidTable *config.UUIDTableType, configMap ConfigMapType, name, target string, network_ipam *config.ContrailConfig) (*config.VirtualNetwork, *config.RoutingInstance, error) {
    t := fmt.Sprintf("target:%s", target)
    rtarget, err := config.NewConfigObject(fqNameTable, uuidTable, "route_target", t, "", []string{t})
    if err != nil {
        return nil, nil, err
    }
    configMap["route_target:" + target] = rtarget

    ri, err := config.NewRoutingInstance(fqNameTable, uuidTable, name)
    if err != nil {
        return nil, nil, err
    }
    configMap["routing_instance:" + name] = ri.ContrailConfig
    ri.AddRef(uuidTable, rtarget)

    vn, err := config.NewVirtualNetwork(fqNameTable, uuidTable, name)
    if err != nil {
        return nil, nil, err
    }
    configMap["virtual_network:" + name] = vn.ContrailConfig
    vn.AddRef(uuidTable, network_ipam)
    vn.AddChild(uuidTable, ri.ContrailConfig)
    return vn, ri, err
}

func generateConfiguration(cat *cat.CAT, configMap ConfigMapType, control_nodes []*controlnode.ControlNode) error {
    fqNameTable := make(config.FQNameTableType)
    uuidTable := make(config.UUIDTableType)
    config.NewGlobalSystemsConfig(&fqNameTable, &uuidTable, "64512")
    vm1, err := config.NewConfigObject(&fqNameTable, &uuidTable, "virtual_machine", "vm1", "", []string{"vm1"})
    if err != nil {
        return err
    }
    configMap["virtual_machine:vm1"] = vm1

    vm2, err := config.NewConfigObject(&fqNameTable, &uuidTable, "virtual_machine", "vm2", "", []string{"vm2"})
    if err != nil {
        return err
    }
    configMap["virtual_machine:vm2"] = vm2

    vm3, err := config.NewConfigObject(&fqNameTable, &uuidTable, "virtual_machine", "vm3", "", []string{"vm3"})
    if err != nil {
        return err
    }
    configMap["virtual_machine:vm3"] = vm3

    vm4, err := config.NewConfigObject(&fqNameTable, &uuidTable, "virtual_machine", "vm4", "", []string{"vm4"})
    if err != nil {
        return err
    }
    configMap["virtual_machine:vm4"] = vm4

    domain, err := config.NewConfigObject(&fqNameTable, &uuidTable, "domain", "default-domain", "", []string{"default-domain"})
    if err != nil {
        return err
    }
    configMap["domain:default-domain"] = domain

    project, err := config.NewConfigObject(&fqNameTable, &uuidTable, "project", "default-project", "domain:" + domain.UUID, []string{"default-domain", "default-project"})
    if err != nil {
        return err
    }
    configMap["project:default-project"] = project

    network_ipam, err := config.NewConfigObject(&fqNameTable, &uuidTable, "network_ipam", "default-network-ipam", "project:" + project.UUID, []string{"default-domain", "default-project", "default-network-ipam"})
    if err != nil {
        return err
    }
    configMap["network_ipam:default-network-ipam"] = network_ipam

    _, _, err = createVirtualNetwork(&fqNameTable, &uuidTable, configMap, "ip-fabric", "64512:80000000", network_ipam)
    if err != nil {
        return err
    }

    vn1, ri1, err := createVirtualNetwork(&fqNameTable, &uuidTable, configMap, "vn1", "64512:80000001", network_ipam)
    if err != nil {
        return err
    }

    var bgp_routers []*config.BGPRouter
    for i := range control_nodes {
        name := fmt.Sprintf("control-node%d", i+1)
        address := fmt.Sprintf("127.0.0.%d", i+1)
        bgp_router, err := config.NewBGPRouter(&fqNameTable, &uuidTable, name, address, control_nodes[i].Config.BGPPort)
        if err != nil {
            return err
        }
        configMap["bgp_router:" + name] = bgp_router.ContrailConfig
        bgp_routers = append(bgp_routers, bgp_router)
    }

    // Form full mesh ibgp peerings.
    for i := range bgp_routers {
        for j := range bgp_routers {
            if i != j {
                bgp_routers[i].AddRef(&uuidTable, bgp_routers[j].ContrailConfig)
            }
        }
    }

    vr1, err := config.NewVirtualRouter(&fqNameTable, &uuidTable, "Agent1", "1.2.3.1")
    if err != nil {
        return err
    }
    configMap["virtual_router:Agent1"] = vr1.ContrailConfig
    vr1.AddRef(&uuidTable, vm1)
    vr1.AddRef(&uuidTable, vm2)

    vr2, err := config.NewVirtualRouter(&fqNameTable, &uuidTable, "Agent2", "1.2.3.2")
    if err != nil {
        return err
    }
    configMap["virtual_router:Agent2"] = vr2.ContrailConfig
    vr2.AddRef(&uuidTable, vm3)
    vr2.AddRef(&uuidTable, vm4)

    vmi1, err := config.NewVirtualMachineInterface(&fqNameTable, &uuidTable, "vmi1")
    if err != nil {
        return err
    }
    configMap["virtual_machine_interface:vmi1"] = vmi1.ContrailConfig
    vmi1.AddRef(&uuidTable, vm1)
    vmi1.AddRef(&uuidTable, vn1.ContrailConfig)
    vmi1.AddRef(&uuidTable, ri1.ContrailConfig)

    instance_ip1, err := config.NewInstanceIp(&fqNameTable, &uuidTable, "ip1", "2.2.2.10", "v4")
    if err != nil {
        return err
    }
    configMap["instance_ip:ip1"] = instance_ip1.ContrailConfig
    instance_ip1.AddRef(&uuidTable, vn1.ContrailConfig)
    instance_ip1.AddRef(&uuidTable, vmi1.ContrailConfig)

    return config.GenerateDB(&fqNameTable, &uuidTable, fmt.Sprintf("%s/db.json", cat.SUT.Manager.RootDir))
}

func verifyConfiguration(control_nodes []*controlnode.ControlNode) error {
    for c := range control_nodes {
        if err := control_nodes[c].CheckConfiguration("domain", 1, 3, 3); err != nil {
            return fmt.Errorf("domain configuration check failed: %v", err)
        }
        if err := control_nodes[c].CheckConfiguration("global-system-config", 1, 3, 3); err != nil {
            return fmt.Errorf("global-system-config configuration check failed: %v", err)
        }
        if err := control_nodes[c].CheckConfiguration("instance-ip", 1, 3, 3); err != nil {
            return fmt.Errorf("instance-ip configuration check failed: %v", err)
        }
        if err := control_nodes[c].CheckConfiguration("network-ipam", 1, 3, 3); err != nil {
            return fmt.Errorf("network-ipam configuration check failed: %v", err)
        }
        if err := control_nodes[c].CheckConfiguration("project", 1, 3, 3); err != nil {
            return fmt.Errorf("project configuration check failed: %v", err)
        }
        if err := control_nodes[c].CheckConfiguration("route-target", 1, 3, 3); err != nil {
            return fmt.Errorf("route-target configuration check failed: %v", err)
        }
        if err := control_nodes[c].CheckConfiguration("routing-instance", 1, 3, 3); err != nil {
            return fmt.Errorf("routing-instance configuration check failed: %v", err)
        }
        if err := control_nodes[c].CheckConfiguration("virtual-machine", 4, 3, 3); err != nil {
            return fmt.Errorf("virtual-machine configuration check failed: %v", err)
        }
        if err := control_nodes[c].CheckConfiguration("virtual-machine-interface", 1, 3, 3); err != nil {
            return fmt.Errorf("virtual-machine-interface configuration check failed: %v", err)
        }
        if err := control_nodes[c].CheckConfiguration("virtual-network", 2, 3, 3); err != nil {
            return fmt.Errorf("virtual-network configuration check failed: %v", err)
        }
        if err := control_nodes[c].CheckConfiguration("virtual-network-network-ipam", 1, 3, 3); err != nil {
            return fmt.Errorf("virtual-network-network-ipam configuration check failed: %v", err)
        }
        if err := control_nodes[c].CheckConfiguration("virtual-router", 1, 3, 3); err != nil {
            return fmt.Errorf("virtual-router configuration check failed: %v", err)
        }
        if err := control_nodes[c].CheckConfiguration("bgp-router", len(control_nodes), 3, 3); err != nil {
            return fmt.Errorf("virtual-router configuration check failed: %v", err)
        }
    }
    return nil
}

func addVirtualPorts (agents[]*agent.Agent, configMap ConfigMapType) error {
    for i := range agents {
        if err := agents[i].AddVirtualPort(configMap["virtual_machine_interface:vmi1"], configMap["virtual_machine:vm1"], configMap["virtual_network:vn1"], configMap["project:default-project"], "1.1.1.10", "90:e2:ff:ff:94:9d", "tap1"); err != nil {
            return err
        }
        // TODO: Add a virtual port to one agent for now.
        break
    }
    return nil
}
