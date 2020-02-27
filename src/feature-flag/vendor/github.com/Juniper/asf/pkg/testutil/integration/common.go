package integration

// TODO(Michał): Split this file and refactor some functions.

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/Juniper/asf/pkg/client"
	"github.com/Juniper/asf/pkg/fileutil"
	"github.com/Juniper/asf/pkg/format"
	"github.com/Juniper/asf/pkg/keystone"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/Juniper/asf/pkg/models/basemodels"
	"github.com/Juniper/asf/pkg/services/baseservices"
	"github.com/Juniper/asf/pkg/sync"
	"github.com/Juniper/asf/pkg/testutil"
	"github.com/coreos/etcd/clientv3"
	"github.com/flosch/pongo2"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
	"github.com/stretchr/testify/assert"
	"github.com/stretchr/testify/require"

	integrationetcd "github.com/Juniper/asf/pkg/testutil/integration/etcd"
	yaml "gopkg.in/yaml.v2"
)

// Integration tests constants.
const (
	collectTimeout  = 5 * time.Second
	DefaultClientID = "default"
)

// TestMain is a function that can be called inside package specific TestMain
// to enable integration testing capabilities.
func TestMain(m *testing.M, s **APIServer) {
	WithViperConfig(func() {
		cacheDB, cancelEtcdEventProducer, err := RunCacheDB()
		if err != nil {
			logutil.FatalWithStackTrace(errors.Wrap(err, "failed to run Cache DB"))
		}
		defer testutil.LogFatalIfError(cancelEtcdEventProducer)

		if viper.GetBool("sync.enabled") {
			sync, err := sync.NewService()
			if err != nil {
				logutil.FatalWithStackTrace(errors.Wrap(err, "failed to initialize Sync"))
			}
			errChan := RunConcurrently(sync)
			defer CloseFatalIfError(sync, errChan)
			<-sync.DumpDone()
		}

		if srv, err := NewRunningServer(&APIServerConfig{
			RepoRootPath:       "../../..",
			EnableEtcdNotifier: true,
			CacheDB:            cacheDB,
		}); err != nil {
			logutil.FatalWithStackTrace(errors.Wrap(err, "failed to initialize API Server"))
		} else {
			*s = srv
		}
		defer testutil.LogFatalIfError((*s).Close)

		if code := m.Run(); code != 0 {
			os.Exit(code)
		}
	})
}

// RunTestFromTestsDirectory invokes integration test located in "tests" directory.
func RunTestFromTestsDirectory(t *testing.T, name string, server *APIServer) {
	ts, err := LoadTest(fmt.Sprintf("./tests/%s.yml", format.CamelToSnake(name)), nil)
	require.NoError(t, err, "failed to load test data")
	RunCleanTestScenario(t, ts, server)
}

// RunTest invokes integration test located in given file.
func RunTest(t *testing.T, file string, server *APIServer) {
	ts, err := LoadTest(file, nil)
	require.NoError(t, err)
	RunCleanTestScenario(t, ts, server)
}

// RunTestTemplate invokes integration test from template located in given file.
func RunTestTemplate(t *testing.T, file string, server *APIServer, context map[string]interface{}) {
	ts, err := LoadTest(file, context)
	require.NoError(t, err)
	RunCleanTestScenario(t, ts, server)
}

// WithViperConfig initializes Viper configuration and logging and runs given test function.
func WithViperConfig(f func()) {
	if err := initViperConfig(); err != nil {
		logutil.FatalWithStackTrace(errors.Wrap(err, "failed to initialize Viper config"))
	}
	if err := logutil.Configure(viper.GetString("log_level")); err != nil {
		logutil.FatalWithStackTrace(err)
	}

	logrus.Info("Starting integration tests")
	f()
	logrus.Info("Finished integration tests")
}

