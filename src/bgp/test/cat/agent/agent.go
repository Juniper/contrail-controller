package agent

import (
	"fmt"
	"os"
	"path/filepath"
	"syscall"

	"cat/sut"
)

const agentName = "control-vrouter-agent"

// Agent is a SUT component for VRouter Agent.
type Agent struct {
	Component sut.Component
	XMPPPorts []int
}

func New(m sut.Manager, name, test string) (*Agent, error) {
	a := &Agent{
		Component: sut.Component{
			Name:    name,
			Manager: m,
			LogDir:  filepath.Join(m.LogDir, test, agentName, name, "log"),
			ConfDir: filepath.Join(m.LogDir, test, agentName, name, "conf"),
			Config: sut.Config{
				PID:      0,
				HTTPPort: 0,
				XMPPPort: 0,
			},
			ConfFile: "",
		},
	}
	if err := os.MkdirAll(a.Component.ConfDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to make conf directory: %v", err)
	}
	if err := os.MkdirAll(a.Component.LogDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to make log directory: %v", err)
	}
	return a, nil
}

// Stop will stop the component sending a signal to the process.
func (a *Agent) Stop(signal syscall.Signal) {
	syscall.Kill(c.Component.Config.PID, signal) // syscall.SIGHUP
	os.Remove(strconv.Itoa(item.Pid) + ".json")
}

func (a *Agent) CreateAgent(control_nodes []*ControlNode) {
	var ports []int
	for _, i := range control_nodes {
		ports = append(ports, i.Ports.XmppPort)
	}
	a.XmppPorts = ports
	a.CreateConfiguration()
	a.Pid = a.Start()
}

func (a *Agent) Start() int {
	c1 :=
		"../../../../../../build/debug/vnsw/agent/contrail/contrail-vrouter-agent"
	if _, err := os.Stat(c1); os.IsNotExist(err) {
		log.Fatal(err)
	}
	env := map[string]string{
		"LD_LIBRARY_PATH": "../../../../../../build/lib",
		"LOGNAME":         os.Getenv("USER"),
	}
	var enva []string
	for k, v := range env {
		enva = append(enva, k+"="+v)
	}
	cmd := exec.Command(c1, "--config_file="+a.ConfFile)
	cmd.Env = enva
	err := cmd.Start()
	if err != nil {
		log.Fatal(err)
	}
	return cmd.Process.Pid
}

func (a *Agent) CreateConfiguration() {
	sample_conf := "contrail-vrouter-agent.conf"
	file, err := os.Open(sample_conf)
	if err != nil {
		log.Fatal(err)
	}
	defer file.Close()
	var new_conf []string
	scanner := bufio.NewScanner(file)
	for scanner.Scan() {
		line := scanner.Text()
		if strings.Contains(line, "xmpp_port") {
			updated_line := "servers="
			for _, p := range a.XmppPorts {
				updated_line = updated_line + "127.0.0.1:" + strconv.Itoa(p) + " "
			}
			new_conf = append(new_conf, updated_line)
		} else if strings.Contains(line, "agent_name") {
			new_conf = append(new_conf, "agent_name="+a.Name)
		} else if strings.Contains(line, "log_file") {
			new_conf = append(new_conf, "log_file="+a.LogDir+"/"+
				a.Name+".log")
		} else {
			new_conf = append(new_conf, line)
		}
	}

	a.ConfFile = a.ConfDir + "/" + a.Name + ".conf"
	file, err = os.Create(a.ConfFile)
	if err != nil {
		log.Fatal(err)
	}
	defer file.Close()

	for _, line := range new_conf {
		file.WriteString(line + "\n")
	}
}
