FROM ubuntu:24.04
ARG UNAME=testuser
ARG UID=1000
ARG GID=1000
RUN groupadd -g $GID -o $UNAME
RUN useradd -m -u $UID -g $GID -o -s /bin/bash $UNAME

ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y \
    libboost-dev sudo git meson libtool pkg-config g++ libsystemd-dev \
    python3 python3-pip python3-yaml python3-mako python3-inflection \
    cmake clang-tools nlohmann-json3-dev \
    sqlite3 libsqlite3-dev libyaml-cpp-dev libcurl4-gnutls-dev \
    libspdlog-dev libfmt-dev libgtest-dev libgmock-dev \
    systemd gcovr lcov protobuf-compiler protobuf-c-compiler libprotobuf-dev \
    && rm -rf /var/lib/apt/lists/*

RUN sed -i '1iDefaults umask=000' /etc/sudoers
RUN echo $UNAME "ALL=(ALL) NOPASSWD: ALL" >>/etc/sudoers
USER $UNAME

WORKDIR /nvidia-fdr
ENTRYPOINT ["bash", "-c"]
CMD ["./coverage/get_coverage.sh"]