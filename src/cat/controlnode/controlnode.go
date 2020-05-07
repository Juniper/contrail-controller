// Package controlnode provides methods to instantiate and manage control-node
// (aka contrail-control) objects and processes.
package controlnode

import (
	"encoding/json"
	"fmt"
	"io/ioutil"
	"os"
	"os/exec"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"

	"cat/agent"
	"cat/config"
	"cat/sut"

	log "github.com/sirupsen/logrus"
)

// ControlNode represents a contrail-control (control-node) process under CAT
// framework.
type ControlNode struct {
	sut.Component
	confFile       string
	ContrailConfig *config.BGPRouter
}

const controlNodeName = "control-node"
const controlNodeBinary = "../../../../build/debug/bgp/test/bgp_ifmap_xmpp_integration_test"
const controlNodeConfFile = "../../../../controller/src/ifmap/client/testdata/exmp_gen_config.json"

// New creates a ControlNode object and starts the process to run in background.
// This routine shall not return until the launched control-node's server ports
// are retrieved (BGP, XMPP and HTTP/Introspect)
func New(m sut.Manager, name, ipAddress, confFile, test string, bgpPort int) (*ControlNode, error) {
	// Attach IP to loopback.
	if !strings.HasPrefix(ipAddress, "127.") {
		cmd := exec.Command("sudo", "--non-interactive", "ip", "address", "add", ipAddress+"/24", "dev", "lo")
		if err := cmd.Start(); err != nil {
			return nil, fmt.Errorf("Failed to add ip address %s on lo: %v", ipAddress, err)
		}
	}

	c := &ControlNode{
		Component: sut.Component{
			Name:      name,
			IPAddress: ipAddress,
			Manager:   m,
			LogDir:    filepath.Join(m.RootDir, test, controlNodeName, name, "log"),
			ConfDir:   filepath.Join(m.RootDir, test, controlNodeName, name, "conf"),
			Config: sut.Config{
				BGPPort:  bgpPort,
				HTTPPort: 0,
				XMPPPort: 0,
			},
			ConfFile: confFile,
		},
	}
	if err := os.MkdirAll(c.Component.ConfDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to make conf directory: %v", err)
	}
	if err := os.MkdirAll(c.Component.LogDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to make log directory: %v", err)
	}
	if err := c.start(); err != nil {
		return nil, err
	}
	if err := c.readPortNumbers(); err != nil {
		return nil, err
	}
	return c, nil
}

// start starts the control-node process in the OS.
func (c *ControlNode) start() error {
	if _, err := os.Stat(controlNodeBinary); err != nil {
		log.Fatal(err)
	}

	if c.ConfFile == "" {
		c.ConfFile = controlNodeConfFile
	}

	output, err := os.Create(c.Component.LogDir + "/log")
	if err != nil {
		log.Fatal(err)
	}
	defer output.Close()

	c.Cmd = exec.Command(controlNodeBinary, "--config_file="+c.Component.ConfFile)
	env := sut.EnvMap{
		"USER": os.Getenv("USER"),
		"BGP_IFMAP_XMPP_INTEGRATION_TEST_SELF_NAME":  c.Name,
		"CAT_BGP_IP_ADDRESS":                         c.IPAddress,
		"CAT_BGP_PORT":                               strconv.Itoa(c.Config.BGPPort),
		"CAT_XMPP_PORT":                              strconv.Itoa(c.Config.XMPPPort),
		"BGP_IFMAP_XMPP_INTEGRATION_TEST_INTROSPECT": strconv.Itoa(c.Config.HTTPPort),
		"BGP_IFMAP_XMPP_INTEGRATION_TEST_PAUSE":      "1",
		//"LOG_DISABLE" : strconv.FormatBool(c.Verbose),
		"BGP_IFMAP_XMPP_INTEGRATION_TEST_DATA_FILE": c.ConfFile,
		"LD_LIBRARY_PATH":                           "../../../../build/lib",
		"CONTRAIL_CAT_FRAMEWORK":                    "1",
		"USER_DIR":                                  c.ConfDir + "/..",
	}

	c.Cmd.Stdout = output
	c.Cmd.Env = c.Env(env)
	if err := c.Cmd.Start(); err != nil {
		return fmt.Errorf("Failed to start Control-node: %v", err)
	}
	return nil
}

// Restart restarts the control-node process already running before.
func (c *ControlNode) Restart() error {
	log.Debugf("Restart %s\n", c.Name)
	if err := c.Stop(); err != nil {
		return err
	}
	return c.start()
}

// Teardown cleanup ControlNode object by terminating associated control-node
// process.
func (c *ControlNode) Teardown() error {
	// Detach IP from loopback.
	if !strings.HasPrefix(c.IPAddress, "127.") {
		cmd := exec.Command("sudo", "--non-interactive", "ip", "address", "delete", c.IPAddress+"/24", "dev", "lo")
		if err := cmd.Start(); err != nil {
			return fmt.Errorf("Failed to delete ip address %s from lo: %v", c.IPAddress, err)
		}
	}
	c.Component.Teardown()
	return nil
}

// readPortNumbers reads BGP, XMPP and HTTP listening port numbers from mock
// control-node.
func (c *ControlNode) readPortNumbers() error {
	c.PortsFile = fmt.Sprintf("%s/%d.json", c.ConfDir, c.Cmd.Process.Pid)
	retry := 30
	var err error
	for retry > 0 {
		if bytes, err := ioutil.ReadFile(c.PortsFile); err == nil {
			if err := json.Unmarshal(bytes, &c.Config); err == nil {
				return nil
			}
		}
		time.Sleep(1 * time.Second)
		retry = retry - 1
	}
	return err
}