func initViperConfig() error {
	viper.SetConfigName("test_config")
	viper.SetConfigType("yml")
	viper.AddConfigPath(".")
	viper.AddConfigPath("../../../../sample")
	viper.AddConfigPath("../../../sample")
	viper.AddConfigPath("../../sample")
	viper.AddConfigPath("../sample")
	viper.AddConfigPath("./sample")
	viper.AddConfigPath("./test_data")
	viper.SetEnvPrefix("contrail")
	viper.SetEnvKeyReplacer(strings.NewReplacer(".", "_"))
	viper.AutomaticEnv()
	return viper.ReadInConfig()
}

// Event represents event received from etcd watch.
type Event struct {
	Data     map[string]interface{} `yaml:"data,omitempty"`
	SyncOnly bool                   `yaml:"sync_only,omitempty"`
}

func convertEventsIntoMapList(events []Event) []map[string]interface{} {
	m := make([]map[string]interface{}, len(events))
	for i := range events {
		m[i] = events[i].Data
	}
	return m
}

// Watchers map contains slices of events that should be emitted on
// etcd key matching the map key.
type Watchers map[string][]Event

// Waiters map contains slices of events that have to be emitted
// during single task.
type Waiters map[string][]Event

//Task has API request and expected response.
type Task struct {
	Name     string          `yaml:"name,omitempty"`
	Client   string          `yaml:"client,omitempty"`
	Request  *client.Request `yaml:"request,omitempty"`
	Expect   interface{}     `yaml:"expect,omitempty"`
	Watchers Watchers        `yaml:"watchers,omitempty"`
	Waiters  Waiters         `yaml:"await,omitempty"`
}

// CleanTask defines clean task
type CleanTask struct {
	Client string   `yaml:"client,omitempty"`
	Path   string   `yaml:"path,omitempty"`
	FQName []string `yaml:"fq_name,omitempty"`
	Kind   string   `yaml:"kind,omitempty"`
}

// TestScenario defines integration test scenario.
type TestScenario struct {
	Name                  string                        `yaml:"name,omitempty"`
	Description           string                        `yaml:"description,omitempty"`
	IntentCompilerEnabled bool                          `yaml:"intent_compiler_enabled,omitempty"`
	Tables                []string                      `yaml:"tables,omitempty"`
	ClientConfigs         map[string]*client.HTTPConfig `yaml:"clients,omitempty"`
	Clients               ClientsList                   `yaml:"-"`
	CleanTasks            []CleanTask                   `yaml:"cleanup,omitempty"`
	Workflow              []*Task                       `yaml:"workflow,omitempty"`
	Watchers              Watchers                      `yaml:"watchers,omitempty"`
	TestData              interface{}                   `yaml:"test_data,omitempty"`
}

// ClientsList is the list of clients used in test
type ClientsList map[string]*client.HTTP

// LoadTest loads test scenario from given file.
func LoadTest(file string, ctx map[string]interface{}) (*TestScenario, error) {
	template, err := pongo2.FromFile(file)
	if err != nil {
		return nil, errors.Wrap(err, "failed to read test data template")
	}

	content, err := template.ExecuteBytes(ctx)
	if err != nil {
		return nil, errors.Wrap(err, "failed to apply test data template")
	}

	ts := TestScenario{Clients: ClientsList{}}
	if err = yaml.UnmarshalStrict(content, &ts); err != nil {
		return nil, errors.Wrapf(err, "failed to unmarshal test scenario %q", file)
	}

	return &ts, nil
}

type trackedResource struct {
	Path   string
	Client string
}

// RunCleanTestScenario runs test scenario from loaded yaml file, expects no resources leftovers
func RunCleanTestScenario(
	t *testing.T,
	ts *TestScenario,
	server *APIServer,
) {
	logrus.WithField("test-scenario", ts.Name).Debug("Running clean test scenario")
	checkWatchers := StartWatchers(t, ts.Name, ts.Watchers)

	ctx := context.Background()
	clients := PrepareClients(ctx, t, ts, server)
	tracked := runTestScenario(ctx, t, ts, clients, server.APIServer.DBService)
	cleanupTrackedResources(ctx, tracked, clients)

	checkWatchers(t)
}

