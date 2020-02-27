module github.com/Juniper/asf

go 1.13

require (
	github.com/ExpansiveWorlds/instrumentedsql v0.0.0-20171218214018-45abb4b1947d
	github.com/NYTimes/gziphandler v1.1.1 // indirect
	github.com/apparentlymart/go-cidr v1.0.1
	github.com/cockroachdb/apd v1.1.0 // indirect
	github.com/codegangsta/cli v1.20.0 // indirect
	github.com/coreos/bbolt v1.3.3 // indirect
	github.com/coreos/etcd v3.3.2+incompatible
	github.com/coreos/go-semver v0.3.0 // indirect
	github.com/coreos/go-systemd v0.0.0-20191104093116-d3cd4ed1dbcf // indirect
	github.com/coreos/pkg v0.0.0-20180928190104-399ea9e2e55f // indirect
	github.com/databus23/keystone v0.0.0-20180111110916-350fd0e663cd
	github.com/dgrijalva/jwt-go v3.2.0+incompatible // indirect
	github.com/dustin/go-humanize v1.0.0 // indirect
	github.com/elazarl/go-bindata-assetfs v1.0.0 // indirect
	github.com/fatih/structs v1.1.0 // indirect
	github.com/flosch/pongo2 v0.0.0-20190707114632-bbf5a6c351f4
	github.com/go-openapi/spec v0.19.3
	github.com/go-sql-driver/mysql v1.4.1 // indirect
	github.com/gogo/protobuf v1.3.1
	github.com/golang/groupcache v0.0.0-20200121045136-8c9f03a8e57e // indirect
	github.com/golang/mock v1.3.1
	github.com/google/btree v1.0.0 // indirect
	github.com/gorilla/websocket v1.4.1 // indirect
	github.com/grpc-ecosystem/go-grpc-prometheus v1.2.0 // indirect
	github.com/grpc-ecosystem/grpc-gateway v1.13.0 // indirect
	github.com/hashicorp/go-cleanhttp v0.5.1
	github.com/hashicorp/go-multierror v1.0.0 // indirect
	github.com/hashicorp/go-version v1.2.0 // indirect
	github.com/inconshreveable/mousetrap v1.0.0 // indirect
	github.com/jackc/fake v0.0.0-20150926172116-812a484cc733 // indirect
	github.com/jackc/pgx v3.1.0+incompatible
	github.com/jonboulle/clockwork v0.1.0 // indirect
	github.com/konsorten/go-windows-terminal-sequences v1.0.2 // indirect
	github.com/kyleconroy/pgoutput v0.0.0-20180223044356-3fd8343739da
	github.com/labstack/echo v3.3.10+incompatible
	github.com/labstack/gommon v0.3.0 // indirect
	github.com/lib/pq v0.0.0-20180319194135-b2004221932b
	github.com/mattn/go-shellwords v1.0.6
	github.com/mattn/go-sqlite3 v1.11.0 // indirect
	github.com/mitchellh/mapstructure v1.1.2 // indirect
	github.com/opentracing/opentracing-go v1.1.0 // indirect
	github.com/pkg/errors v0.9.1
	github.com/prometheus/client_golang v1.4.1 // indirect
	github.com/prometheus/procfs v0.0.10 // indirect
	github.com/satori/go.uuid v1.2.0
	github.com/shopspring/decimal v0.0.0-20191009025716-f1972eb1d1f5 // indirect
	github.com/sirupsen/logrus v1.4.2
	github.com/soheilhy/cmux v0.1.4 // indirect
	github.com/spf13/cast v1.3.0 // indirect
	github.com/spf13/cobra v0.0.3
	github.com/spf13/pflag v1.0.5 // indirect
	github.com/spf13/viper v1.2.1
	github.com/stretchr/testify v1.4.0
	github.com/tmc/grpc-websocket-proxy v0.0.0-20200122045848-3419fae592fc // indirect
	github.com/ugorji/go/codec v0.0.0-20181204163529-d75b2dcb6bc8 // indirect
	github.com/valyala/fasttemplate v1.1.0 // indirect
	github.com/volatiletech/inflect v0.0.0-20170731032912-e7201282ae8d // indirect
	github.com/volatiletech/sqlboiler v3.5.0+incompatible
	github.com/xiang90/probing v0.0.0-20190116061207-43a291ad63a2 // indirect
	github.com/yudai/gotty v2.0.0-alpha.3+incompatible
	github.com/yudai/hcl v0.0.0-20151013225006-5fa2393b3552 // indirect
	go.etcd.io/bbolt v1.3.2 // indirect
	go.opencensus.io v0.22.1 // indirect
	golang.org/x/crypto v0.0.0-20200221231518-2aa609cf4a9d // indirect
	golang.org/x/net v0.0.0-20200222125558-5a598a2470a0 // indirect
	golang.org/x/sys v0.0.0-20200223170610-d5e6a3e2c0ae // indirect
	golang.org/x/time v0.0.0-20191024005414-555d28b269f0 // indirect
	google.golang.org/api v0.10.0 // indirect
	google.golang.org/genproto v0.0.0-20200218151345-dad8c97a84f5 // indirect
	google.golang.org/grpc v1.27.1
	gopkg.in/yaml.v2 v2.2.8
)

// github.com/codegangsta/cli was moved, but github.com/yudai/gotty uses obsolete import path
replace github.com/codegangsta/cli v1.20.0 => github.com/urfave/cli v1.20.0

// workaround for https://github.com/gin-gonic/gin/issues/1673, etcd v3.2.2 dependency
replace github.com/ugorji/go v1.1.4 => github.com/ugorji/go v0.0.0-20181204163529-d75b2dcb6bc8