// CheckXMPPConnection checks whether XMPP connection to an agent has reached
// ESTABLISHED sate (up).
func (c *ControlNode) CheckXMPPConnection(agent *agent.Agent) error {
	url := fmt.Sprintf("/usr/bin/curl --connect-timeout 5 -s http://%s:%d/Snh_BgpNeighborReq?x=%s | xmllint --format  - | grep -iw state | grep -i Established", c.IPAddress, c.Config.HTTPPort, agent.Component.Name)
	_, err := exec.Command("/bin/bash", "-c", url).Output()
	return err
}

// CheckXMPPConnections checks whether XMPP connections to all agents provided
// has reached ESTABLISHED state. Retry and wait time can be used as applicable.
func (c *ControlNode) CheckXMPPConnections(agents []*agent.Agent, retry int, wait time.Duration) error {
	for r := 0; r < retry; r++ {
		var err error
		for i := 0; i < len(agents); i++ {
			if err = c.CheckXMPPConnection(agents[i]); err != nil {
				break
			}
			log.Infof("%s: XMPP Peer %s session reached established state", c.Name, agents[i].Name)
		}
		if err == nil {
			return nil
		}
		time.Sleep(wait * time.Second)
	}
	return fmt.Errorf("CheckXMPPConnections failed")
}

// CheckBGPConnection checks whether BGP connection to an agent has reached
// ESTABLISHED sate (up) (or DOWN) as requested in the down bool parameter.
func (c *ControlNode) CheckBGPConnection(name string, down bool) error {
	url := fmt.Sprintf("/usr/bin/curl --connect-timeout 5 -s http://%s:%d/Snh_BgpNeighborReq?x=%s | xmllint --format  - | grep -wi state | grep -i Established", c.IPAddress, c.Config.HTTPPort, name)
	_, err := exec.Command("/bin/bash", "-c", url).Output()
	log.Debugf("Command %s completed with status %v\n", url, err)
	if !down {
		return err
	}

	// session should be down, so expect an error here from the grep!
	if err != nil {
		return nil
	}

	return fmt.Errorf("BGP session is still established")
}

// CheckBGPConnections checks whether BGP connections to all BGP routers
// provided has reached ESTABLISHED state (or not). Retry and wait time can be
// used as applicable.
func (c *ControlNode) CheckBGPConnections(components []*sut.Component, down bool, retry int, wait time.Duration) error {
	for r := 0; r < retry; r++ {
		var err error
		for i := 0; i < len(components); i++ {
			if &c.Component != components[i] {
				if err = c.CheckBGPConnection(components[i].Name, down); err != nil {
					break
				}
				if !down {
					log.Infof("%s: BGP Peer %s session reached established state", c.Name, components[i].Name)
				}
			}
		}
		if err == nil {
			return nil
		}
		time.Sleep(wait * time.Second)
	}
	return fmt.Errorf("CheckBGPConnections failed")
}

// checkConfiguration checks whether a particular configuration object count
// matches what is reflected in memory data base of control-node.
//
// TODO: Parse xml to do specific checks in the data received from curl.
func (c *ControlNode) checkConfiguration(tp string, count int) error {
	url := fmt.Sprintf("/usr/bin/curl --connect-timeout 5 -s http://%s:%d/Snh_IFMapNodeTableListShowReq? | xmllint format -  | grep -iw -A1 '>%s</table_name>' | grep -w '>'%d'</size>'", c.IPAddress, c.Config.HTTPPort, tp, count)
	_, err := exec.Command("/bin/bash", "-c", url).Output()

	if count == 0 {
		if err == nil {
			return fmt.Errorf("%s config is still present", tp)
		}
		return nil
	}
	return err
}

// CheckConfiguration calls checkConfiguration with retries and wait times as
// requested.
func (c *ControlNode) CheckConfiguration(tp string, count int, retry int, wait time.Duration) error {
	for r := 0; r < retry; r++ {
		if err := c.checkConfiguration(tp, count); err == nil {
			return nil
		}
		time.Sleep(wait * time.Second)
	}
	return fmt.Errorf("CheckConfiguration failed")
}

// UpdateConfigDB updates the configuration by regnerating all objects into
// the db file and notifies all control-nodes by sending SIGHUP signal.
func UpdateConfigDB(rootdir string, controlNodes []*ControlNode, fqNameTable *config.FQNameTableType, uuidTable *config.UUIDTableType) error {
	if err := config.GenerateDB(fqNameTable, uuidTable, fmt.Sprintf("%s/db.json", rootdir)); err != nil {
		return err
	}

	// Send signal to contrl-nodes to trigger configuration processing.
	for i := range controlNodes {
		log.Debugf("Sending SIGHUP to control-node %d", controlNodes[i].Cmd.Process.Pid)
		if err := controlNodes[i].Cmd.Process.Signal(syscall.SIGHUP); err != nil {
			return fmt.Errorf("Could not send SIGHUP to process %d: %v", controlNodes[i].Cmd.Process.Pid, err)
		}
	}
	return nil
}

func GetConfFile() (string) {
	return controlNodeConfFile
}
