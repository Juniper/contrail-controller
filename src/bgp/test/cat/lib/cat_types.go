package main

// Utility methods to manage tests' log and reports
type CAT struct {
    Directory string
    Pause bool
    Verbose bool
    Ip string
    ReportDir string
    LogDir string
    ControlNodes []*ControlNode
    Agents []*Agent
}

// Specific/allocated tcp ports from various daemons launched
type Ports struct {
    ProcessID int `json:ProcessID`
    XmppPort int `json:XmppPort`
    BgpPort int `json:BgpPort`
    HttpPort int `json:HttpPort`
}

// Common per component class
type Component struct {
    Cat *CAT
    Pid int
    Name string
    LogDir string
    ConfDir string
    Ports Ports
    ConfFile string
    PortsFile string
    Verbose bool
    HttpPort int
    XmppPort int
}

// Control-Node specific class
type ControlNode struct {
    Component
}

// Agent specifc class
type Agent struct {
    Component
    XmppPorts []int
}
