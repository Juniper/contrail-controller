package sut

type SUT struct {
	Verbose   bool
	IP        string
	ReportDir string
	LogDir    string
	RootDir   string
}

// Component is the base CAT component used for testing.
type Component struct {
	Name      string
	SUT       SUT
	LogDir    string
	ConfDir   string
	ConfFile  string
	PortsFile string
	Verbose   bool
	Config    Config
}

type Config struct {
	PID      int `json:pid`
	XMPPPort int `json:xmpp_port`
	BGPPort  int `json:bgp_port`
	HTTPPort int `json:http_port`
}
