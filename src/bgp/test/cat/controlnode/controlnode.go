package controlnode

import (
	"cat/sut"
)

// Control-Node specific class
type ControlNode struct {
	sut.Component
}

func (c *ControlNode) Restart() {
	syscall.Kill(c.Pid, syscall.SIGKILL)
	c.Pid = c.Start()
}

func (c *ControlNode) ReadPortNumbers() {
	c.PortsFile = self.ConfDir + "/" + strconv.Itoa(self.Pid) + ".json"
	read := 30
	var err error
	for read > 0 {
		jsonFile, err := os.Open(self.PortsFile)
		defer jsonFile.Close()
		if err != nil {
			time.Sleep(1 * time.Second)
			read = read - 1
			continue
		}
		bytes, _ := ioutil.ReadAll(jsonFile)
		json.Unmarshal(bytes, &self.Ports)
		return
	}
	log.Fatal(err)
}

func (c *ControlNode) CheckXmppConnection(agent *Agent) bool {
	url := "/usr/bin/curl -s http://127.0.0.1:" +
		strconv.Itoa(c.Config.HttpPort) + "/Snh_BgpNeighborReq?x=" +
		agent.Name +
		" | xmllint --format  - | grep -i state | grep -i Established"
	_, err := exec.Command("/bin/bash", "-c", url).Output()
	return err == nil
}

func (c *ControlNode) CheckXmppConnections(agents []*Agent) bool {
	for i := 0; i < len(agents); i++ {
		if c._CheckXmppConnection(agents[i]) == false {
			return false
		}
	}
	return true
}

func (c *ControlNode) CheckXmppConnections(agents []*Agent, retry int, wait time.Duration) bool {
	for r := 0; r < retry; r++ {
		if c.CheckXmppConnections(agents) {
			return true
		}
		time.Sleep(wait * time.Second)
	}
	return false
}
