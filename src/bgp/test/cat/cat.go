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
    
    "cat/controlnode"
)

// CAT is a contrail automated test.
type CAT struct {
	Directory    string
	PauseAfterRun        bool
	Verbose      bool
	IP           string
	ReportDir    string
	LogDir       string
	ControlNodes []*controlNode.ControlNode
	Agents       []*agent.Agent
}

func New() (*CAT, error) {
	cmd := exec.Command("/bin/date")
	ds, err := cmd.CombinedOutput()
	if err != nil {
		return nil, fmt.Errorf("failed to get timestamp: %v", err)
	}
	c.LogDir = "/tmp/CAT/" + os.Getenv("USER") + c.LogDir + "/" +
		strings.Replace(strings.TrimSpace(string(ds)), " ", "_", -1)
	self.CreateDir(c.LogDir)
	fmt.Println("Logs are in " + self.LogDir)
	self.Ip, _ = self.__GetHostIp()
	signalCh := make(chan os.Signal, 1)
	signal.Notify(signalCh, os.Interrupt)
    // signal.signal(signal.SIGTERM, CAT.sig_cleanup)
    err := os.MkdirAll(c.ReportDir)

	return c
}




func (c *CAT) Teardown() {
	if c.PauseAfterRun {
		c.Pause()
	}
	for _, item := range c.ControlNodes {
		syscall.Kill(item.Pid, syscall.SIGKILL)
	}
	for _, item := range c.Agents {
		syscall.Kill(item.Pid, syscall.SIGKILL)
		os.Remove(strconv.Itoa(item.Pid) + ".json")
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

func (c *CAT) SigCleanup(sig int, frame int) {
	c.CleanUp()
}

func (c *CAT) hostIP() (string, error) {
	cmd := exec.Command("hostname", "-i")
	out, err := cmd.CombinedOutput()
	if err != nil {
		log.Fatal("Cannot find host ip address")
		return "", err
	}

	ips := strings.Split(string(out), " ")
	for _, ip := range ips {
		if !strings.HasPrefix(ip, "127.") {
			return strings.Trim(ip, "\n"), nil
		}
	}
	return "", errors.New("Cannot retrieve host ip address")
}



func (c *CAT) AddAgent(test string, name string, control_nodes []*ControlNode) *Agent {
	agent := new(Agent).Initialize(test, name, self)
	agent.CreateAgent(control_nodes)
	self.Agents = append(self.Agents, agent)
	return agent
}

func (c *CAT) AddControlNode(test string, name string, port int) *ControlNode {
	con := NewControlNode().Initialize(test, name, self)
	con.Verbose = self.Verbose
	con.HttpPort = port
	con.Pid = con.Start()
	con.ReadPortNumbers()
	self.ControlNodes = append(self.ControlNodes, con)
	return con
}
