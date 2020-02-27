package models

import (
	"strings"
)

// SetDNSNameservers sets DNS Nameservers.
func (m *IpamSubnetType) SetDNSNameservers(nameservers []string) {
	opts := &DhcpOptionsListType{}
	if len(nameservers) > 0 {
		opts.DHCPOption = []*DhcpOptionType{{
			DHCPOptionName:  "6",
			DHCPOptionValue: strings.Join(nameservers, " "),
		}}
	}
	m.DHCPOptionList = opts
}
