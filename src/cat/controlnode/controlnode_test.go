/*
 * copyright (c) 2019 juniper networks, inc. all rights reserved.
 */

package controlnode_test

import (
    "cat"
    "cat/controlnode"
    log "github.com/sirupsen/logrus"
    "syscall"
    "testing"
)

func Test1(t *testing.T) {
    c, err := cat.New(); if err != nil {
        t.Errorf("Failed to create CAT object: %v", err)
    }
    cn, err := controlnode.New(c.SUT.Manager, "control-node", "127.0.0.1", "conf_file", "test", 0); if err != nil {
        t.Errorf("Failed to create control-node: %v", err)
    }

    log.Debugf("%v", cn)
    if cn.Name != "control-node" {
        t.Errorf("incorrect control-node name %s; want %s", cn.Name, "control-node")
    }

    // Verify that control-node process is started
    pid := cn.Cmd.Process.Pid
    if pid == 0 {
        t.Errorf("%s: process id is zero; want non-zero", cn.Name)
    }

    // Verify that control-node process started with valid server ports.
    if cn.Config.BGPPort == 0 {
        t.Errorf("%s: bgp server port is zero; want non-zero", cn.Name)
    }
    if cn.Config.BGPPort == 0 {
        t.Errorf("%s: xmpp server port is zero; want non-zero", cn.Name)
    }
    if cn.Config.BGPPort == 0 {
        t.Errorf("%s: http server port is zero; want non-zero", cn.Name)
    }

    // Verify that the process can be restarted and with the same port numbers.
    config := cn.Config

    // Restart a few times..
    for i := 0; i < 3; i++ {
        err = cn.Restart(); if err != nil {
            t.Errorf("%s: restart failed %v", cn.Name, err)
        }

        if cn.Config != config {
            t.Errorf("%s: ports are different after restart %v; want %v", cn.Name, cn.Config, config)
        }

        if pid == cn.Cmd.Process.Pid {
            t.Errorf("%s: pid must be different after restart %d; want %d", cn.Name, cn.Cmd.Process.Pid, pid)
        }
    }

    if err := cn.Teardown(); err != nil {
        t.Fatalf("CAT objects cleanup failed: %v", err)
    }

    // Verify that process indeed went down.
    err = syscall.Kill(pid, syscall.Signal(0)); if err != nil {
        t.Fatalf("%s process %d did not die", cn.Name, pid)
    }
}
