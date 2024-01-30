# Makefile for fdrtool

# Directory where fdr_logs_schema exists
PROTO_DIR := ..
VPATH= $(PROTO_DIR)

.PHONY: fdrtool clean

fdrtool: fdrtool.py

clean:
	@echo "Cleaning up..."
	rm -f fdrtool
	rm -f protoc_middleman fdr_logs_schema_pb2.py
	rm -f *.pyc
	rm -r fdr_logs
	rm -r fdr_logs_db
	rm -rf dist build fdrtool.egg-info
	rm -rf __pycache__
	rm -f warning.txt


protoc_middleman: fdr_logs_schema.proto
	#protoc --proto_path=$(PROTO_DIR) --python_out=. $(PROTO_DIR)/fdr_logs_schema.proto
	protoc --proto_path=.. --python_out=. --experimental_allow_proto3_optional ../fdr_logs_schema.proto
	@touch protoc_middleman

fdrtool: fdrtool.py protoc_middleman
	@echo "Writing shortcut script fdrtool..."
	@echo '#! /bin/sh' > fdrtool
	@echo './fdrtool.py "$$@"' >> fdrtool
	@chmod +x fdrtool

install: all
	@echo "Building fdrtool..."
	python3 setup.py sdist
	python3 setup.py bdist_wheel
	@cd dist

.PHONY: all clean install
