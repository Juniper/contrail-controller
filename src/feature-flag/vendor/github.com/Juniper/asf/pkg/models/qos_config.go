package models

// CheckQoSValues checks values of DSCP, VlanPriority and MPLS EXP Entries.
func (m *QosConfig) CheckQoSValues() error {
	if err := m.validateDscpEntries(); err != nil {
		return err
	}
	if err := m.validateVlanPriorityEntries(); err != nil {
		return err
	}
	if err := m.validateMplsExpEntries(); err != nil {
		return err
	}
	return nil
}

func (m *QosConfig) validateDscpEntries() error {
	for _, qosIDPair := range m.GetDSCPEntries().GetQosIDForwardingClassPair() {
		if err := qosIDPair.CheckKeyDscp(); err != nil {
			return err
		}
	}
	return nil
}

func (m *QosConfig) validateVlanPriorityEntries() error {
	for _, qosIDPair := range m.GetVlanPriorityEntries().GetQosIDForwardingClassPair() {
		if err := qosIDPair.CheckKeyVlanPriority(); err != nil {
			return err
		}
	}
	return nil
}

func (m *QosConfig) validateMplsExpEntries() error {
	for _, qosIDPair := range m.GetMPLSExpEntries().GetQosIDForwardingClassPair() {
		if err := qosIDPair.CheckKeyMplsExp(); err != nil {
			return err
		}
	}
	return nil
}

// AddDefaultGlobalSystemConfigRef adds Default Global System Config reference to QoS config.
func (m *QosConfig) AddDefaultGlobalSystemConfigRef(uuid string, fqName []string) {
	m.GlobalSystemConfigRefs = append(m.GlobalSystemConfigRefs, &QosConfigGlobalSystemConfigRef{
		UUID: uuid,
		To:   fqName,
	})
}
