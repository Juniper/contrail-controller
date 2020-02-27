package contrailutil

import (
	"context"
	"fmt"

	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"

	"github.com/Juniper/asf/pkg/client"
	"github.com/Juniper/asf/pkg/fileutil"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/Juniper/asf/pkg/testutil/integration"
)

var inputPath string
var outputPath string
var variablePath string
var endpoint string
var authURL string

func init() {
	ContrailUtil.AddCommand(recordTestCmd)
	recordTestCmd.Flags().StringVarP(&inputPath, "input", "i", "", "Input test scenario path")
	recordTestCmd.Flags().StringVarP(&variablePath, "vars", "v", "", "test variables")
	recordTestCmd.Flags().StringVarP(&outputPath, "output", "o", "", "Output test scenario path")
	recordTestCmd.Flags().StringVarP(&endpoint, "endpoint", "e", "", "Endpoint")
	recordTestCmd.Flags().StringVarP(&authURL, "auth_url", "a", "", "AuthURL")
}

func assertError(err error, message string) {
	if err != nil {
		logutil.FatalWithStackTrace(errors.Wrapf(err, "%s (%s)", message, err))
	}
}

func recordTest() {
	ctx := context.Background()
	logrus.Info("Recording API behavior")
	var vars map[string]interface{}
	if variablePath != "" {
		err := fileutil.LoadFile(variablePath, &vars)
		if err != nil {
			logutil.FatalWithStackTrace(err)
		}
	}

	testScenario, err := integration.LoadTest(inputPath, vars)
	assertError(err, "failed to load test scenario")

	clients := map[string]*client.HTTP{}
	for key := range testScenario.Clients {
		clients[key] = client.NewHTTP(&client.HTTPConfig{
			Endpoint: endpoint,
			AuthURL:  authURL,
			Insecure: true,
		})

		_, err = clients[key].Login(ctx)
		assertError(err, "client can't login")
	}

	for _, task := range testScenario.Workflow {
		logrus.Debug("[Step] ", task.Name)
		task.Request.Data = fileutil.YAMLtoJSONCompat(task.Request.Data)
		clientID := "default"
		if task.Client != "" {
			clientID = task.Client
		}

		_, err = clients[clientID].DoRequest(ctx, task.Request)
		assertError(err, fmt.Sprintf("Task %s failed", task.Name))
		task.Expect = task.Request.Output
		task.Request.Output = nil
	}

	err = fileutil.SaveFile(outputPath, testScenario)
	if err != nil {
		logutil.FatalWithStackTrace(err)
	}
}

var recordTestCmd = &cobra.Command{
	Use:   "record_test",
	Short: "Record test result",
	Long:  `Run test scenario and save result to file`,
	Run: func(cmd *cobra.Command, args []string) {
		recordTest()
	},
}
