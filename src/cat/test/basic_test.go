// Package main runs complete integration test involving mock control-node and
// mock agent processes.
package main

import (
	"fmt"
	"testing"
	"time"

	"cat"
	"cat/config"
	"cat/controlnode"
	"cat/crpd"
	"cat/sut"

	log "github.com/sirupsen/logrus"
)

// TestConnectivityWithConfiguration tests various combination of control-nodes,
// agents, and CRPDs. It also injects basic configuration necessary in order to
// add mock vmi in the agent and exchange routing information.
func TestConnectivityWithConfiguration(t *testing.T) {
	tests := []struct {
		desc         string
		controlNodes int
		agents       int
		crpds        int
	}{{
		desc:         "MultipleControlNodeMultipleCRPD",
		controlNodes: 2,
		agents:       0,
		crpds:        2,
	},
		{
			desc:         "MultipleControlNodes",
			controlNodes: 3,
			agents:       0,
			crpds:        0,
		},
		{
			desc:         "SingleControlNodeSingleAgent",
			controlNodes: 1,
			agents:       1,
			crpds:        0,
		}, {
			desc:         "SingleControlNodeMultipleAgent",
			controlNodes: 1,
			agents:       3,
			crpds:        0,
		}, {
			desc:         "MultipleControlNodeSingleAgent",
			controlNodes: 2,
			agents:       1,
			crpds:        0,
		}, {
			desc:         "MultipleControlNodeMultipleAgent",
			controlNodes: 2,
			agents:       3,
			crpds:        2,
		}}

	for _, tt := range tests {
		t.Run(tt.desc, func(t *testing.T) {
			log.Infof("Started test %s", tt.desc)

			// Create basic CAT object first to hold all objects managed.
			c, err := cat.New()
			if err != nil {
				t.Fatalf("Failed to create CAT cect: %v", err)
			}

			// Check whether crpd docker container can be instantiated.
			if !crpd.CanUseCRPD() {
				tt.crpds = 0
			}

			// Bring up control-nodes, agents and crpds.
			if err := setup(c, tt.desc, tt.controlNodes, tt.agents, tt.crpds); err != nil {
				t.Fatalf("Failed to create control-nodes, agents and/or c.CRPDs: %v", err)
			}

			// Verify that control-node bgp router configurations are added.
			if err := verifyControlNodeBgpRoutersConfiguration(c, tt.controlNodes+tt.crpds); err != nil {
				t.Fatalf("Cannot verify bgp routers configuration")
			}

			// Verify that BGP and XMPP connections reach established state.
			if err := verifyControlNodesAndAgents(c); err != nil {
				t.Fatalf("Failed to verify control-nodes, agents and/or c.CRPDs: %v", err)
			}

			// Restart each control-node.
			for _, cn := range c.ControlNodes {
				if err := cn.Restart(); err != nil {
					t.Fatalf("Failed to restart control-nodes: %v", err)
				}
			}

			// Verify again that BGP and XMPP connections reach established
			// state after restart.
			if err := verifyControlNodesAndAgents(c); err != nil {
				t.Fatalf("Failed to verify control-nodes, agents and/or c.CRPDs after restart: %v", err)
			}

			// Disable bgp-router (admin: down) and ensure that session indeed
			// goes down.
			if err := setControlNodeBgpRoutersAdminDown(c, true); err != nil {
				t.Fatalf("Failed to update bgp-routers admin_down to true: %v", err)
			}

			// Since bgp-router objects have been now configured with admin_down
			// as true, verify that sessions do not remain as established.
			if err := verifyControlNodeBgpSessions(c, true); err != nil {
				t.Fatalf("bgp routers remain up after admin down: %v", err)
			}

			// Re-enable bgp-router (admin: down) and ensure that session indeed
			// comes back up.
			if err := setControlNodeBgpRoutersAdminDown(c, false); err != nil {
				t.Fatalf("Failed to update bgp-routers admin_down to false: %v", err)
			}

			// Now that admin_down configuration knob has been flipped back to
			// false, verify that bgp sessions comes back to established state.
			if err := verifyControlNodeBgpSessions(c, false); err != nil {
				t.Fatalf("bgp routers did not come back up after admin up: %v", err)
			}

			// Delete all control-node bgp-routers configuration.
			if err := deleteControlNodeBgpRouters(c); err != nil {
				t.Fatalf("Cannot delete bgp routers from configuration")
			}

			// Verify that control-node bgp router configurations are deleted.
			if err := verifyControlNodeBgpRoutersConfiguration(c, tt.crpds); err != nil {
				t.Fatalf("Cannot verify bgp routers configuration")
			}

			// Cleanup all cat framework objects created so far in this test.
			if err := c.Teardown(); err != nil {
				t.Fatalf("CAT cects cleanup failed: %v", err)
			}
		})
	}
}

