package db

import (
	"database/sql"
	"os"
	"strings"
	"testing"

	"github.com/Juniper/asf/pkg/db/basedb"
	"github.com/Juniper/asf/pkg/logutil"
	"github.com/sirupsen/logrus"
	"github.com/spf13/viper"
)

var db *Service

func TestMain(m *testing.M) {
	viper.SetConfigType("yml")
	viper.SetConfigName("test_config")
	viper.AddConfigPath("../../sample")
	err := viper.ReadInConfig()
	if err != nil {
		logutil.FatalWithStackTrace(err)
	}
	viper.SetEnvPrefix("contrail")
	viper.SetEnvKeyReplacer(strings.NewReplacer(".", "_"))
	viper.AutomaticEnv()

	if err = logutil.Configure(viper.GetString("log_level")); err != nil {
		logutil.FatalWithStackTrace(err)
	}

	testDB, err := basedb.OpenConnection(basedb.ConnectionConfig{
		Driver:   basedb.DriverPostgreSQL,
		User:     viper.GetString("database.user"),
		Password: viper.GetString("database.password"),
		Host:     viper.GetString("database.host"),
		Name:     viper.GetString("database.name"),
		Debug:    viper.GetBool("database.debug"),
	})
	if err != nil {
		logutil.FatalWithStackTrace(err)
	}
	defer closeDB(testDB)

	db = &Service{
		BaseDB: basedb.NewBaseDB(testDB),
	}
	db.initQueryBuilders()

	logrus.Info("Starting integration tests")
	code := m.Run()
	logrus.Info("Finished integration tests")
	if code != 0 {
		os.Exit(code)
	}
}

func closeDB(db *sql.DB) {
	if err := db.Close(); err != nil {
		logutil.FatalWithStackTrace(err)
	}
}
