package controlnode

import (
    "cat/agent"
    "cat/sut"
    "encoding/json"
    "fmt"
    "io/ioutil"
    "log"
    "os"
    "os/exec"
    "path/filepath"
    "strconv"
    "syscall"
    "time"
)

// Control-Node specific class
type ControlNode struct {
    sut.Component
}

const controlNodeName = "control-node"

func New(m sut.Manager, name, test string,http_port int) (*ControlNode, error) {
    c := &ControlNode{
        Component: sut.Component{
            Name:    name,
            Manager: m,
            LogDir: filepath.Join(m.RootDir,test,controlNodeName, name,"log"),
            ConfDir: filepath.Join(m.RootDir,test,controlNodeName,name,"conf"),
            Config: sut.Config{
                Pid:      0,
                HTTPPort: http_port,
                XMPPPort: 0,
            },
            ConfFile: "",
        },
    }
    if err := os.MkdirAll(c.Component.ConfDir, 0755); err != nil {
        return nil, fmt.Errorf("failed to make conf directory: %v", err)
    }
    if err := os.MkdirAll(c.Component.LogDir, 0755); err != nil {
        return nil, fmt.Errorf("failed to make log directory: %v", err)
    }
    c.Component.Config.Pid = c.start()
    c.readPortNumbers()
    return c, nil
}

func (c *ControlNode) start() int {
    c1 :=
      "../../../../build/debug/bgp/test/bgp_ifmap_xmpp_integration_test"
    if _, err := os.Stat(c1); os.IsNotExist(err) {
        log.Fatal(err)
    }

    env := map[string] string {
"USER": os.Getenv("USER"),
"BGP_IFMAP_XMPP_INTEGRATION_TEST_SELF_NAME": "overcloud-contrailcontroller-1",
"CAT_BGP_PORT": strconv.Itoa(c.Config.BGPPort),
"CAT_XMPP_PORT": strconv.Itoa(c.Config.XMPPPort),
"BGP_IFMAP_XMPP_INTEGRATION_TEST_INTROSPECT": strconv.Itoa(c.Config.HTTPPort),
"BGP_IFMAP_XMPP_INTEGRATION_TEST_PAUSE": "1",
"LOG_DISABLE" : strconv.FormatBool(c.Verbose),
"BGP_IFMAP_XMPP_INTEGRATION_TEST_DATA_FILE":
    "../../../../controller/src/ifmap/client/testdata/bulk_sync.json",
"LD_LIBRARY_PATH": "../../../../build/lib",
"CONTRAIL_CAT_FRAMEWORK": "1",
"USER_DIR": c.ConfDir + "/..",
    }
    var enva []string
    for k, v := range env {
        enva = append(enva, k + "=" + v)
    }
    cmd := exec.Command(c1)
    cmd.Env = enva
    err := cmd.Start()
    if err != nil {
        log.Fatal(err)
    }
    return cmd.Process.Pid
}

func (c *ControlNode) Restart() {
    syscall.Kill(c.Config.Pid, syscall.SIGKILL)
    c.Config.Pid = c.start()
}

func (c *ControlNode) readPortNumbers() {
    c.PortsFile = c.ConfDir + "/" + strconv.Itoa(c.Config.Pid) + ".json"
    read := 30
    var err error
    for read > 0 {
        jsonFile, err := os.Open(c.PortsFile)
        defer jsonFile.Close()
        if err != nil {
            time.Sleep(1 * time.Second)
            read = read - 1
            continue
        }
        bytes, _ := ioutil.ReadAll(jsonFile)
        json.Unmarshal(bytes, &c.Config)
        return
    }
    log.Fatal(err)
}

func (c *ControlNode) CheckXmppConnection(agent *agent.Agent) bool {
    url := "/usr/bin/curl -s http://127.0.0.1:" +
        strconv.Itoa(c.Config.HTTPPort) + "/Snh_BgpNeighborReq?x=" +
        agent.Component.Name +
        " | xmllint --format  - | grep -i state | grep -i Established"
    _, err := exec.Command("/bin/bash", "-c", url).Output()
    return err == nil
}

func (c *ControlNode) CheckXmppConnectionsOnce(agents []*agent.Agent) bool {
    for i := 0; i < len(agents); i++ {
        if c.CheckXmppConnection(agents[i]) == false {
            return false
        }
    }
    return true
}

func (c *ControlNode) CheckXmppConnections(agents []*agent.Agent, retry int,
                                           wait time.Duration) bool {
    for r := 0; r < retry; r++ {
        if c.CheckXmppConnectionsOnce(agents) {
            return true
        }
        time.Sleep(wait * time.Second)
    }
    return false
}