// setup as many control-nods, agents and crpds as requested.
func setup(c *cat.CAT, desc string, nc, na, ncrpd int) error {
	log.Debugf("%s: Creating %d control-nodes, %d c.Agents and %d c.CRPDs\n", desc, nc, na, ncrpd)

	ipOctet := 127
	if ncrpd > 0 {
		ipOctet = 10
	}
	for i := 0; i < nc; i++ {
		if _, err := c.AddControlNode(desc, fmt.Sprintf("control-node%d", i+1), fmt.Sprintf("%d.0.0.%d", ipOctet, i+1), fmt.Sprintf("%s/db.json", c.SUT.Manager.RootDir), 0); err != nil {
			return err
		}
	}

	for i := 0; i < ncrpd; i++ {
		if _, err := c.AddCRPD(desc, fmt.Sprintf("crpd%d", i+1)); err != nil {
			return err
		}
	}

	generateConfiguration(c)

	for i := 0; i < na; i++ {
		if _, err := c.AddAgent(desc, fmt.Sprintf("Agent%d", i+1), c.ControlNodes); err != nil {
			return err
		}
	}
	if err := verifyControlNodesAndAgents(c); err != nil {
		return err
	}

	if err := verifyConfiguration(c); err != nil {
		return err
	}

	if err := addVirtualPorts(c); err != nil {
		return err
	}

	return nil
}

// deleteControlNodeBgpRouters deletes all control-node bgp-router objects from
// contrail configuration (mocked) database.
func deleteControlNodeBgpRouters(c *cat.CAT) error {
	for _, cn := range c.ControlNodes {
		if err := cn.ContrailConfig.Delete(&c.FqNameTable, &c.UuidTable); err != nil {
			return fmt.Errorf("bgp-router configuration deletion failed: %v", err)
		}
	}

	// Invoke UpdateConfigDB in order to regenerate database and notify all
	// control-nodes processes.
	if err := controlnode.UpdateConfigDB(c.SUT.Manager.RootDir, c.ControlNodes, &c.FqNameTable, &c.UuidTable); err != nil {
		return err
	}
	return nil
}

func verifyControlNodeBgpRoutersConfiguration(c *cat.CAT, count int) error {
	for _, cn := range c.ControlNodes {
		if err := cn.CheckConfiguration("bgp-router", count, 5, 3); err != nil {
			return fmt.Errorf("bgp-router configuration check failed: %v", err)
		}
	}
	return nil
}

// setControlNodeBgpRoutersAdminDown brings up/down bgp sessions by updating
// BgpRouter::admin_down flag.
func setControlNodeBgpRoutersAdminDown(c *cat.CAT, down bool) error {
	for _, cn := range c.ControlNodes {
		cn.ContrailConfig.BGPRouterParameters.AdminDown = down
		cn.ContrailConfig.IdPerms.LastModified = time.Now().String()
		cn.ContrailConfig.UpdateDB(&c.UuidTable)
	}

	if err := controlnode.UpdateConfigDB(c.SUT.Manager.RootDir, c.ControlNodes, &c.FqNameTable, &c.UuidTable); err != nil {
		return err
	}
	return nil
}

