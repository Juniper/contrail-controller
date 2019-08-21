package agent

import (
	"cat/component"
)

// Agent is a SUT component for VRouter Agent.
type Agent struct {
	component.Component
	cat.XmppPorts []int
}


func (self *CAT) DeleteAllFile() {
	return
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


func (c *ControlNode) Initialize(test, name string, cat *CAT) *ControlNode {
	self.Cat = cat
	self.Name = name
	self.Pid = 0
	self.HttpPort = 0
	self.XmppPort = 0
	self.PortsFile = ""

	self.Verbose = false
	self.ConfDir = cat.LogDir + "/" + test + "/control-node/" + name + "/" +
		"conf"
	self.LogDir = cat.LogDir + "/" + test + "/control-node/" + name + "/" +
		"log"
	self.Cat.CreateDir(self.LogDir)
	self.Cat.CreateDir(self.ConfDir)
	return self
}

func (self *ControlNode) Start() int {
	c1 :=
		"../../../../../../build//debug/bgp/test/bgp_ifmap_xmpp_integration_test"
	if _, err := os.Stat(c1); os.IsNotExist(err) {
		log.Fatal(err)
	}

	env := map[string]string{
		"USER": os.Getenv("USER"),
		"BGP_IFMAP_XMPP_INTEGRATION_TEST_SELF_NAME": "overcloud-contrailcontroller-1",
		"CAT_BGP_PORT":  strconv.Itoa(self.Ports.BgpPort),
		"CAT_XMPP_PORT": strconv.Itoa(self.Ports.XmppPort),
		"BGP_IFMAP_XMPP_INTEGRATION_TEST_INTROSPECT": strconv.Itoa(self.Ports.HttpPort),
		"BGP_IFMAP_XMPP_INTEGRATION_TEST_PAUSE":      "1",
		"LOG_DISABLE":                                strconv.FormatBool(self.Verbose),
		"BGP_IFMAP_XMPP_INTEGRATION_TEST_DATA_FILE":  "/cs-shared/CAT/configs/bulk_sync_2.json",
		"LD_LIBRARY_PATH":                            "../../../../../../build/lib",
		"CONTRAIL_CAT_FRAMEWORK":                     "1",
		"USER_DIR":                                   self.ConfDir + "/..",
	}
	var enva []string
	for k, v := range env {
		enva = append(enva, k+"="+v)
	}
	cmd := exec.Command(c1)
	cmd.Env = enva
	err := cmd.Start()
	if err != nil {
		log.Fatal(err)
	}
	return cmd.Process.Pid
}

func (self *ControlNode) Restart() {
	syscall.Kill(self.Pid, syscall.SIGKILL)
	self.Pid = self.Start()
}

func (self *Agent) Initialize(test, name string, cat *CAT) *Agent {
	self.Cat = cat
	self.Pid = 0
	self.XmppPort = 0
	self.ConfFile = ""
	self.Name = name
	self.ConfDir = cat.LogDir + "/" + test + "/control-vrouter-agent/" +
		name + "/" + "conf"
	self.LogDir = cat.LogDir + "/" + test + "/control-vrouter-agent/" +
		name + "/" + "log"
	self.Cat.CreateDir(self.ConfDir)
	self.Cat.CreateDir(self.LogDir)
	return self
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
