// Package agent provides methods to manage (mock) contrail-vrouter-agent
// configuration objects and processes.
package agent

import (
        "encoding/json"
	"bufio"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
        "time"

	"cat/sut"

	log "github.com/sirupsen/logrus"
)

const agentName = "control-vrouter-agent"
const agentConfFile = "../agent/contrail-vrouter-agent.conf"
const agentBinary = "../../../../build/debug/vnsw/agent/contrail/contrail-vrouter-agent"
const agentAddPortBinary = "../../../../controller/src/vnsw/agent/port_ipc/vrouter-port-control"

// Agent is a SUT component for VRouter Agent.
type Agent struct {
	sut.Component
	Endpoints []sut.Endpoint
}

// New instatiates a mock contrail-vrouter-agent process.
func New(m sut.Manager, name, binary, test string, endpoints []sut.Endpoint) (*Agent, error) {
	a := &Agent{
		Component: sut.Component{
			Name:    name,
			Manager: m,
			LogDir:  filepath.Join(m.RootDir, test, agentName, name, "log"),
			ConfDir: filepath.Join(m.RootDir, test, agentName, name, "conf"),
			Config: sut.Config{
				BGPPort:  0,
				HTTPPort: 0,
				XMPPPort: 0,
			},
			ConfFile: "",
		},
		Endpoints: endpoints,
	}
	if err := os.MkdirAll(a.Component.ConfDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to make conf directory: %v", err)
	}
	if err := os.MkdirAll(a.Component.LogDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to make log directory: %v", err)
	}
	a.writeConfiguration()
	if err := a.start(binary); err != nil {
		return nil, fmt.Errorf("failed to start agent binary: %v", err)
	}
        if err := a.readAgentHttpPort(); err != nil {
                return nil, fmt.Errorf("failed to read http port for agent: %v", err)
        }
	return a, nil
}

// start starts the mock contrail-vrouter-agent process in the background.
func (a *Agent) start(binary string) error {
	if binary == " " {
		binary = agentBinary
	}
	if _, err := os.Stat(agentBinary); err != nil {
		return fmt.Errorf("failed to get agent binary file: %v", err)
	}
	a.Cmd = exec.Command(agentBinary, "--config_file="+a.Component.ConfFile)
	env := sut.EnvMap{
		"LD_LIBRARY_PATH": "../../../../build/lib",
		"LOGNAME":         os.Getenv("USER"),
	}
	a.Cmd.Env = a.Env(env)
	if err := a.Cmd.Start(); err != nil {
		return fmt.Errorf("Failed to start agent: %v", err)
	}
	return nil
}

// AddVirtualPort adds a mock VMI port into the mocked vrouter agent process.
func (a *Agent) AddVirtualPort(vmi_uuid, vm_uuid, vn_uuid, project_uuid, ipv4_address, mac_address, tap_if, portnum, vm_name string) error {
	cmd := fmt.Sprintf("sudo %s --oper=add --uuid=%s --instance_uuid=%s --vn_uuid=%s --vm_project_uuid=%s --ip_address=%s  --ipv6_address= --vm_name=%s --tap_name=%s --mac=%s --rx_vlan_id=0 --tx_vlan_id=0 --agent_port=%s", agentAddPortBinary, vmi_uuid, vm_uuid, vn_uuid, project_uuid, ipv4_address, vm_name, tap_if, mac_address, portnum)
	log.Infof("AddVirtualPort: %q", cmd)
	_, err := exec.Command("/bin/bash", "-c", cmd).Output()
	return err
}

// writeConfiguration generates agent configuration with appropriate xmpp
// server port numbers into agentConfFile.
func (a *Agent) writeConfiguration() error {
	file, err := os.Open(agentConfFile)
	if err != nil {
		return fmt.Errorf("failed to open agent conf file: %v", err)
	}
	defer file.Close()
	var config []string
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := scanner.Text()
		var confLine string
		switch {
		case strings.Contains(line, "xmpp_port"):
			confLine = "servers="
			for _, endpoint := range a.Endpoints {
				confLine = fmt.Sprintf("%s%s:%d ", confLine, endpoint.IP, endpoint.Port)
			}
		case strings.Contains(line, "agent_name"):
			confLine = fmt.Sprintf("agent_name=%s", a.Name)
		case strings.Contains(line, "log_file"):
			confLine = fmt.Sprintf("log_file=%s/%s.log", a.LogDir, a.Name)
		default:
			confLine = line
		}
		config = append(config, confLine)
	}

	a.Component.ConfFile = fmt.Sprintf("%s/%s.conf", a.Component.ConfDir, a.Component.Name)
	if err := ioutil.WriteFile(a.Component.ConfFile, []byte(strings.Join(config, "\n")), 0644); err != nil {
		return fmt.Errorf("failed to write to agent conf file: %v", err)
	}
	return nil
}

func (a *Agent) readAgentHttpPort() error {
        a.PortsFile = fmt.Sprintf("%d.json", a.Cmd.Process.Pid)
        retry := 30
        var err error
        for retry > 0 {
                if bytes, err := ioutil.ReadFile(a.PortsFile); err == nil {
                        if err := json.Unmarshal(bytes, &a.Config); err == nil {
                                return nil
                        }
                }
                time.Sleep(1 * time.Second)
                retry = retry - 1
        }
        return err
}

// Check interface Active/Incative state in agent introspect
func (a *Agent) VerifyIntrospectInterfaceState(intf string, active bool) error {
        url := fmt.Sprintf("/usr/bin/curl --connect-timeout 5 -s http://%s:%d/Snh_ItfReq?name=%s | xmllint --format - | grep  \\/active | grep Active", "0.0.0.0", a.Config.HTTPPort, intf)
        port_active, err := exec.Command("/bin/bash", "-c", url).Output()
        log.Infof("Command %s completed %s with status %v\n", url, port_active, err)
        if (err != nil  && active) {
                return fmt.Errorf("Interface not active in agent: %v ", err)
        }

        return nil
}
