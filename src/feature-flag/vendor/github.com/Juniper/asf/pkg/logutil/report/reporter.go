package report

import (
	"bufio"
	"bytes"
	"context"
	"io"
	"net/http"

	"github.com/sirupsen/logrus"
)

type updater interface {
	Update(context.Context, string, interface{}, interface{}) (*http.Response, error)
}

// Reporter reports provisioing status and log
type Reporter struct {
	api      updater
	resource string
	log      *logrus.Entry
}

// NewReporter creates a reporter object
func NewReporter(apiServer updater, resource string, logger *logrus.Entry) *Reporter {
	return &Reporter{
		api:      apiServer,
		resource: resource,
		log:      logger,
	}
}

// ReportStatus reports status to a particular resource
func (r *Reporter) ReportStatus(ctx context.Context, status map[string]interface{}, resource string) {
	data := map[string]map[string]interface{}{resource: status}
	var response interface{}
	//TODO(nati) fixed context
	_, err := r.api.Update(ctx, r.resource, data, &response)
	if err != nil {
		r.log.Infof("update %s status failed: %s", resource, err)
	}
	r.log.Infof("%s status updated: %s", resource, status)
}

// ReportLog reports log
func (r *Reporter) ReportLog(stdio io.Reader) {
	var output bytes.Buffer
	scanner := bufio.NewScanner(stdio)
	for scanner.Scan() {
		m := scanner.Text()
		output.WriteString(m)
		r.log.Info(m)
	}
	//TODO(ijohnson) Implement status update to db
}