// RunDirtyTestScenario runs test scenario from loaded yaml file, leaves all resources after scenario
func RunDirtyTestScenario(t *testing.T, ts *TestScenario, server *APIServer) func() {
	logrus.WithField("test-scenario", ts.Name).Debug("Running dirty test scenario")
	ctx := context.Background()
	clients := PrepareClients(ctx, t, ts, server)
	tracked := runTestScenario(ctx, t, ts, clients, server.APIServer.DBService)
	cleanupFunc := func() {
		cleanupTrackedResources(ctx, tracked, clients)
	}
	return cleanupFunc
}

func cleanupTrackedResources(ctx context.Context, tracked []trackedResource, clients map[string]*client.HTTP) {
	for _, tr := range tracked {
		response, err := clients[tr.Client].EnsureDeleted(ctx, tr.Path, nil)
		if err != nil {
			logrus.WithError(err).WithFields(logrus.Fields{
				"url-path":    tr.Path,
				"http-client": tr.Client,
			}).Warn("Deleting dirty resource failed - ignoring")
		}
		if response.StatusCode == http.StatusOK {
			logrus.WithFields(logrus.Fields{
				"url-path":    tr.Path,
				"http-client": tr.Client,
			}).Warn("Test scenario has not deleted resource but should have deleted - test scenario is dirty")
		}
	}
}

// StartWaiters checks if there are emitted events described before.
func StartWaiters(t *testing.T, task string, waiters Waiters) func(t *testing.T) {
	checks := []func(t *testing.T){}

	ec := integrationetcd.NewEtcdClient(t)

	for key := range waiters {
		events := waiters[key]
		getMissingEvents := ec.WaitForEvents(
			key, convertEventsIntoMapList(events), collectTimeout, clientv3.WithPrefix(),
		)
		checks = append(checks, createWaiterChecker(task, getMissingEvents))
	}

	return func(t *testing.T) {
		defer ec.Close(t)
		for _, c := range checks {
			c(t)
		}
	}
}

func createWaiterChecker(
	task string, getMissingEvents func() ([]map[string]interface{}, error),
) func(t *testing.T) {
	return func(t *testing.T) {
		notFound, err := getMissingEvents()
		assert.Equal(t, nil, err, "waiter for task %v got an error while collecting events: %v", task, err)
		assert.Equal(t, 0, len(notFound), "etcd didn't emitted events %v for task %v", notFound, task)
	}
}

// StartWatchers checks if events emitted to etcd match those given in watchers dict.
func StartWatchers(t *testing.T, task string, watchers Watchers, opts ...clientv3.OpOption) func(t *testing.T) {
	checks := []func(t *testing.T){}

	ec := integrationetcd.NewEtcdClient(t)

	syncEnabled := viper.GetBool("sync.enabled")
	for key := range watchers {
		events := filterEvents(watchers[key], func(e Event) bool {
			if e.SyncOnly {
				return syncEnabled
			}
			return true
		})
		collect := ec.WatchKeyN(key, len(events), collectTimeout, append(opts, clientv3.WithPrefix())...)
		checks = append(checks, createWatchChecker(task, collect, key, events))
	}

	return func(t *testing.T) {
		defer ec.Close(t)
		for _, c := range checks {
			c(t)
		}
	}
}

func filterEvents(evs []Event, pred func(Event) bool) []Event {
	result := make([]Event, 0, len(evs))
	for _, e := range evs {
		if pred(e) {
			result = append(result, e)
		}
	}
	return result
}

