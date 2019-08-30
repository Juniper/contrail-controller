/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

package cat

import (
    "errors"
    "fmt"
    "log"
    "os"
    "os/exec"
    "path/filepath"
    "strings"
    "syscall"
    "time"

    "cat/agent"
    "cat/controlnode"
    "cat/sut"
)

// CAT is a contrail automated test.
type CAT struct {
    Sut           sut.Component
    PauseAfterRun bool

    ControlNodes []*controlnode.ControlNode
    Agents       []*agent.Agent
}

// Timestamp format for logfiles.
const timestamp = "20060102_150405"

// New creates an initialized CAT instance.
func New() (*CAT, error) {
    c := &CAT{}
    now := time.Now()

    cwd, err := os.Getwd()
    if err != nil {
        return nil, fmt.Errorf("Cannot find present working directory: %v", err)
    }
    c.Sut.Manager.RootDir = filepath.Join(cwd + "../../../../build/debug/cat", now.Format(timestamp))
    if err := os.MkdirAll(c.Sut.Manager.RootDir, 0700); err != nil {
        return nil, fmt.Errorf("failed to create rootdir %q :%v", c.Sut.Manager.RootDir, err)
    }
    c.Sut.Manager.ReportDir = filepath.Join(c.Sut.Manager.RootDir, "reports")
    err = os.MkdirAll(c.Sut.Manager.ReportDir, 0700)
    if err != nil {
        return nil, fmt.Errorf("failed to make report directory: %v", err)
    }
    c.setHostIP()
    log.Printf("Test data in %s", c.Sut.Manager.RootDir)
    return c, err
}

// Teardown stops all components and closes down the CAT instance.
func (c *CAT) Teardown() {
    if c.PauseAfterRun {
        c.Pause()
    }
    for _, cn := range c.ControlNodes {
        cn.Component.Stop(syscall.SIGTERM)
        os.Remove(cn.ConfDir)
        os.Remove(cn.LogDir)
    }
    for _, a := range c.Agents {
        a.Component.Stop(syscall.SIGTERM)
        os.Remove(a.ConfDir)
        os.Remove(a.LogDir)
    }
}

func (c *CAT) Pause() {
    cmd := exec.Command("/usr/bin/python")
    cmd.Stdin = os.Stdin
    cmd.Stdout = os.Stdout
    cmd.Stderr = os.Stderr
    cmd.Run()
    time.Sleep(3600 * time.Second)
}

func (c *CAT) setHostIP() error {
    cmd := exec.Command("hostname", "-i")
    out, err := cmd.CombinedOutput()
    if err != nil {
        log.Fatal("Cannot find host ip address")
        return err
    }

    ips := strings.Split(string(out), " ")
    for _, ip := range ips {
        if !strings.HasPrefix(ip, "127.") {
            c.Sut.Manager.IP = strings.Trim(ip, "\n")
            return nil
        }
    }
    return errors.New("Cannot retrieve host ip address")
}

func (c *CAT) AddAgent(test string, name string, control_nodes []*controlnode.ControlNode) (*agent.Agent, error) {
    var xmpp_ports []int
    for _, control_node := range control_nodes {
        xmpp_ports = append(xmpp_ports, control_node.Config.XMPPPort)
    }
    agent, err := agent.New(c.Sut.Manager, name, test, xmpp_ports)
    if err != nil {
        return nil, fmt.Errorf("failed create agent: %v", err)
    }
    c.Agents = append(c.Agents, agent)
    return agent, nil
}

func (c *CAT) AddControlNode(test, name, ip_address, conf_file string, http_port int) (*controlnode.ControlNode, error) {
    cn, err := controlnode.New(c.Sut.Manager, name, ip_address, conf_file, test, http_port)
    if err != nil {
        return nil, fmt.Errorf("failed to create control-node: %v", err)
    }
    cn.Verbose = c.Sut.Manager.Verbose
    c.ControlNodes = append(c.ControlNodes, cn)
    log.Printf("Started %s at http://%s:%d\n", cn.Name, c.Sut.Manager.IP, cn.Config.HTTPPort)
    return cn, nil
}
