/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

package main

import (
    "cat"
    "cat/agent"
    "cat/config"
    "cat/controlnode"
    "cat/crpd"
    "cat/sut"
    log "github.com/sirupsen/logrus"
    "fmt"
    "time"
    "testing"
)

type ConfigMapType map[string]*config.ContrailConfig

func TestConnectivityWithConfiguration(t *testing.T) {
    tests := []struct{
        desc string
        controlNodes int
        agents int
        crpds int
    }{{
            desc: "SingleControlNodeMultipleCRPD",
            controlNodes: 1,
            agents: 0,
            crpds: 2,
     },
     {
            desc: "MultipleControlNodes",
             controlNodes: 3,
             agents: 0,
             crpds: 0,
     },
     {
             desc: "SingleControlNodeSingleAgent",
             controlNodes: 1,
             agents: 1,
             crpds: 0,
     },{
             desc: "SingleControlNodeMultipleAgent",
             controlNodes: 1,
             agents: 3,
             crpds: 0,
     },{
             desc: "MultipleControlNodeSingleAgent",
             controlNodes: 2,
             agents: 1,
             crpds: 0,
     },{
            desc: "MultipleControlNodeMultipleAgent",
            controlNodes: 2,
            agents: 3,
            crpds: 2,
    }}

    for _, tt := range tests {
        t.Run(tt.desc, func(t *testing.T){
            log.Infof("Started test %s", tt.desc)
            obj, err := cat.New()
            if err != nil {
                t.Fatalf("Failed to create CAT object: %v", err)
            }
            var control_nodes []*controlnode.ControlNode
            var agents []*agent.Agent
            var crpds []*crpd.CRPD
            if (!crpd.CanUseCRPD()) {
                tt.crpds = 0
            }

            fqNameTable := config.FQNameTableType{}
            uuidTable := config.UUIDTableType{}
            control_nodes, agents, crpds, err = setup(obj, tt.desc, tt.controlNodes, tt.agents, tt.crpds, &fqNameTable, &uuidTable)
            if err != nil {
                t.Fatalf("Failed to create control-nodes, agents and/or crpds: %v", err)
            }

            if err := verifyControlNodesAndAgents(control_nodes, agents, crpds); err != nil {
                t.Fatalf("Failed to verify control-nodes, agents and/or crpds: %v", err)
            }

            for i := range control_nodes {
                if err := control_nodes[i].Restart(); err != nil {
                    t.Fatalf("Failed to restart control-nodes: %v", err)
                }
            }
            if err := verifyControlNodesAndAgents(control_nodes, agents, crpds); err != nil {
                t.Fatalf("Failed to verify control-nodes, agents and/or crpds after restart: %v", err)
            }

            // Disable bgp-router (admin: down) and ensure that session indeed
            // goes down.
            if err := setControlNodeBgpRoutersAdminDown(obj, control_nodes, &fqNameTable, &uuidTable, true); err != nil {
                t.Fatalf("Failed to update bgp-routers admin_down to true: %v", err)
            }

            if err := verifyControlNodeBgpSessions(control_nodes, true); err != nil {
                t.Fatalf("bgp routers remain up after admin down: %v", err)
            }

            // Re-enable bgp-router (admin: down) and ensure that session indeed
            // comes back up.
            if err := setControlNodeBgpRoutersAdminDown(obj, control_nodes, &fqNameTable, &uuidTable, false); err != nil {
                t.Fatalf("Failed to update bgp-routers admin_down to false: %v", err)
            }

            if err := verifyControlNodeBgpSessions(control_nodes, false); err != nil {
                t.Fatalf("bgp routers did not come back up after admin up: %v", err)
            }

            // Delete control-node bgp-routers and verify.
            if err := deleteControlNodeBgpRouters(obj, control_nodes, &fqNameTable, &uuidTable); err != nil {
                t.Fatalf("Cannot delete bgp routers from configuration")
            }

            if err := verifyControlNodeBgpRoutersConfiguration(control_nodes, len(crpds)); err != nil {
                t.Fatalf("Cannot verifye bgp routers configuration")
            }

            if err := obj.Teardown(); err != nil {
                t.Fatalf("CAT objects cleanup failed: %v", err)
            }
        })
    }
}

func deleteControlNodeBgpRouters(cat *cat.CAT, control_nodes []*controlnode.ControlNode, fqNameTable *config.FQNameTableType, uuidTable *config.UUIDTableType) error {
    for i := range control_nodes {
        if err := control_nodes[i].ContrailConfig.Delete(fqNameTable, uuidTable); err != nil {
            return fmt.Errorf("bgp-router configuration deletion failed: %v", err)
        }
    }

    if err := controlnode.UpdateConfigDB(cat.SUT.Manager.RootDir, control_nodes, fqNameTable, uuidTable); err != nil {
        return err
    }
    return nil
}

func verifyControlNodeBgpRoutersConfiguration(control_nodes []*controlnode.ControlNode, count int) error {
    for i := range control_nodes {
        if err := control_nodes[i].CheckConfiguration("bgp-router", count, 3, 3); err != nil {
            return fmt.Errorf("bgp-router configuration check failed: %v", err)
        }
    }
    return nil
}

