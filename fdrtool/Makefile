# Makefile for fdrtool

# Directory where fdr_logs_schema exists
PROTO_DIR := ..
VPATH= $(PROTO_DIR)

.PHONY: fdrtool clean

fdrtool: fdrtool.py

clean:
	rm -f fdrtool
	rm -f protoc_middleman fdr_logs_schema_pb2.py
	rm -f *.pyc
	rm -r fdr_logs
	rm -r fdr_logs_db

protoc_middleman: fdr_logs_schema.proto
	protoc --proto_path=$(PROTO_DIR) --python_out=. $(PROTO_DIR)/fdr_logs_schema.proto
	@touch protoc_middleman

fdrtool: fdrtool.py protoc_middleman
	@echo "Writing shortcut script fdrtool..."
	@echo '#! /bin/sh' > fdrtool
	@echo './fdrtool.py "$$@"' >> fdrtool
	@chmod +x fdrtool
