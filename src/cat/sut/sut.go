/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package sut

import (
    "fmt"
    "os"
    "os/exec"
    "syscall"
)

type Manager struct {
    Verbose   bool
    IP        string
    RootDir   string
    ReportDir string
}

// Component is the base CAT component used for testing.
type Component struct {
    Name      string
    IPAddress string
    Manager   Manager
    LogDir    string
    ConfDir   string
    ConfFile  string
    PortsFile string
    Verbose   bool
    Config    Config
    Cmd      *exec.Cmd
}

type Config struct {
    BGPPort  int `json:BGPPort`
    XMPPPort int `json:XMPPPort`
    HTTPPort int `json:HTTPPort`
}

func (c *Component) Stop(signal syscall.Signal) error {
    if err := c.Cmd.Process.Signal(signal); err != nil {
        return fmt.Errorf("Could not send signal %d to process %d", signal, c.Cmd.Process.Pid)
    }
    os.Remove(fmt.Sprintf("%d.json", c.Cmd.Process.Pid))
    return nil
}

type EnvMap map[string]string

func (a *Component) Env(e EnvMap) []string {
    var envs []string
    for k, v := range e {
        envs = append(envs, k + "=" + v)
    }
    return envs
}
