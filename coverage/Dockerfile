FROM ubuntu:23.04

ENV DEBIAN_FRONTEND=noninteractive
RUN apt update && apt install -y git meson libtool pkg-config g++-12 libsystemd-dev \
    python3 python3-pip python3-yaml python3-mako python3-inflection cmake clang-tools nlohmann-json3-dev \
    sqlite3 libsqlite3-dev libyaml-cpp-dev libcurl4-gnutls-dev python3-yaml \
    libspdlog-dev libfmt-dev clang-tools googletest google-mock \
    python3 python3-pip python3-yaml python3-mako python3-inflection systemd gcovr lcov protobuf-compiler protobuf-c-compiler && rm -rf /var/lib/apt/lists/*

WORKDIR /nvidia-fdr
ENTRYPOINT ["bash", "-c"]
CMD ["./coverage/get_coverage.sh"]