func createWatchChecker(task string, collect func() []string, key string, events []Event) func(t *testing.T) {
	return func(t *testing.T) {
		collected := collect()
		eventCount := len(events)
		assert.Equal(
			t, eventCount, len(collected),
			"etcd emitted not enough events on %s(got %v, expected %v)\n",
			key, collected, events,
		)

		for i, e := range events[:len(collected)] {
			c := collected[i]
			var data interface{} = map[string]interface{}{}
			if len(c) > 0 {
				d := json.NewDecoder(bytes.NewReader([]byte(c)))
				d.UseNumber()
				err := d.Decode(&data)
				assert.NoError(t, err)
			}
			testutil.AssertEqual(t, e.Data, data, fmt.Sprintf("task: %s\netcd event not equal for %s[%v]", task, key, i))
		}
	}
}

// PrepareClients creates HTTP clients based on given configurations and logs them in if needed.
// It assigns created clients to given test scenario.
func PrepareClients(ctx context.Context, t *testing.T, ts *TestScenario, server *APIServer) ClientsList {
	for k, c := range ts.ClientConfigs {
		ts.Clients[k] = client.NewHTTP(&client.HTTPConfig{
			ID:       c.ID,
			Password: c.Password,
			Endpoint: server.URL(),
			AuthURL:  server.URL() + keystone.LocalAuthPath,
			Scope:    c.Scope,
			Insecure: c.Insecure,
		})

		if c.ID != "" {
			_, err := ts.Clients[k].Login(ctx)
			assert.NoError(t, err, fmt.Sprintf("client %q failed to login", c.ID))
		}
	}

	return ts.Clients
}

func runTestScenario(
	ctx context.Context,
	t *testing.T,
	ts *TestScenario,
	clients ClientsList,
	m baseservices.MetadataGetter,
) (tracked []trackedResource) {
	for _, cleanTask := range ts.CleanTasks {
		logrus.WithFields(logrus.Fields{
			"test-scenario": ts.Name,
			"clean-task":    cleanTask,
		}).Debug("Deleting existing resources before test scenario workflow")
		err := performCleanup(ctx, cleanTask, getClientByID(cleanTask.Client, clients), m)
		if err != nil {
			logrus.WithError(err).WithFields(logrus.Fields{
				"test-scenario": ts.Name,
				"clean-task":    cleanTask,
			}).Warn("Failed to delete existing resource before running workflow - ignoring")
		}
	}
	for _, task := range ts.Workflow {
		logrus.WithFields(logrus.Fields{
			"test-scenario": ts.Name,
			"task":          task.Name,
		}).Info("Starting task")
		checkWatchers := StartWatchers(t, task.Name, task.Watchers)
		checkWaiters := StartWaiters(t, ts.Name, task.Waiters)
		task.Request.Data = fileutil.YAMLtoJSONCompat(task.Request.Data)
		clientID := DefaultClientID
		if task.Client != "" {
			clientID = task.Client
		}
		client, ok := clients[clientID]
		if !assert.True(t, ok,
			"Client %q not defined in task %q of scenario %q", clientID, task.Name, ts.Name) {
			break
		}
		response, err := client.DoRequest(ctx, task.Request)
		assert.NoError(
			t,
			err,
			fmt.Sprintf("HTTP request failed in task %q of scenario %q", task.Name, ts.Name),
		)
		tracked = handleTestResponse(task, response.StatusCode, err, tracked)

		task.Expect = fileutil.YAMLtoJSONCompat(task.Expect)
		ok = testutil.AssertEqual(
			t,
			task.Expect,
			task.Request.Output,
			fmt.Sprintf("Invalid response body in task %q of scenario %q", task.Name, ts.Name),
		)
		checkWatchers(t)
		checkWaiters(t)
		if !ok {
			break
		}
	}
	// Reverse the order in tracked array so delete of nested resources is possible
	// https://github.com/golang/go/wiki/SliceTricks#reversing
	for left, right := 0, len(tracked)-1; left < right; left, right = left+1, right-1 {
		tracked[left], tracked[right] = tracked[right], tracked[left]
	}
	return tracked
}

