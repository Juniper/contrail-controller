/*
 * Copyright (c) 2019 Juniper Networks, Inc. All rights reserved.
 */

package cat

import (
    "errors"
    "fmt"
    log "github.com/sirupsen/logrus"
    "os"
    "os/exec"
    "path/filepath"
    "strings"
    "time"

    "cat/agent"
    "cat/config"
    "cat/controlnode"
    "cat/crpd"
    "cat/sut"
)

// CAT is a contrail automated test.
type CAT struct {
    SUT           sut.Component

    ControlNodes []*controlnode.ControlNode
    Agents       []*agent.Agent
    CRPDs        []*crpd.CRPD
    FqNameTable     config.FQNameTableType
    UuidTable       config.UUIDTableType
    ConfigMap       config.ConfigMap
}

// Timestamp format for logfiles.
const timestamp = "20060102_150405"

const crpdImageGetCommand = "sshpass -p c0ntrail123 ssh 10.84.5.39 cat /cs-shared/crpd/crpd.tgz | sudo --non-interactive docker load"

// New creates an initialized CAT instance.
func New() (*CAT, error) {
    c := &CAT{
        ControlNodes: []*controlnode.ControlNode{},
        Agents: []*agent.Agent{},
        CRPDs: []*crpd.CRPD{},
        FqNameTable: config.FQNameTableType{},
        UuidTable: config.UUIDTableType{},
        ConfigMap: config.ConfigMap{},
    }
    now := time.Now()

    cwd, err := os.Getwd()
    if err != nil {
        return nil, fmt.Errorf("Cannot find present working directory: %v", err)
    }
    c.SUT.Manager.RootDir = filepath.Join(cwd + "../../../../build/debug/cat", now.Format(timestamp))
    if err := os.MkdirAll(c.SUT.Manager.RootDir, 0700); err != nil {
        return nil, fmt.Errorf("failed to create rootdir %q :%v", c.SUT.Manager.RootDir, err)
    }
    c.SUT.Manager.ReportDir = filepath.Join(c.SUT.Manager.RootDir, "reports")
    err = os.MkdirAll(c.SUT.Manager.ReportDir, 0700)
    if err != nil {
        return nil, fmt.Errorf("failed to make report directory: %v", err)
    }
    c.setHostIP()

    if crpd.CanUseCRPD() {
        cmd := exec.Command("sudo", "--non-interactive", "/usr/bin/docker", "image", "inspect", "crpd")
        if _, err := cmd.Output(); err != nil {
            // Setup crpd docker image.
            cmd := exec.Command("/bin/bash", "-c", crpdImageGetCommand)
            if _, err := cmd.Output(); err != nil {
                return nil, fmt.Errorf("Cannot load crpd docker image")
            }
        }
    }
    log.Infof("Test data in %s", c.SUT.Manager.RootDir)
    return c, err
}

// Teardown stops all components and closes down the CAT instance.
func (c *CAT) Teardown() error {
    for _, cn := range c.ControlNodes {
        if err := cn.Teardown(); err != nil {
            return err
        }
    }
    for _, a := range c.Agents {
        if err := a.Teardown(); err != nil {
            return err
        }
    }

    for _, cr := range c.CRPDs {
        if err := cr.Teardown(); err != nil {
            return err
        }
    }
    return nil
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
            c.SUT.Manager.IP = strings.Trim(ip, "\n")
            return nil
        }
    }
    return errors.New("Cannot retrieve host ip address")
}

func (c *CAT) AddAgent(test string, name string, control_nodes []*controlnode.ControlNode) (*agent.Agent, error) {
    endpoints := []sut.Endpoint{}
    for _, control_node := range control_nodes {
        endpoints = append(endpoints, sut.Endpoint{
            IP: control_node.IPAddress,
            Port: control_node.Config.XMPPPort,
        })
    }
    agent, err := agent.New(c.SUT.Manager, name, test, endpoints)
    if err != nil {
        return nil, fmt.Errorf("failed create agent: %v", err)
    }
    c.Agents = append(c.Agents, agent)
    return agent, nil
}

func (c *CAT) AddControlNode(test, name, ip_address, conf_file string, http_port int) (*controlnode.ControlNode, error) {
    cn, err := controlnode.New(c.SUT.Manager, name, ip_address, conf_file, test, http_port)
    if err != nil {
        return nil, fmt.Errorf("failed to create control-node: %v", err)
    }
    cn.Verbose = c.SUT.Manager.Verbose
    c.ControlNodes = append(c.ControlNodes, cn)
    log.Infof("Started %s at http://%s:%d\n", cn.Name, c.SUT.Manager.IP, cn.Config.HTTPPort)
    return cn, nil
}

func (c *CAT) AddCRPD(test, name string) (*crpd.CRPD, error) {
    cr, err := crpd.New(c.SUT.Manager, name, test)
    if err != nil {
        return nil, fmt.Errorf("failed to create crpd: %v", err)
    }
    c.CRPDs = append(c.CRPDs, cr)
    return cr, nil
}
