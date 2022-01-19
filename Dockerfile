FROM ubuntu:20.04

USER root
WORKDIR /

SHELL ["/bin/bash", "-c"]

ENV DEBIAN_FRONTEND=noninteractive \
    http_proxy="http://child-prc.intel.com:913/" \
    https_proxy="http://child-prc.intel.com:913/" \
    HTTP_PROXY="http://child-prc.intel.com:913/" \
    HTTPS_PROXY="http://child-prc.intel.com:913/"

COPY ./scripts /scripts-omniscidb
# Only 4 files in omniscidb/scripts are needed:
    # mapd-deps-ubuntu.sh
    # common-functions.sh
    # llvm-9-glibc-2.31-708430.patch

RUN cd /scripts-omniscidb && \
    sed -i 's/sudo //g' mapd-deps-ubuntu.sh && \
    sed -i 's/sudo //g' common-functions.sh && \
    sed -i 's/PREFIX=\/usr\/local\/mapd-deps/PREFIX=\/usr\/local/g' mapd-deps-ubuntu.sh && \
    apt-get update && \
    source /usr/local/mapd-deps.sh && \
    ./mapd-deps-ubuntu.sh --compress --nocuda && \
    rm -rf /var/lib/apt/lists/* && \
    cd / && \
    rm -rf scripts-omniscidb

COPY entrypoint.sh /entrypoint.sh

RUN bash entrypoint.sh