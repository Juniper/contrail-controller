// Package agent_test provides unit tests to test agent package methods.
package agent_test

import (
	"os"
	"syscall"
	"testing"

	"cat"
	"cat/agent"
	"cat/sut"
)

func TestAgent(t *testing.T) {
	c, err := cat.New()
	if err != nil {
		t.Errorf("Failed to create CAT object: %v", err)
	}

	endpoints := []sut.Endpoint{
		sut.Endpoint{
			IP:   "10.0.0.1",
			Port: 1,
		},
		sut.Endpoint{
			IP:   "10.0.0.2",
			Port: 2,
		},
	}
	a, err := agent.New(c.SUT.Manager, "agent", " ", "test", endpoints)
	if err != nil {
		t.Errorf("Failed to create agent: %v", err)
	}

	if a.Name != "agent" {
		t.Errorf("incorrect agent name %s; want %s", a.Name, "agent")
	}

	// Verify that agent process is started
	pid := a.Cmd.Process.Pid
	if pid == 0 {
		t.Errorf("%s: process id is zero; want non-zero", a.Name)
	}

	// Verify that component directory is created
	if _, err := os.Stat(a.Component.ConfDir); err != nil {
		t.Errorf("%s: Conf directory %s is not created", a.Name, a.Component.ConfDir)
	}
	if _, err := os.Stat(a.Component.LogDir); err != nil {
		t.Errorf("%s: Log directory %s is not created", a.Name, a.Component.LogDir)
	}

	if err := a.Teardown(); err != nil {
		t.Fatalf("CAT objects cleanup failed: %v", err)
	}

	// Verify that process indeed went down.
	if err := syscall.Kill(pid, syscall.Signal(0)); err != nil {
		t.Fatalf("%s process %d did not die", a.Name, pid)
	}
}
