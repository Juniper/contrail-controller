package osutil

import (
	"errors"
	"io"
	"os"
	"os/exec"
	"strings"

	"github.com/sirupsen/logrus"
)

type reporter interface {
	ReportLog(io.Reader)
}

// ExecCmdAndWait execs cmd, reports the stdout & stderr and waits for cmd to complete.
func ExecCmdAndWait(r reporter, cmd string, args []string, dir string, envVars ...string) error {
	cmdline := exec.Command(cmd, args...)
	if dir != "" {
		cmdline.Dir = dir
	}
	if len(envVars) != 0 {
		cmdline.Env = append(os.Environ(), envVars...)
	}

	return ExecAndWait(r, cmdline)
}

// ExecAndWait execs cmd, reports the stdout & stderr and waits for cmd to complete.
func ExecAndWait(r reporter, cmd *exec.Cmd) error {
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return err
	}
	if err := cmd.Start(); err != nil {
		return err
	}
	// Report progress log periodically to stdout/db
	go r.ReportLog(stdout)
	go r.ReportLog(stderr)

	return cmd.Wait()
}

// ForceRemoveFiles removes files.
func ForceRemoveFiles(files []string, log *logrus.Entry) error {
	log.Info("Removing credentials")
	unremovedFiles := []string{}
	for _, file := range files {
		if err := os.Remove(file); err == nil {
			log.Infof("Succesfully removed file: %s", file)
		} else if os.IsNotExist(err) {
			log.Infof("There is no such file as: %s", file)
		} else {
			log.Errorf("Could not remove file: %s", file)
			unremovedFiles = append(unremovedFiles, file)
		}
	}
	if len(unremovedFiles) != 0 {
		return errors.New("Removing credentials failed for files:" + strings.Join(unremovedFiles, ";") +
			"Please SSH on your machine and remove them manually otherwise they won't be removed at all!")
	}
	return nil
}