// verifyControlNodeBgpSessions waits for bgp sessions to either come up or
// go down.
func verifyControlNodeBgpSessions(c *cat.CAT, down bool) error {
	cnComponents := []*sut.Component{}

	for _, cn := range c.ControlNodes {
		cnComponents = append(cnComponents, &cn.Component)
	}

	for _, cn := range c.ControlNodes {
		if err := cn.CheckBGPConnections(cnComponents, down, 30, 5); err != nil {
			return err
		}
	}
	return nil
}

// verifyControlNodesAndAgents verifies that bgp and xmpp connections
func verifyControlNodesAndAgents(c *cat.CAT) error {
	for _, cn := range c.ControlNodes {

		// Check all xmpp agent connections to control-nodes.
		if err := cn.CheckXMPPConnections(c.Agents, 30, 1); err != nil {
			return fmt.Errorf("%s to c.Agents xmpp connections are down: %v", cn.Name, err)
		}

		// Check all control-node bgp connections to control-nodes.
		cnComponents := []*sut.Component{}
		for _, cn2 := range c.ControlNodes {
			cnComponents = append(cnComponents, &cn2.Component)
		}
		if err := cn.CheckBGPConnections(cnComponents, false, 30, 5); err != nil {
			return fmt.Errorf("%s to control-nodes bgp connections are down: %v", cn.Name, err)
		}

		// Check all crpd bgp connections to control-nodes.
		crComponents := []*sut.Component{}
		for _, cr := range c.ControlNodes {
			crComponents = append(crComponents, &cr.Component)
		}
		if err := cn.CheckBGPConnections(crComponents, false, 30, 5); err != nil {
			return fmt.Errorf("%s to control-nodes bgp connections are down: %v", cn.Name, err)
		}
	}
	return nil
}

// createVirtualNetwork generates virtual network configuration along with
// its dependent configuration objects such as routing_instance, route_target,
// etc. It also updates refs and children links to ensure that configuration
// dependency is correctly reflected in the configuration database.
func createVirtualNetwork(c *cat.CAT, name, target string, networkIpam *config.ContrailConfig) (*config.VirtualNetwork, *config.RoutingInstance, error) {
	t := fmt.Sprintf("target:%s", target)
	rtarget, err := config.NewConfigObject(&c.FqNameTable, &c.UuidTable, "route_target", t, "", []string{t})
	if err != nil {
		return nil, nil, err
	}
	c.ConfigMap["route_target:"+target] = rtarget

	ri, err := config.NewRoutingInstance(&c.FqNameTable, &c.UuidTable, name)
	if err != nil {
		return nil, nil, err
	}
	c.ConfigMap["routing_instance:"+name] = ri.ContrailConfig
	ri.AddRef(&c.UuidTable, rtarget)

	vn, err := config.NewVirtualNetwork(&c.FqNameTable, &c.UuidTable, name)
	if err != nil {
		return nil, nil, err
	}
	c.ConfigMap["virtual_network:"+name] = vn.ContrailConfig
	vn.AddRef(&c.UuidTable, networkIpam)
	vn.AddChild(&c.UuidTable, ri.ContrailConfig)
	return vn, ri, err
}

