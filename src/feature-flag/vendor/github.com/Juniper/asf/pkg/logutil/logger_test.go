package logutil

import (
	"os"
	"testing"

	"github.com/sirupsen/logrus"
	"github.com/stretchr/testify/assert"
)

func TestConfigureFailsWhenInvalidLevelGiven(t *testing.T) {
	tests := []struct {
		level string
	}{
		{"invalid"},
		{"warn "},
	}
	for _, test := range tests {
		t.Run(test.level, func(t *testing.T) {
			err := Configure(test.level)

			assert.Error(t, err)
		})
	}
}

func TestConfigureSetsMinimalLevelPackageVariable(t *testing.T) {
	tests := []struct {
		level string
		out   logrus.Level
	}{
		{"debug", logrus.DebugLevel},
		{"info", logrus.InfoLevel},
		{"warn", logrus.WarnLevel},
		{"error", logrus.ErrorLevel},
		{"InFo", logrus.InfoLevel},
		{"", logrus.DebugLevel},
	}
	for _, test := range tests {
		t.Run(test.level, func(t *testing.T) {
			err := Configure(test.level)

			assert.Nil(t, err)
			assert.Equal(t, test.out, minimalLevel)
		})
	}
}

func TestConfigureConfiguresGlobalLogger(t *testing.T) {
	globalLogger := logrus.StandardLogger()

	err := Configure("warn")

	assert.Nil(t, err)
	assert.Equal(t, os.Stdout, globalLogger.Out)
	assert.Equal(t, &logrus.TextFormatter{}, globalLogger.Formatter)
	assert.Equal(t, logrus.WarnLevel, globalLogger.Level)
}

func TestNewLoggerContainsLoggerName(t *testing.T) {
	l := NewLogger("test-logger")

	assert.Equal(t, "test-logger", l.Data[loggerKey])
}
