package retry

import (
	"errors"
	"testing"
)

func TestDo(t *testing.T) {
	sampleErr := errors.New("sample error")
	var runCounter int
	tests := []struct {
		name         string
		f            Func
		expectedRuns int
		mockLogger   *mockLogger
		wantErr      bool
	}{{
		name: "failing function",
		f: func() (bool, error) {
			return false, sampleErr
		},
		expectedRuns: 1,
		wantErr:      true,
	}, {
		name: "succeding function",
		f: func() (bool, error) {
			return false, nil
		},
		expectedRuns: 1,
	}, {
		name: "function succeeds after 3 times",
		f: func() (bool, error) {
			if runCounter < 3 {
				return true, sampleErr
			}
			return false, nil
		},
		expectedRuns: 3,
	}, {
		name: "function succeeds but wants to be run 4 times",
		f: func() (bool, error) {
			return runCounter < 4, nil
		},
		expectedRuns: 4,
	}, {
		name: "function succeeds after 3 times but wants to be run 4 times",
		f: func() (bool, error) {
			if runCounter < 3 {
				return true, sampleErr
			}
			return runCounter < 4, nil
		},
		expectedRuns: 4,
	}, {
		name: "function succeeds after 3 times with logger",
		f: func() (bool, error) {
			if runCounter < 3 {
				return true, sampleErr
			}
			return false, nil
		},
		mockLogger:   &mockLogger{expectedCalls: 2},
		expectedRuns: 3,
	}}
	for _, tt := range tests {
		t.Run(tt.name, func(t *testing.T) {
			runCounter = 0
			var opts []Option
			if tt.mockLogger != nil {
				opts = append(opts, WithLog(tt.mockLogger))
				defer tt.mockLogger.VerifyExpectations(t)
			}

			if err := Do(func() (bool, error) {
				runCounter++
				return tt.f()
			}, opts...); (err != nil) != tt.wantErr {
				t.Errorf("Do() error = %v, wantErr %v", err, tt.wantErr)
			}

			if runCounter > tt.expectedRuns {
				t.Errorf("function was called too many times, want %d, got %d", tt.expectedRuns, runCounter)
			} else if runCounter < tt.expectedRuns {
				t.Errorf("function was called not enough times, want %d, got %d", tt.expectedRuns, runCounter)
			}
		})
	}
}

type mockLogger struct {
	expectedCalls int
}

func (l *mockLogger) Debugf(string, ...interface{}) {
	l.expectedCalls--
}
func (l *mockLogger) VerifyExpectations(t *testing.T) {
	if l.expectedCalls < 0 {
		t.Errorf("logger was called %d times more than expected", -l.expectedCalls)
	} else if l.expectedCalls > 0 {
		t.Errorf("function was called %d times less than expected", l.expectedCalls)
	}
}
