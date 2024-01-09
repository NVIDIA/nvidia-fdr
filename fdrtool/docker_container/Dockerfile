# Use the official Debian image as the base image
FROM debian:latest

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
    virtualenv \
    protobuf-compiler
 

# Copy the SSH key to the image
ARG SSH_PRIVATE_KEY
RUN mkdir /root/.ssh/ && \
    echo "${SSH_PRIVATE_KEY}" > /root/.ssh/id_rsa && \
    chmod 600 /root/.ssh/id_rsa

# Update known hosts with GitLab's host key
RUN ssh-keyscan gitlab-master.nvidia.com >> /root/.ssh/known_hosts

# Clone the repository
RUN git clone ssh://git@gitlab-master.nvidia.com:12051/dgx/nvidia-fdr.git

# Change to the cloned directory
WORKDIR /app/nvidia-fdr/fdrtool




# Make a virtual environment and install the dependencies
RUN virtualenv env_fdr 
RUN /bin/bash -c "source /app/nvidia-fdr/fdrtool/env_fdr/bin/activate && pip3 install -r requirements.txt"


# Build the tool using the Makefile
RUN make fdrtool

