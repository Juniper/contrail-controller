package contrailschema

import (
	"strings"

	"github.com/Juniper/asf/pkg/fileutil"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/Juniper/asf/pkg/schema"
	"github.com/pkg/errors"
	"github.com/sirupsen/logrus"
	"github.com/spf13/cobra"
	"github.com/spf13/viper"
)

type generateFlags struct {
	SchemasDir         string
	AddonsDir          string
	TemplateConfigPath string
	DBImportPath       string
	ETCDImportPath     string
	ModelsImportPath   string
	ServicesImportPath string
	SchemaOutputPath   string
	OpenAPIOutputPath  string
	NoRegenerate       bool
	SkipMissingRefs    bool
	Verbose            bool
}

var flags = generateFlags{}

func init() {
	viper.SetEnvPrefix("contrail")
	viper.SetEnvKeyReplacer(strings.NewReplacer(".", "_"))
	viper.AutomaticEnv()

	ContrailSchema.AddCommand(generateCmd)
	generateCmd.Flags().StringVarP(&flags.SchemasDir, "schemas", "s", "", "Schema Directory")
	generateCmd.Flags().StringVarP(&flags.AddonsDir, "addons", "a", "", "Addons Directory")
	generateCmd.Flags().StringVarP(
		&flags.TemplateConfigPath, "template-config", "t", "",
		"Path to a file with a list of templates to be applied",
	)
	// TODO(dfurman): provide a default so that specifying these paths is not required: use "go list -m" to determine
	// client's package path and concatenate subpackage path, e.g. /pkg/db.
	generateCmd.Flags().StringVarP(
		&flags.DBImportPath, "db-import-path", "", "",
		"Generated db import path, e.g. github.com/client/repository/pkg/db",
	)
	generateCmd.Flags().StringVarP(
		&flags.ETCDImportPath, "etcd-import-path", "", "",
		"Generated etcd import path, e.g. github.com/client/repository/pkg/etcd",
	)
	generateCmd.Flags().StringVarP(
		&flags.ModelsImportPath, "models-import-path", "", "",
		"Generated models import path, e.g. github.com/client/repository/pkg/models",
	)
	generateCmd.Flags().StringVarP(
		&flags.ServicesImportPath, "services-import-path", "", "",
		"Generated services import path, e.g. github.com/client/repository/pkg/services",
	)
	generateCmd.Flags().StringVarP(&flags.SchemaOutputPath, "schema-output", "", "", "Schema Output path")
	generateCmd.Flags().StringVarP(&flags.OpenAPIOutputPath, "openapi-output", "", "", "OpenAPI Output path")
	generateCmd.Flags().BoolVarP(
		&flags.NoRegenerate, "no-regenerate", "n", false,
		"Avoid regenerating file if it is newer that its source schema and template files",
	)
	generateCmd.Flags().BoolVarP(
		&flags.SkipMissingRefs, "skip-missing-refs", "", false,
		"Skip references that are missing instead of failing",
	)
	generateCmd.Flags().BoolVarP(&flags.Verbose, "verbose", "v", false, "Enable debug logging")
}

var generateCmd = &cobra.Command{
	Use:   "generate",
	Short: "generate code from schema",
	Long:  ``,
	Run: func(cmd *cobra.Command, args []string) {
		if err := logutil.Configure(getLogLevel()); err != nil {
			terminate(err)
		}

		if err := generateCode(); err != nil {
			terminate(err)
		}
	},
}

func getLogLevel() string {
	level := viper.GetString("log_level")
	if flags.Verbose {
		level = logrus.DebugLevel.String()
	}
	if level == "" {
		level = logrus.InfoLevel.String()
	}
	return level
}

func terminate(err error) {
	if logrus.GetLevel() == logrus.DebugLevel {
		logutil.FatalWithStackTrace(err)
	} else {
		logrus.Fatal(err)
	}
}

func generateCode() error {
	logrus.Info("Generating source code from schema")
	api, err := schema.MakeAPI(
		strings.Split(flags.SchemasDir, ","),
		strings.Split(flags.AddonsDir, ","),
		flags.SkipMissingRefs,
	)
	if err != nil {
		return errors.Wrap(err, "make API")
	}

	tcs, err := schema.LoadTemplateConfigs(flags.TemplateConfigPath)
	if err != nil {
		return errors.Wrap(err, "load template config")
	}

	if err = schema.GenerateFiles(api, &schema.GenerateConfig{
		TemplateConfigs:    tcs,
		DBImportPath:       flags.DBImportPath,
		ETCDImportPath:     flags.ETCDImportPath,
		ModelsImportPath:   flags.ModelsImportPath,
		ServicesImportPath: flags.ServicesImportPath,
		NoRegenerate:       flags.NoRegenerate,
	}); err != nil {
		return errors.Wrap(err, "generate files")
	}

	if flags.SchemaOutputPath != "" {
		if err = fileutil.SaveFile(flags.SchemaOutputPath, api); err != nil {
			return errors.Wrap(err, "save schema to file")
		}
	}

	if flags.OpenAPIOutputPath != "" {
		openAPI, err := api.ToOpenAPI()
		if err != nil {
			return errors.Wrap(err, "convert API to OpenAPI")
		}

		if err = fileutil.SaveFile(flags.OpenAPIOutputPath, openAPI); err != nil {
			return errors.Wrap(err, "save OpenAPI to file")
		}
	}
	return nil
}
