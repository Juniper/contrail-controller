BUILD_DIR := ../build
GENERATE_DS_REPO := contrail-api-client
GO_API_CLIENT_REPO := contrail-go-api
GO_API_CLIENT_VENDOR := ./vendor/github.com/Juniper/$(GO_API_CLIENT_REPO)/

GENERATE_DS_REPO_DIR ?= ""
GENERATE_DS_BRANCH ?= master
GENERATE_DS_REVISION ?= HEAD

GO_API_CLIENT_REPO_DIR ?= ""
GO_API_CLIENT_BRANCH ?= master
GO_API_CLIENT_REVISION ?= HEAD

BASE_IMAGE_REGISTRY ?= opencontrailnightly
BASE_IMAGE_REPOSITORY ?= contrail-base
BASE_IMAGE_TAG ?= latest

GOPATH ?= `go env GOPATH`
SOURCEDIR ?= $(GOPATH)
DOCKER_FILE := $(BUILD_DIR)/docker/Dockerfile

all: vendor generate install test lint ## Perform all checks

vendor: ## Ensure vendor dependencies
	GO111MODULE=on go mod vendor

generateds_prepare: ## Prepare generateDS repo
	rm -rf $(BUILD_DIR)/$(GENERATE_DS_REPO)
ifeq ($(GENERATE_DS_REPO_DIR),"")
		git clone -b $(GENERATE_DS_BRANCH) https://github.com/Juniper/$(GENERATE_DS_REPO).git $(BUILD_DIR)/$(GENERATE_DS_REPO)
		cd $(BUILD_DIR)/$(GENERATE_DS_REPO) && git checkout $(GENERATE_DS_REVISION)
else
		cp -r $(GENERATE_DS_REPO_DIR) $(BUILD_DIR)/$(GENERATE_DS_REPO)
endif

goapi_client_prepare: ## Prepare go-api client repo
	rm -rf $(BUILD_DIR)/$(GO_API_CLIENT_REPO)
ifeq ($(GO_API_CLIENT_REPO_DIR),"")
		git clone -b $(GO_API_CLIENT_BRANCH) https://github.com/Juniper/$(GO_API_CLIENT_REPO).git $(BUILD_DIR)/$(GO_API_CLIENT_REPO)
		cd $(BUILD_DIR)/$(GO_API_CLIENT_REPO) && git checkout $(GO_API_CLIENT_REVISION)
else
		cp -r $(GO_API_CLIENT_REPO_DIR) $(BUILD_DIR)/$(GO_API_CLIENT_REPO)
endif

generate_client:
	rm -rf $(GO_API_CLIENT_VENDOR)/
	$(BUILD_DIR)/$(GENERATE_DS_REPO)/generateds/generateDS.py -f -o $(BUILD_DIR)/$(GO_API_CLIENT_REPO)/types -g golang-api $(BUILD_DIR)/$(GENERATE_DS_REPO)/schema/all_cfg.xsd
	mkdir -p $(GO_API_CLIENT_VENDOR)
	cp -r $(BUILD_DIR)/$(GO_API_CLIENT_REPO)/* $(GO_API_CLIENT_VENDOR)/

generate: generateds_prepare goapi_client_prepare generate_client

install:
	go install ./cmd/contrailflcm

test:

lint:

fmt:
	gofiles=$(find . -type f -name '*.go' -not -path "./vendor/*" -not -path "*gen*")
	goimports -v -w ${gofiles}
	gofmt -s -w ${gofiles}