func performCleanup(
	ctx context.Context,
	cleanTask CleanTask,
	client *client.HTTP,
	m baseservices.MetadataGetter,
) error {
	switch {
	case client == nil:
		return fmt.Errorf("failed to delete resource, got nil http client")
	case cleanTask.Path != "":
		return cleanPath(ctx, cleanTask.Path, client)
	case cleanTask.Kind != "" && len(cleanTask.FQName) > 0:
		return cleanByFQNameAndKind(ctx, cleanTask.FQName, cleanTask.Kind, client, m)
	default:
		return fmt.Errorf("invalid clean task %v", cleanTask)
	}
}

func getClientByID(clientID string, clients ClientsList) *client.HTTP {
	if clientID == "" {
		clientID = DefaultClientID
	}
	return clients[clientID]
}

func cleanPath(ctx context.Context, path string, client *client.HTTP) error {
	response, err := client.EnsureDeleted(ctx, path, nil)
	if err != nil && response.StatusCode != http.StatusNotFound {
		return errors.Wrapf(err, "failed to delete resource, got status code %v", response.StatusCode)
	}
	return nil
}

func cleanByFQNameAndKind(
	ctx context.Context,
	fqName []string,
	kind string,
	client *client.HTTP,
	m baseservices.MetadataGetter,
) error {
	metadata, err := m.GetMetadata(ctx, basemodels.Metadata{
		Type:   kind,
		FQName: fqName,
	})
	if err != nil {
		return errors.Wrapf(err, "failed to fetch uuid for %s with fqName %s", kind, fqName)
	}
	return cleanPath(ctx, "/"+kind+"/"+metadata.UUID, client)
}

func extractResourcePathFromJSON(data interface{}) (path string) {
	if data, ok := data.(map[string]interface{}); ok {
		var uuid interface{}
		if uuid, ok = data["uuid"]; !ok {
			return path
		}
		if suuid, ok := uuid.(string); ok {
			path = "/" + suuid
		}
	}
	return path
}

func extractSyncOperation(syncOp map[string]interface{}, client string) []trackedResource {
	resources := []trackedResource{}
	var operIf, kindIf interface{}
	var ok bool
	if kindIf, ok = syncOp["kind"]; !ok {
		return nil
	}
	if operIf, ok = syncOp["operation"]; !ok {
		return nil
	}
	var oper, kind string
	if oper, ok = operIf.(string); !ok {
		return nil
	}
	if oper != "CREATE" {
		return nil
	}
	if kind, ok = kindIf.(string); !ok {
		return nil
	}
	if dataIf, ok := syncOp["data"]; ok {
		if path := extractResourcePathFromJSON(dataIf); path != "" {
			return append(resources, trackedResource{Path: "/" + kind + path, Client: client})
		}
	}

	return resources
}

func handleTestResponse(task *Task, code int, rerr error, tracked []trackedResource) []trackedResource {
	if task.Request.Output != nil && task.Request.Method == "POST" && code == http.StatusOK && rerr == nil {
		clientID := DefaultClientID
		if task.Client != "" {
			clientID = task.Client
		}
		tracked = trackResponse(task.Request.Output, clientID, tracked)
	}
	return tracked
}

func trackResponse(respDataIf interface{}, clientID string, tracked []trackedResource) []trackedResource {
	switch respData := respDataIf.(type) {
	case []interface{}:
		logrus.Warn("After-workflow Sync request cleanup is not supported")
		for _, syncOpIf := range respData {
			if syncOp, ok := syncOpIf.(map[string]interface{}); ok {
				tracked = append(tracked, extractSyncOperation(syncOp, clientID)...)
			}
		}
	case map[string]interface{}:
		for k, v := range respData {
			if path := extractResourcePathFromJSON(v); path != "" {
				tracked = append(tracked, trackedResource{Path: "/" + k + path, Client: clientID})
			}
		}
	}
	return tracked
}
