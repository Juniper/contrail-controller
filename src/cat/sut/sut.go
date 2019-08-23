package sut

import (
    "os"
    "strconv"
    "syscall"
)

type Manager struct {
    Verbose   bool
    IP        string
    RootDir   string
    ReportDir string
}

// Component is the base CAT component used for testing.
type Component struct {
    Name      string
    Manager   Manager
    LogDir    string
    ConfDir   string
    ConfFile  string
    PortsFile string
    Verbose   bool
    Config    Config
}

type Config struct {
    Pid      int `json:Pid`
    BGPPort  int `json:BGPPort`
    XMPPPort int `json:XMPPPort`
    HTTPPort int `json:HTTPPort`
}

// Stop will stop the component sending a signal to the process.
func (c *Component) Stop(signal syscall.Signal) {
    syscall.Kill(c.Config.Pid, signal) // syscall.SIGHUP
    os.Remove(strconv.Itoa(c.Config.Pid) + ".json")
}