func setControlNodeBgpRoutersAdminDown(cat *cat.CAT, control_nodes []*controlnode.ControlNode, fqNameTable *config.FQNameTableType, uuidTable *config.UUIDTableType, down bool) error {
    for i := range control_nodes {
        control_nodes[i].ContrailConfig.BGPRouterParameters.AdminDown = down
        control_nodes[i].ContrailConfig.IdPerms.LastModified = time.Now().String()
        control_nodes[i].ContrailConfig.UpdateDB(uuidTable)
    }

    if err := controlnode.UpdateConfigDB(cat.SUT.Manager.RootDir, control_nodes, fqNameTable, uuidTable); err != nil {
        return err
    }
    return nil
}

func verifyControlNodeBgpSessions(control_nodes []*controlnode.ControlNode, down bool) error {
    cn_components := []*sut.Component{}
    for i := 0; i < len(control_nodes); i++ {
        cn_components = append(cn_components, &control_nodes[i].Component)
    }

    for i := range control_nodes {
        if err := control_nodes[i].CheckBGPConnections(cn_components, down, 30, 5); err != nil {
            return err
        }
    }
    return nil
}

func setup(cat *cat.CAT, desc string, nc, na, ncrpd int, fqNameTable *config.FQNameTableType, uuidTable *config.UUIDTableType) ([]*controlnode.ControlNode, []*agent.Agent, []*crpd.CRPD, error) {
    log.Debugf("%s: Creating %d control-nodes, %d agents and %d crpds\n", desc, nc, na, ncrpd)
    configMap := ConfigMapType{}
    control_nodes := []*controlnode.ControlNode{}

    ip_octet := 10
    if ncrpd > 0 {
        ip_octet = 10
    }
    for i := 0; i < nc; i++ {
        cn, err := cat.AddControlNode(desc, fmt.Sprintf("control-node%d", i+1), fmt.Sprintf("%d.0.0.%d", ip_octet, i+1), fmt.Sprintf("%s/db.json", cat.SUT.Manager.RootDir), 0)
        if err != nil {
            return nil, nil, nil, err
        }
        control_nodes = append(control_nodes, cn)
    }

    crpds := []*crpd.CRPD{}
    for i := 0; i < ncrpd; i++ {
        cr, err := cat.AddCRPD(desc, fmt.Sprintf("crpd%d", i+1))
        if err != nil {
            return nil, nil, nil, err
        }
        crpds = append(crpds, cr)
    }

    generateConfiguration(cat, configMap, control_nodes, crpds, fqNameTable, uuidTable)

    agents := []*agent.Agent{}
    for i := 0; i < na; i++ {
        ag, err := cat.AddAgent(desc, fmt.Sprintf("Agent%d", i+1), control_nodes)
        if err != nil {
            return nil, nil, nil, err
        }
        agents = append(agents, ag)
    }
    if err := verifyControlNodesAndAgents(control_nodes, agents, crpds); err != nil {
        return nil, nil, nil, err
    }

    if err := verifyConfiguration(control_nodes, crpds); err != nil {
        return nil, nil, nil, err
    }

    if err := addVirtualPorts(agents, configMap); err != nil {
        return nil, nil, nil, err
    }

    return control_nodes, agents, crpds, nil
}

