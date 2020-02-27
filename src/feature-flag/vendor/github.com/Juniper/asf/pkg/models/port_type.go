package models

// AllPorts returns a range of all possible ports.
func AllPorts() *PortType {
	return &PortType{
		StartPort: 0,
		EndPort:   65535,
	}
}