func generateConfiguration(c *cat.CAT) error {
	config.NewGlobalSystemsConfig(&c.FqNameTable, &c.UuidTable, "64512")
	vm1, err := config.NewConfigObject(&c.FqNameTable, &c.UuidTable, "virtual_machine", "vm1", "", []string{"vm1"})
	if err != nil {
		return err
	}
	c.ConfigMap["virtual_machine:vm1"] = vm1

	vm2, err := config.NewConfigObject(&c.FqNameTable, &c.UuidTable, "virtual_machine", "vm2", "", []string{"vm2"})
	if err != nil {
		return err
	}
	c.ConfigMap["virtual_machine:vm2"] = vm2

	vm3, err := config.NewConfigObject(&c.FqNameTable, &c.UuidTable, "virtual_machine", "vm3", "", []string{"vm3"})
	if err != nil {
		return err
	}
	c.ConfigMap["virtual_machine:vm3"] = vm3

	vm4, err := config.NewConfigObject(&c.FqNameTable, &c.UuidTable, "virtual_machine", "vm4", "", []string{"vm4"})
	if err != nil {
		return err
	}
	c.ConfigMap["virtual_machine:vm4"] = vm4

	domain, err := config.NewConfigObject(&c.FqNameTable, &c.UuidTable, "domain", "default-domain", "", []string{"default-domain"})
	if err != nil {
		return err
	}
	c.ConfigMap["domain:default-domain"] = domain

	project, err := config.NewConfigObject(&c.FqNameTable, &c.UuidTable, "project", "default-project", "domain:"+domain.UUID, []string{"default-domain", "default-project"})
	if err != nil {
		return err
	}
	c.ConfigMap["project:default-project"] = project

	networkIpam, err := config.NewConfigObject(&c.FqNameTable, &c.UuidTable, "network_ipam", "default-network-ipam", "project:"+project.UUID, []string{"default-domain", "default-project", "default-network-ipam"})
	if err != nil {
		return err
	}
	c.ConfigMap["network_ipam:default-network-ipam"] = networkIpam

	_, _, err = createVirtualNetwork(c, "ip-fabric", "64512:80000000", networkIpam)
	if err != nil {
		return err
	}

	vn1, ri1, err := createVirtualNetwork(c, "vn1", "64512:80000001", networkIpam)
	if err != nil {
		return err
	}

	var bgpRouters []*config.BGPRouter
	ipOctet := 127
	if len(c.CRPDs) > 0 {
		ipOctet = 10
	}
	for i := range c.ControlNodes {
		name := fmt.Sprintf("control-node%d", i+1)
		address := fmt.Sprintf("%d.0.0.%d", ipOctet, i+1)
		bgpRouter, err := config.NewBGPRouter(&c.FqNameTable, &c.UuidTable, name, address, "control-node", c.ControlNodes[i].Config.BGPPort)
		if err != nil {
			return err
		}
		c.ConfigMap["bgp_router:"+name] = bgpRouter.ContrailConfig
		c.ControlNodes[i].ContrailConfig = bgpRouter
		bgpRouters = append(bgpRouters, bgpRouter)
	}

	for i := range c.CRPDs {
		crpd, err := config.NewBGPRouter(&c.FqNameTable, &c.UuidTable, c.CRPDs[i].Name, c.CRPDs[i].IPAddress, "router", 179)
		if err != nil {
			return err
		}
		c.ConfigMap["bgp_router:"+c.CRPDs[i].Name] = crpd.ContrailConfig
		bgpRouters = append(bgpRouters, crpd)
	}

	// Form full mesh ibgp peerings.
	for i := range bgpRouters {
		for j := range bgpRouters {
			if i != j {
				bgpRouters[i].AddRef(&c.UuidTable, bgpRouters[j].ContrailConfig)
			}
		}
	}

	vr1, err := config.NewVirtualRouter(&c.FqNameTable, &c.UuidTable, "Agent1", "1.2.3.1")
	if err != nil {
		return err
	}
	c.ConfigMap["virtual_router:Agent1"] = vr1.ContrailConfig
	vr1.AddRef(&c.UuidTable, vm1)
	vr1.AddRef(&c.UuidTable, vm2)

	vr2, err := config.NewVirtualRouter(&c.FqNameTable, &c.UuidTable, "Agent2", "1.2.3.2")
	if err != nil {
		return err
	}
	c.ConfigMap["virtual_router:Agent2"] = vr2.ContrailConfig
	vr2.AddRef(&c.UuidTable, vm3)
	vr2.AddRef(&c.UuidTable, vm4)

	vmi1, err := config.NewVirtualMachineInterface(&c.FqNameTable, &c.UuidTable, "vmi1")
	if err != nil {
		return err
	}
	c.ConfigMap["virtual_machine_interface:vmi1"] = vmi1.ContrailConfig
	vmi1.AddRef(&c.UuidTable, vm1)
	vmi1.AddRef(&c.UuidTable, vn1.ContrailConfig)
	vmi1.AddRef(&c.UuidTable, ri1.ContrailConfig)

	instanceIp1, err := config.NewInstanceIp(&c.FqNameTable, &c.UuidTable, "ip1", "2.2.2.10", "v4")
	if err != nil {
		return err
	}
	c.ConfigMap["instance_ip:ip1"] = instanceIp1.ContrailConfig
	instanceIp1.AddRef(&c.UuidTable, vn1.ContrailConfig)
	instanceIp1.AddRef(&c.UuidTable, vmi1.ContrailConfig)

	return config.GenerateDB(&c.FqNameTable, &c.UuidTable, fmt.Sprintf("%s/db.json", c.SUT.Manager.RootDir))
}

