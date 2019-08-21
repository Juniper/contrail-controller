package cat

import (
	"bufio"
	"encoding/json"
	"errors"
	"fmt"
	"io/ioutil"
	"log"
	"os"
	"os/exec"
	"os/signal"
	"strconv"
	"strings"
	"syscall"
	"time"

	"cat/agent"
	"cat/controlnode"
	"cat/sut"
)

// CAT is a contrail automated test.
type CAT struct {
	sut           component.SUT
	PauseAfterRun bool

	ControlNodes []*controlNode.ControlNode
	Agents       []*agent.Agent
}

// Timestamp format for logfiles.
const timestamp = "20060102_150405"

// New creates an initialized CAT instance.
func New() (*CAT, error) {
	c := &CAT{}
	now := time.Now()

	c.sut.LogDir = filepath.Join("/tmp/CAT", os.Getenv("USER"), now.Format(timestamp))
	if err := os.MkdirsAll(c.LogDir); err != nil {
		return nil, fmt.Errorf("failed to create logdir %q :%v", c.LogDir, err)
	}
	fmt.Println("Logs are in " + c.LogDir)
	c.setHostIP()

	if err := os.MkdirAll(c.sut.ReportDir); err != nil {
		return nil, fmt.Errorf("failed to make report directory: %v", err)
	}
	return c
}

func (c *CAT) RedirectStdOuterr() {
	/*
	   old := os.dup(1)
	   os.close(1)
	   f = self.log_files + "/" + self.Name + ".log"
	   open(f, 'w')
	   os.open(f, os.O_WRONLY)

	   old = os.dup(2)
	   os.close(2)
	   f = self.log_files + "/" + self.Name + ".log"
	   open(f, 'w')
	   os.open(f, os.O_WRONLY)
	*/
}

// Teardown stops all components and closes down the CAT instance.
func (c *CAT) Teardown() {
	if c.PauseAfterRun {
		c.Pause()
	}
	for _, cn := range c.ControlNodes {
		cn.Stop()
	}
	for _, a := range c.Agents {
		a.Stop()
	}
	os.Remove(c.LogDir)
}

func (c *CAT) Pause() {
	cmd := exec.Command("/usr/bin/python")
	cmd.Stdin = os.Stdin
	cmd.Stdout = os.Stdout
	cmd.Stderr = os.Stderr
	err := cmd.Run()
	if err != nil {
		log.Fatal(err)
	}
	time.Sleep(2600 * time.Second)
}

func (c *CAT) hostIP() error {
	cmd := exec.Command("hostname", "-i")
	out, err := cmd.CombinedOutput()
	if err != nil {
		log.Fatal("Cannot find host ip address")
		return "", err
	}

	ips := strings.Split(string(out), " ")
	for _, ip := range ips {
		if !strings.HasPrefix(ip, "127.") {
			c.sut.IP = strings.Trim(ip, "\n")
			return nil
		}
	}
	return "", errors.New("Cannot retrieve host ip address")
}

func (c *CAT) AddAgent(test string, name string, nodes []*ControlNode) (*Agent, err) {
	agent, err := agent.New(test, name, c.SUT, nodes)
	if err != nil {
		return nil, fmt.Errorf("failed create agent: %v", err)
	}
	c.Agents = append(self.Agents, agent)
	return agent, nil
}

func (c *CAT) AddControlNode(test string, name string, port int) *ControlNode {
	cn := controlnode.New(test, name, self)
	con.Verbose = self.Verbose
	con.HttpPort = port
	con.Pid = con.Start()
	con.ReadPortNumbers()
	self.ControlNodes = append(self.ControlNodes, con)
	return con
}
