package component

import (
	"syscall"
)

// Component is the base CAT component used for testing.
type Component struct {
	Name      string
	LogDir    string
	ConfDir   string
	ConfFile  string
	PortsFile string
	Verbose   bool
	HTTPPort  int
	XMPPPort  int
	Ports     struct {
		PID      int `json:PID`
		XmppPort int `json:XmppPort`
		BgpPort  int `json:BgpPort`
		HttpPort int `json:HttpPort`
	}
}

func (c *Component) Stop(signal syscall.Signal) {
	syscall.Kill(c.Ports.PID, signal) // syscall.SIGHUP
}
