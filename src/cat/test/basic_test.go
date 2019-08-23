package main

import (
    "strconv"
    "testing"
    "cat"
    "cat/agent"
    "cat/controlnode"
    "fmt"
)

var cat_obj *cat.CAT

func setup() error {
    if cat_obj != nil {
        return nil
    }
    var err error
    cat_obj, err = cat.New()
    if err != nil {
        fmt.Printf("%v", err)
    }
    return err
}

func TestSingleControlNodeSingleAgent(t *testing.T) {
    err := setup()
    if err != nil {
        t.Fail()
    }
    createControlNodseAndAgents(t, "TestSingleControlNodeSingleAgent", 1,1)
    cat_obj.Teardown()
}

func TestSingleControlNodeMultipleAgent(t *testing.T) {
    if setup() != nil {
        t.Fail()
    }
    createControlNodseAndAgents(t, "TestSingleControlNodeMultipleAgent",1,3)
    cat_obj.Teardown()
}

func TestMultipleControlNodeSingleAgent(t *testing.T) {
    if setup() != nil {
        t.Fail()
    }
    createControlNodseAndAgents(t, "TestMultipleControlNodeSingleAgent", 2, 1)
    cat_obj.Teardown()
}

func TestMultipleControlNodeMultipleAgent(t *testing.T) {
    if setup() != nil {
        t.Fail()
    }
    createControlNodseAndAgents(t, "TestMultipleControlNodeMultipleAgent", 2, 3)
    cat_obj.Teardown()
}

func TestSingleControlNodeRestart(t *testing.T) {
    if setup() != nil {
        t.Fail()
    }
    control_nodes, agents, _ := createControlNodseAndAgents(t,
        "TestSingleControlNodeRestart", 1, 1)
    control_nodes[0].Restart()
    verifyControlNodseAndAgents(t, control_nodes, agents)
    cat_obj.Teardown()
}

func TestMultipleControlNodeRestart(t *testing.T) {
    if setup() != nil {
        t.Fail()
    }
    control_nodes, agents, _ := createControlNodseAndAgents(
        t, "TestMultipleControlNodeRestart", 2, 3)
    for c := range control_nodes {
        control_nodes[c].Restart()
    }
    verifyControlNodseAndAgents(t, control_nodes, agents)
    cat_obj.Teardown()
}

func createControlNodseAndAgents(t *testing.T, test string, nc,
        na int) ([]*controlnode.ControlNode, []*agent.Agent, error) {
    fmt.Printf("%s: Creating %d control-nodes and %d agents\n", test, nc, na);
    control_nodes := []*controlnode.ControlNode{}
    agents := []*agent.Agent{}

    for c := 0; c < nc; c++ {
        cn, err := cat_obj.AddControlNode(test,
            "control-node" + strconv.Itoa(c), 0)
        if err != nil {
            return control_nodes, agents, err
        }
        control_nodes = append(control_nodes, cn)
    }

    for a := 0; a < na; a++ {
        ag, err := cat_obj.AddAgent(test, "agent1", control_nodes)
        if err != nil {
            return control_nodes, agents, err
        }
        agents = append(agents, ag)
    }

    verifyControlNodseAndAgents(t, control_nodes, agents)
    return control_nodes, agents, nil
}

func verifyControlNodseAndAgents(t *testing.T,
                                 control_nodes []*controlnode.ControlNode,
                                 agents[]*agent.Agent) {
    for c := range control_nodes {
        if !control_nodes[c].CheckXmppConnections(agents, 30, 1) {
            t.Fail()
        }
    }
}
