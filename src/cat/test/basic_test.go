/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

package main

import (
    "cat"
    "cat/agent"
    "cat/config"
    "cat/controlnode"
    "log"
    "fmt"
    "testing"
)

type ConfigMap map[string]*config.ContrailConfigObject

func TestConnectivityWithConfiguration(t *testing.T) {
    tests := []struct{
        desc string
        controlNodes int
        agents int
    }{
        {
            desc: "MultipleControlNodes",
            controlNodes: 3,
            agents: 0,
        },
        {
            desc: "SingleControlNodeSingleAgent",
            controlNodes: 1,
            agents: 1,
        },
        {
            desc: "SingleControlNodeMultipleAgent",
            controlNodes: 1,
            agents: 3,
        },
        {
            desc: "MultipleControlNodeSingleAgent",
            controlNodes: 2,
            agents: 1,
        },
        {
            desc: "MultipleControlNodeMultipleAgent",
            controlNodes: 2,
            agents: 3,
        },
    }

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
            // obj.PauseAfterRun = true
            obj.Teardown()
        })
    }
}

func setup(cat *cat.CAT, desc string, nc, na int) ([]*controlnode.ControlNode, []*agent.Agent, error) {
    log.Printf("%s: Creating %d control-nodes and %d agents\n", desc, nc, na);
    var configMap ConfigMap = make(ConfigMap)
    control_nodes := []*controlnode.ControlNode{}

    for i := 0; i < nc; i++ {
        cn, err := cat.AddControlNode(desc, fmt.Sprintf("control-node%d", i+1), fmt.Sprintf("127.0.0.%d", i+1), fmt.Sprintf("%s/db.json", cat.Sut.Manager.RootDir), 0)
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
        if err := control_nodes[i].CheckXmppConnections(agents, 30, 1); err != nil {
            return fmt.Errorf("%s to agents xmpp connections are down: %v", control_nodes[i].Name, err)
        }
        if err := control_nodes[i].CheckBgpConnections(control_nodes, 30, 5); err != nil {
            return fmt.Errorf("%s to control-nodes bgp connections are down: %v", control_nodes[i].Name, err)
        }
    }
    return nil
}

func createVirtualNetwork(configMap ConfigMap, name, target string, network_ipam *config.ContrailConfigObject) (*config.VirtualNetwork, *config.RoutingInstance, error) {
    t := fmt.Sprintf("target:%s", target)
    rtarget, err := config.NewConfigObject("route_target", t, "", []string{t})
    if err != nil {
        return nil, nil, err
    }
    configMap["route_target:" + target] = rtarget

    ri, err := config.NewRoutingInstance(name)
    if err != nil {
        return nil, nil, err
    }
    configMap["routing_instance:" + name] = ri.ContrailConfigObject
    ri.AddRef(rtarget)

    vn, err := config.NewVirtualNetwork(name)
    if err != nil {
        return nil, nil, err
    }
    configMap["virtual_network:" + name] = vn.ContrailConfigObject
    vn.AddRef(network_ipam)
    vn.AddChild(ri.ContrailConfigObject)
    return vn, ri, err
}

func generateConfiguration(cat *cat.CAT, configMap ConfigMap, control_nodes []*controlnode.ControlNode) error {
    config.Initialize()
    config.NewGlobalSystemsConfig("64512")
    vm1, err := config.NewConfigObject("virtual_machine", "vm1", "", []string{"vm1"})
    if err != nil {
        return err
    }
    configMap["virtual_machine:vm1"] = vm1

    vm2, err := config.NewConfigObject("virtual_machine", "vm2", "", []string{"vm2"})
    if err != nil {
        return err
    }
    configMap["virtual_machine:vm2"] = vm2

    vm3, err := config.NewConfigObject("virtual_machine", "vm3", "", []string{"vm3"})
    if err != nil {
        return err
    }
    configMap["virtual_machine:vm3"] = vm3

    vm4, err := config.NewConfigObject("virtual_machine", "vm4", "", []string{"vm4"})
    if err != nil {
        return err
    }
    configMap["virtual_machine:vm4"] = vm4

    domain, err := config.NewConfigObject("domain", "default-domain", "", []string{"default-domain"})
    if err != nil {
        return err
    }
    configMap["domain:default-domain"] = domain

    project, err := config.NewConfigObject("project", "default-project", "domain:" + domain.Uuid, []string{"default-domain", "default-project"})
    if err != nil {
        return err
    }
    configMap["project:default-project"] = project

    network_ipam, err := config.NewConfigObject("network_ipam", "default-network-ipam", "project:" + project.Uuid, []string{"default-domain", "default-project", "default-network-ipam"})
    if err != nil {
        return err
    }
    configMap["network_ipam:default-network-ipam"] = network_ipam

    _, _, err = createVirtualNetwork(configMap, "ip-fabric", "64512:80000000", network_ipam)
    if err != nil {
        return err
    }

    vn1, ri1, err := createVirtualNetwork(configMap, "vn1", "64512:80000001", network_ipam)
    if err != nil {
        return err
    }

    var bgp_routers []*config.BgpRouter
    for i := range control_nodes {
        name := fmt.Sprintf("control-node%d", i+1)
        address := fmt.Sprintf("127.0.0.%d", i+1)
        bgp_router, err := config.NewBgpRouter(name, address, control_nodes[i].Config.BGPPort)
        if err != nil {
            return err
        }
        configMap["bgp_router:" + name] = bgp_router.ContrailConfigObject
        bgp_routers = append(bgp_routers, bgp_router)
    }

    // Form full mesh ibgp peerings.
    for i := range bgp_routers {
        for j := range bgp_routers {
            if i != j {
                bgp_routers[i].AddRef(bgp_routers[j].ContrailConfigObject)
            }
        }
    }

    vr1, err := config.NewVirtualRouter("Agent1", "1.2.3.1")
    if err != nil {
        return err
    }
    configMap["virtual_router:Agent1"] = vr1.ContrailConfigObject
    vr1.AddRef(vm1)
    vr1.AddRef(vm2)

    vr2, err := config.NewVirtualRouter("Agent2", "1.2.3.2")
    if err != nil {
        return err
    }
    configMap["virtual_router:Agent2"] = vr2.ContrailConfigObject
    vr2.AddRef(vm3)
    vr2.AddRef(vm4)

    vmi1, err := config.NewVirtualMachineInterface("vmi1")
    if err != nil {
        return err
    }
    configMap["virtual_machine_interface:vmi1"] = vmi1.ContrailConfigObject
    vmi1.AddRef(vm1)
    vmi1.AddRef(vn1.ContrailConfigObject)
    vmi1.AddRef(ri1.ContrailConfigObject)

    instance_ip1, err := config.NewInstanceIp("ip1", "2.2.2.10", "v4")
    if err != nil {
        return err
    }
    configMap["instance_ip:ip1"] = instance_ip1.ContrailConfigObject
    instance_ip1.AddRef(vn1.ContrailConfigObject)
    instance_ip1.AddRef(vmi1.ContrailConfigObject)

    return config.GenerateDB(fmt.Sprintf("%s/db.json", cat.Sut.Manager.RootDir))
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

func addVirtualPorts (agents[]*agent.Agent, configMap ConfigMap) error {
    for i := range agents {
        if err := agents[i].AddVirtualPort(configMap["virtual_machine_interface:vmi1"], configMap["virtual_machine:vm1"], configMap["virtual_network:vn1"], configMap["project:default-project"], "1.1.1.10", "90:e2:ff:ff:94:9d", "tap1"); err != nil {
            return err
        }
        // TODO: Add a virtual port to one agent for now.
        break
    }
    return nil
}
