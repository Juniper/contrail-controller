package models

import (
	"regexp"

	"github.com/pkg/errors"
)

// ValidateRegexps compiles user defined log statistics to check if they are valid regexps.
func (stats *UserDefinedLogStatList) ValidateRegexps() (err error) {
	for _, udc := range stats.Statlist {
		_, err = regexp.Compile(udc.Pattern)
		if err != nil {
			return errors.Wrap(err, "Compiling udc regexp failed")
		}
	}
	return nil
}
