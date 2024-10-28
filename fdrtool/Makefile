# Makefile for fdrtool

# Directory where fdr_logs_schema exists
PROTO_DIR := ..
VPATH= $(PROTO_DIR)
CUSTOMER ?= 1

.PHONY: fdrtool clean

fdrtool: fdrtool.py

clean:
	@echo "Cleaning up..."
	rm -f fdrtool
	rm -f protoc_middleman fdr_logs_schema_pb2.py
	rm -f *.pyc
	rm -f check_fdr_telemetry_coverage.py
	rm -f telemetry_catalog.py
	rm -f data_validator.py
	rm -rf fdr_logs
	rm -rf fdr_logs_db
	rm -rf dist build fdrtool.egg-info
	rm -rf __pycache__
	rm -f warning.txt
	rm -f tc.log
	rm -f .customer_view


protoc_middleman: fdr_logs_schema.proto
	#protoc --proto_path=$(PROTO_DIR) --python_out=. $(PROTO_DIR)/fdr_logs_schema.proto
	protoc --proto_path=.. --python_out=. --experimental_allow_proto3_optional ../fdr_logs_schema.proto
	@touch protoc_middleman

fdrtool: fdrtool.py protoc_middleman
	@echo "Writing shortcut script fdrtool..."
	@echo '#! /bin/sh' > fdrtool
	@echo './fdrtool.py "$$@"' >> fdrtool
ifeq ($(CUSTOMER), 0)
	@cp ../tools/data_validation/check_fdr_telemetry_coverage.py .
	@cp ../tools/data_validation/telemetry_catalog.py .
	@cp ../tools/data_validation/data_validator.py .
	@echo "0" > .customer_view
else
	@echo "1" > .customer_view
	@gawk -i inplace 'NR < 33 || NR > 43' config.yaml
endif
	@chmod +x fdrtool

install: all
	@echo "Building fdrtool..."
	python3 setup.py sdist
	python3 setup.py bdist_wheel
	@cd dist

.PHONY: all clean install