// verifyConfiguration checks if all configuration elements configured earlier
// as correctly reflected in control-nodes in memory configuration data base.
func verifyConfiguration(c *cat.CAT) error {
	for _, cn := range c.ControlNodes {
		if err := cn.CheckConfiguration("domain", 1, 5, 3); err != nil {
			return fmt.Errorf("domain configuration check failed: %v", err)
		}
		if err := cn.CheckConfiguration("global-system-config", 1, 5, 3); err != nil {
			return fmt.Errorf("global-system-config configuration check failed: %v", err)
		}
		if err := cn.CheckConfiguration("instance-ip", 1, 5, 3); err != nil {
			return fmt.Errorf("instance-ip configuration check failed: %v", err)
		}
		if err := cn.CheckConfiguration("network-ipam", 1, 5, 3); err != nil {
			return fmt.Errorf("network-ipam configuration check failed: %v", err)
		}
		if err := cn.CheckConfiguration("project", 1, 5, 3); err != nil {
			return fmt.Errorf("project configuration check failed: %v", err)
		}
		if err := cn.CheckConfiguration("route-target", 1, 5, 3); err != nil {
			return fmt.Errorf("route-target configuration check failed: %v", err)
		}
		if err := cn.CheckConfiguration("routing-instance", 1, 5, 3); err != nil {
			return fmt.Errorf("routing-instance configuration check failed: %v", err)
		}
		if err := cn.CheckConfiguration("virtual-machine", 4, 5, 3); err != nil {
			return fmt.Errorf("virtual-machine configuration check failed: %v", err)
		}
		if err := cn.CheckConfiguration("virtual-machine-interface", 1, 5, 3); err != nil {
			return fmt.Errorf("virtual-machine-interface configuration check failed: %v", err)
		}
		if err := cn.CheckConfiguration("virtual-network", 2, 5, 3); err != nil {
			return fmt.Errorf("virtual-network configuration check failed: %v", err)
		}
		if err := cn.CheckConfiguration("virtual-network-network-ipam", 1, 5, 3); err != nil {
			return fmt.Errorf("virtual-network-network-ipam configuration check failed: %v", err)
		}
		if err := cn.CheckConfiguration("virtual-router", 1, 5, 3); err != nil {
			return fmt.Errorf("virtual-router configuration check failed: %v", err)
		}
		if err := cn.CheckConfiguration("bgp-router", len(c.ControlNodes)+len(c.CRPDs), 5, 3); err != nil {
			return fmt.Errorf("bgp-router configuration check failed: %v", err)
		}
	}
	return nil
}

func addVirtualPorts(c *cat.CAT) error {
	for i := range c.Agents {
		if err := c.Agents[i].AddVirtualPort(c.ConfigMap["virtual_machine_interface:vmi1"], c.ConfigMap["virtual_machine:vm1"], c.ConfigMap["virtual_network:vn1"], c.ConfigMap["project:default-project"], "1.1.1.10", "90:e2:ff:ff:94:9d", "tap1", 9091); err != nil {
			return err
		}
		// TODO: Add a virtual port to one agent for now.
		break
	}
	return nil
}
