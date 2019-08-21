package agent

import (
	"cat/sut"
)

// Agent is a SUT component for VRouter Agent.
type Agent struct {
	sut.Component
	XMPPPorts []int
}

func New(sut sut.SUT, name, test string) (*Agent, error) {
	a := &Agent {
		Name: name,
		SUT: sut,
		LogDir: filepath.Join(cat.LogDir, test, agentName, name, "log")
		ConfDir: filepath.Join(cat.LogDir, test, agentName, name, "conf")
		PID: 0,
		HttpPort: 0,
		XMPPPort: 0,
		ConfFile: "",
	}
	if err := os.MakeDirsAll(a.ConfDir); err != nil {
		return nil, fmt.Errorf("failed to make conf directory: %v", err)
	}
	if err := os.MakeDirsAll(a.LogDir); err != nil {
		return nil, fmt.Errorf("failed to make log directory: %v", err)
	}
	return a, nil
}

// Stop will stop the component sending a signal to the process.
func (a *Agent) Stop(signal syscall.Signal) {
	syscall.Kill(c.Ports.PID, signal) // syscall.SIGHUP
  os.Remove(strconv.Itoa(item.Pid) + ".json")
}

func (self *ControlNode) Restart() {
	syscall.Kill(self.Pid, syscall.SIGKILL)
	self.Pid = self.Start()
}

func (self *Agent) Initialize(test, name string, cat *CAT) *Agent {

}

func (self *Agent) CreateAgent(control_nodes []*ControlNode) {
	var ports []int
	for _, i := range control_nodes {
		ports = append(ports, i.Ports.XmppPort)
	}
	self.XmppPorts = ports
	self.CreateConfiguration()
	self.Pid = self.Start()
}

func (self *Agent) Start() int {
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
	cmd := exec.Command(c1, "--config_file="+self.ConfFile)
	cmd.Env = enva
	err := cmd.Start()
	if err != nil {
		log.Fatal(err)
	}
	return cmd.Process.Pid
}

func (self *Agent) CreateConfiguration() {
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
			for _, p := range self.XmppPorts {
				updated_line = updated_line + "127.0.0.1:" + strconv.Itoa(p) + " "
			}
			new_conf = append(new_conf, updated_line)
		} else if strings.Contains(line, "agent_name") {
			new_conf = append(new_conf, "agent_name="+self.Name)
		} else if strings.Contains(line, "log_file") {
			new_conf = append(new_conf, "log_file="+self.LogDir+"/"+
				self.Name+".log")
		} else {
			new_conf = append(new_conf, line)
		}
	}

	self.ConfFile = self.ConfDir + "/" + self.Name + ".conf"
	file, err = os.Create(self.ConfFile)
	if err != nil {
		log.Fatal(err)
	}
	defer file.Close()

	for _, line := range new_conf {
		file.WriteString(line + "\n")
	}
}