func verifyControlNodesAndAgents(control_nodes []*controlnode.ControlNode, agents[]*agent.Agent, crpds []*crpd.CRPD) error {
    for i := range control_nodes {
        if err := control_nodes[i].CheckXMPPConnections(agents, 30, 1); err != nil {
            return fmt.Errorf("%s to agents xmpp connections are down: %v", control_nodes[i].Name, err)
        }

        cn_components := []*sut.Component{}
        for i := 0; i < len(control_nodes); i++ {
            cn_components = append(cn_components, &control_nodes[i].Component)
        }
        if err := control_nodes[i].CheckBGPConnections(cn_components, false, 30, 5); err != nil {
            return fmt.Errorf("%s to control-nodes bgp connections are down: %v", control_nodes[i].Name, err)
        }

        cr_components := []*sut.Component{}
        for i := 0; i < len(crpds); i++ {
            cr_components = append(cr_components, &crpds[i].Component)
        }
        if err := control_nodes[i].CheckBGPConnections(cr_components, false, 30, 5); err != nil {
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

func generateConfiguration(cat *cat.CAT, configMap ConfigMapType, control_nodes []*controlnode.ControlNode, crpds []*crpd.CRPD, fqNameTable *config.FQNameTableType, uuidTable *config.UUIDTableType) error {
    config.NewGlobalSystemsConfig(fqNameTable, uuidTable, "64512")
    vm1, err := config.NewConfigObject(fqNameTable, uuidTable, "virtual_machine", "vm1", "", []string{"vm1"})
    if err != nil {
        return err
    }
    configMap["virtual_machine:vm1"] = vm1

    vm2, err := config.NewConfigObject(fqNameTable, uuidTable, "virtual_machine", "vm2", "", []string{"vm2"})
    if err != nil {
        return err
    }
    configMap["virtual_machine:vm2"] = vm2

    vm3, err := config.NewConfigObject(fqNameTable, uuidTable, "virtual_machine", "vm3", "", []string{"vm3"})
    if err != nil {
        return err
    }
    configMap["virtual_machine:vm3"] = vm3

    vm4, err := config.NewConfigObject(fqNameTable, uuidTable, "virtual_machine", "vm4", "", []string{"vm4"})
    if err != nil {
        return err
    }
    configMap["virtual_machine:vm4"] = vm4

    domain, err := config.NewConfigObject(fqNameTable, uuidTable, "domain", "default-domain", "", []string{"default-domain"})
    if err != nil {
        return err
    }
    configMap["domain:default-domain"] = domain

    project, err := config.NewConfigObject(fqNameTable, uuidTable, "project", "default-project", "domain:" + domain.UUID, []string{"default-domain", "default-project"})
    if err != nil {
        return err
    }
    configMap["project:default-project"] = project

    network_ipam, err := config.NewConfigObject(fqNameTable, uuidTable, "network_ipam", "default-network-ipam", "project:" + project.UUID, []string{"default-domain", "default-project", "default-network-ipam"})
    if err != nil {
        return err
    }
    configMap["network_ipam:default-network-ipam"] = network_ipam

    _, _, err = createVirtualNetwork(fqNameTable, uuidTable, configMap, "ip-fabric", "64512:80000000", network_ipam)
    if err != nil {
        return err
    }

    vn1, ri1, err := createVirtualNetwork(fqNameTable, uuidTable, configMap, "vn1", "64512:80000001", network_ipam)
    if err != nil {
        return err
    }

    var bgp_routers []*config.BGPRouter
    ip_octet := 10
    if len(crpds) > 0 {
        ip_octet = 10
    }
    for i := range control_nodes {
        name := fmt.Sprintf("control-node%d", i+1)
        address := fmt.Sprintf("%d.0.0.%d", ip_octet, i+1)
        bgp_router, err := config.NewBGPRouter(fqNameTable, uuidTable, name, address, "control-node", control_nodes[i].Config.BGPPort)
        if err != nil {
            return err
        }
        configMap["bgp_router:" + name] = bgp_router.ContrailConfig
        control_nodes[i].ContrailConfig = bgp_router
        bgp_routers = append(bgp_routers, bgp_router)
    }

    for i := range crpds {
        crpd, err := config.NewBGPRouter(fqNameTable, uuidTable, crpds[i].Name, crpds[i].IPAddress, "router", 179)
        if err != nil {
            return err
        }
        configMap["bgp_router:" + crpds[i].Name] = crpd.ContrailConfig
        bgp_routers = append(bgp_routers, crpd)
    }

    // Form full mesh ibgp peerings.
    for i := range bgp_routers {
        for j := range bgp_routers {
            if i != j {
                bgp_routers[i].AddRef(uuidTable, bgp_routers[j].ContrailConfig)
            }
        }
    }

    vr1, err := config.NewVirtualRouter(fqNameTable, uuidTable, "Agent1", "1.2.3.1")
    if err != nil {
        return err
    }
    configMap["virtual_router:Agent1"] = vr1.ContrailConfig
    vr1.AddRef(uuidTable, vm1)
    vr1.AddRef(uuidTable, vm2)

    vr2, err := config.NewVirtualRouter(fqNameTable, uuidTable, "Agent2", "1.2.3.2")
    if err != nil {
        return err
    }
    configMap["virtual_router:Agent2"] = vr2.ContrailConfig
    vr2.AddRef(uuidTable, vm3)
    vr2.AddRef(uuidTable, vm4)

    vmi1, err := config.NewVirtualMachineInterface(fqNameTable, uuidTable, "vmi1")
    if err != nil {
        return err
    }
    configMap["virtual_machine_interface:vmi1"] = vmi1.ContrailConfig
    vmi1.AddRef(uuidTable, vm1)
    vmi1.AddRef(uuidTable, vn1.ContrailConfig)
    vmi1.AddRef(uuidTable, ri1.ContrailConfig)

    instance_ip1, err := config.NewInstanceIp(fqNameTable, uuidTable, "ip1", "2.2.2.10", "v4")
    if err != nil {
        return err
    }
    configMap["instance_ip:ip1"] = instance_ip1.ContrailConfig
    instance_ip1.AddRef(uuidTable, vn1.ContrailConfig)
    instance_ip1.AddRef(uuidTable, vmi1.ContrailConfig)

    return config.GenerateDB(fqNameTable, uuidTable, fmt.Sprintf("%s/db.json", cat.SUT.Manager.RootDir))
}

func verifyConfiguration(control_nodes []*controlnode.ControlNode, crpds []*crpd.CRPD) error {
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
        if err := control_nodes[c].CheckConfiguration("bgp-router", len(control_nodes) + len(crpds), 3, 3); err != nil {
            return fmt.Errorf("bgp-router configuration check failed: %v", err)
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
