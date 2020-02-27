all: deps generate testenv test

deps:
	./tools/install_proto.sh

generate:
	go generate ./...

testenv:
	docker-compose up -d

# TODO(dfurman): use go test ./...
# TODO(dfurman): remove or fix the package compilation and tests for following packages:
# cmd/contrailcli
# cmd/contrailutil
# pkg/bench
# pkg/cache
# pkg/cassandra
# pkg/cmd/contrailcli
# pkg/cmd/contrailutil
# pkg/convert
# pkg/db
# pkg/endpoint
# pkg/models
# pkg/rbac TODO(dfurman): this definitely needs to be fixed
# pkg/services
# pkg/sync
# pkg/sync/replication
# pkg/testutil/integration
# pkg/testutil/integration/etcd

TESTPKGS=./cmd/contrailschema \
	./pkg/apiserver \
	./pkg/auth \
	./pkg/client \
	./pkg/cmd/contrailschema \
	./pkg/config \
	./pkg/db/basedb \
	./pkg/errutil \
	./pkg/etcd \
	./pkg/fileutil \
	./pkg/fileutil/template \
	./pkg/format \
	./pkg/httputil \
	./pkg/keystone \
	./pkg/logutil \
	./pkg/models/basemodels \
	./pkg/osutil \
	./pkg/proxy \
	./pkg/retry \
	./pkg/schema \
	./pkg/services/baseservices \
	./pkg/testutil

test:
	# TODO(mblotniak): After fixing the package compilation and tests please add it to $$TESTPKGS above.
	go test $(TESTPKGS)

clean:
	docker-compose down

.PHONY: test generate
