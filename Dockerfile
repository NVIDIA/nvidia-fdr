# Use the official Debian image as the base image
FROM debian:12.4

# Set the working directory
WORKDIR /app

# Install necessary dependencies
RUN apt-get update && \
    apt-get install -y \
    git \
    python3 \
    python3-pip \
    build-essential \
    sqlite3 \
    virtualenv 
 


# Copy fdrtool to container
COPY fdrtool /app/fdrtool
COPY fdr_logs_schema.proto /app


# Change to the cloned directory
WORKDIR /app/fdrtool


# Make a virtual environment and install the dependencies
RUN virtualenv env_fdr 
RUN /bin/bash -c "source /app/fdrtool/env_fdr/bin/activate && pip3 install -r requirements.txt"


# Build the tool using the Makefile
RUN make fdrtool

