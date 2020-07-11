//Package test_upgrade runs the upgrade test-cases for ZIU

package main

import (
        "fmt"
        "testing"

        "cat"
        "cat/controlnode"
        "cat/sut"

        log "github.com/sirupsen/logrus"
)

//Common Setup/Teardown function for SubTests inside Test.
func setupSubTest(t *testing.T, c *cat.CAT) func(t *testing.T, c *cat.CAT) {
        //Place code for common setup for subtest here
        return func(t *testing.T, c *cat.CAT) {
                //code for common teardown for the subtests
                if err := c.Teardown(); err != nil {
                        t.Fatalf("CAT cects cleanup failed: %v", err)
                }
        }
}

func TestRunZIUTestcase(t *testing.T) {

    err := cat.CheckIfApplicable()
    if err != nil {
        log.Infof("ZIU test-case NOT applicable since svl-artifactory is not reachable \n")
        return
    }

    var ConNodes map[string]interface{}
    ConNodes, _ = cat.GetNumOfControlNodes()
    Releases, _ := cat.GetPreviousReleases()

    size := Releases.Len()
    k := 6
    if k > size {
        k = size
    }

    for i,j:=1,Releases.Front(); i<=size; i,j=i+1,j.Next() {

        if (size-k) >= i {
            continue;
        }
        tests := []struct {
            desc         string
            controlNodes int
            agents       int
            crpds        int
        }{
                    {
                desc:         fmt.Sprintf("Check N-%d to N upgrade", ((k-(i-(size-k)))+1)),
                controlNodes: len(ConNodes),
                agents:       1,
                crpds:        0,
            }}

        for _, tt := range tests {
            t.Run(tt.desc, func(t *testing.T) {
                log.Infof("Started test %s", tt.desc)

                // Create basic CAT object first to hold all objects managed.
                c, err := cat.New()
                if err != nil {
                    t.Fatalf("Failed to create CAT struct: %v", err)
                }

                agent_binary := cat.GetAgentBinary(c, j.Value.(string))
                //control_binary := cat.GetControlBinary(c, j.Value.(string))

                // Bring up control-nodes, agents and crpds.
                if err := setupFromFile(c, tt.desc, tt.controlNodes, tt.agents, agent_binary, tt.crpds, ConNodes); err != nil {
                    _ = c.Teardown()
                    t.Fatalf("Failed to create control-nodes, agents and/or c.CRPDs: %v", err)
                }

		//Add port VMI
		if err := addVirtualPorts(c); err != nil {
			_ = c.Teardown()
			t.Fatalf("Add-virtual-port failed with error: %v", err)
		}

                // Verify that control-node bgp router configurations are added.
                if err := verifyControlNodeBgpRoutersConfiguration(c, tt.controlNodes+tt.crpds); err != nil {
		    _ = c.Teardown()
                    t.Fatalf("BGP Connections are not Established")
                }

                // Verify that introspect page has correct config
                if err := verifyIntrospectGlobalSystemConfig(c, 64512); err != nil {
		    _ = c.Teardown()
                    t.Fatalf("global system config is not applied correctly: %v", err)
                }

                // Verify that there are not xmpp flaps
                if err := verifyIntrospectXmppFlaps(c); err != nil {
		    _ = c.Teardown()
                    t.Fatalf("There are unexpected xmpp flaps: %v", err)
                }

                if err := c.Teardown(); err != nil {
                        t.Fatalf("CAT cects cleanup failed: %v", err)
                }
                return;
            })
        }
    }
}

// setup as many control-nods, agents and crpds as requested.
func setupFromFile(c *cat.CAT, desc string, nc, na int, agent_binary string, ncrpd int, ConNodes map[string]interface{}) error {
        log.Debugf("%s: Creating %d control-nodes, %d c.Agents and %d c.CRPDs\n", desc, nc, na, ncrpd)

        for key, value := range ConNodes {
                v := value.(map[string]interface{})
                if _, err := c.AddControlNode(desc, key, v["address"].(string), controlnode.GetConfFile(), v["port"].(int)); err != nil {
                        return err
                }
        }

        for i := 0; i < ncrpd; i++ {
                if _, err := c.AddCRPD(desc, fmt.Sprintf("crpd%d", i+1)); err != nil {
                        return err
                }
        }

        for i := 0; i < na; i++ {
                if _, err := c.AddAgent(desc, fmt.Sprintf("Agent%d", i+1), agent_binary, c.ControlNodes); err != nil {
                        return err
                }
        }
        if err := verifyControlNodesAndAgents(c); err != nil {
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

func addVirtualPorts(c *cat.CAT) error {
	for i := range c.Agents {
                vmi_id := "f98903fb-7278-11ea-bf6e-02486f9480e0"
                vm_id, vn_id, proj_id, vm_name, err := cat.GetParamsAddPort(vmi_id)
                if err != nil {
                        return err
                }
                if err := c.Agents[i].AddVirtualPort(vmi_id, vm_id, vn_id, proj_id, "1.1.1.10", "90:e2:ff:ff:94:9d", "tap1", "9091", vm_name); err != nil {
                        return err
                }
                if err := c.Agents[i].VerifyIntrospectInterfaceState("tap1", true); err != nil {
                        return err
                }
                break
        }
        return nil
}

// checks global system config in introspect page
func verifyIntrospectGlobalSystemConfig(c *cat.CAT, asn uint32) error {
   for _, cn := range c.ControlNodes {
       if err := cn.CheckGlobalSystemConfig(asn); err != nil {
           log.Infof("check global system config failed for cn %s", cn.Name)
           return err
       }
   }

   return nil
}

// checks for xmpp flaps in introspect page
func verifyIntrospectXmppFlaps(c *cat.CAT) error {
   for _, cn := range c.ControlNodes {
       if err := cn.CheckXmppFlaps(); err != nil {
           log.Infof("check xmpp flaps failed for cn %s", cn.Name)
           return err
       }
   }

   return nil
}
