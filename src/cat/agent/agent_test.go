/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package agent_test

import (
    "cat"
    "cat/agent"
    log "github.com/sirupsen/logrus"
    "os"
    "syscall"
    "testing"
)

func Test1(t *testing.T) {
    c, err := cat.New(); if err != nil {
        t.Errorf("Failed to create CAT object: %v", err)
    }

    xmpp_ports := []int{1,2}
    a, err := agent.New(c.SUT.Manager, "agent", "test", xmpp_ports); if err != nil {
        t.Errorf("Failed to create agent: %v", err)
    }

    log.Debugf("%v", a)
    if a.Name != "agent" {
        t.Errorf("incorrect agent name %s; want %s", a.Name, "agent")
    }

    // Verify that agent process is started
    pid := a.Cmd.Process.Pid
    if pid == 0 {
        t.Errorf("%s: process id is zero; want non-zero", a.Name)
    }

   // Verify that component directory is created
    _, err = os.Stat(a.Component.ConfDir); if os.IsNotExist(err) {
        t.Errorf("%s: Conf directory %s is not created", a.Name, a.Component.ConfDir)
    }
    _, err = os.Stat(a.Component.LogDir); if os.IsNotExist(err) {
        t.Errorf("%s: Log directory %s is not created", a.Name, a.Component.LogDir)
    }

    if err := a.Teardown(); err != nil {
        t.Fatalf("CAT objects cleanup failed: %v", err)
    }

    // Verify that process indeed went down.
    err = syscall.Kill(pid, syscall.Signal(0)); if err != nil {
        t.Fatalf("%s process %d did not die", a.Name, pid)
    }
}
