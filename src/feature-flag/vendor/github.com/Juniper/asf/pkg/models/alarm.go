package models

import (
	"encoding/json"

	"github.com/pkg/errors"
)

// Validate validates AlarmRules structure and values
func (alarm *Alarm) Validate() error {

	if err := alarm.validateAlarmRules(); err != nil {
		return err
	}

	if err := alarm.validateAlarmExpressions(); err != nil {
		return err
	}

	return nil
}

func (alarm *Alarm) validateAlarmRules() error {
	rules := alarm.GetAlarmRules()
	if rules == nil {
		return errors.New("field AlarmRules cannot be nil")
	}

	orList := rules.OrList
	if orList == nil {
		return errors.New("field OrList cannot be nil")
	}

	for _, andList := range orList {
		if andList == nil {
			return errors.New("field AndList cannot be nil")
		}
		for _, andCond := range andList.AndList {
			if andCond.Operand2 == nil {
				return errors.New("field Operand2 cannot be nil")
			}
		}
	}

	return nil
}

func (alarm *Alarm) validateAlarmExpressions() error {
	for _, andList := range alarm.GetAlarmRules().OrList {
		for _, andCond := range andList.AndList {
			op := andCond.Operand2
			if op.JSONValue != "" {
				if op.UveAttribute != "" {
					return errors.New("field Operand2 should have JSONValue or UveAttribute filled, not both")
				}
				if err := validateJSONValue(andCond.Operation, op.JSONValue); err != nil {
					return err
				}

			} else if op.UveAttribute == "" {
				return errors.New("field Operand2 should have JSONValue or UveAttribute filled")
			}
		}
	}

	return nil
}

func validateJSONValue(operation, value string) error {
	if operation == "range" {
		var parsed []int
		if err := json.Unmarshal([]byte(value), &parsed); err != nil {
			return errors.Errorf("couldn't parse JSONValue: %v", err)
		}

		if len(parsed) != 2 || parsed[0] >= parsed[1] {
			return errors.New("field JSONValue should be 2 element integer array [x,y] where x<y")
		}

	} else {
		var parsed interface{}
		if err := json.Unmarshal([]byte(value), &parsed); err != nil {
			return errors.Errorf("couldn't parse JSONValue: %v", err)
		}
	}

	return nil
}
