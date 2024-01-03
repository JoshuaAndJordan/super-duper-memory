FROM ubuntu:latest
LABEL maintainer="joshua.ogunyinka@codethink.co.uk"

# This block represents the core dependencies
RUN export DEBIAN_FRONTEND=noninteractive; \
    apt update && \
    apt install -y git cmake libssl-dev g++ make libboost-all-dev nano && \
    apt clean

RUN export DEBIAN_FRONTEND=noninteractive; \
    apt install -y libmsgpackc2 libmsgpack-dev pkg-config libzmq3-dev && \
    apt install -y unixodbc-dev supervisor nginx python3 python3-pip && \
    pip install requests flask black && \
    apt clean

RUN git clone "https://github.com/msgpack/msgpack-c"
RUN git clone "https://github.com/zeromq/libzmq"
RUN git clone "https://github.com/zeromq/cppzmq"

# install msgpack
WORKDIR msgpack-c/
RUN git checkout cpp-6.1.0 && git submodule update --init
RUN cmake . && make -j 4 && make install

WORKDIR /
RUN rm -rf msgpack-c

#install zeromq
WORKDIR libzmq
RUN git submodule update --init
RUN mkdir build
WORKDIR build
RUN cmake .. && make clean && make -j 4 && make install

WORKDIR /
RUN rm -rf zeromq

# install cppzmq
WORKDIR cppzmq
RUN cmake . && make -j 4 && make install

WORKDIR /
RUN rm -rf cppzmq

RUN mkdir -p run_crypto
WORKDIR run_crypto
RUN mkdir -p log log/account_monitor log/price_monitor log/server_message
RUN mkdir -p log/process_delegator log/message_delegator log/nginx

COPY docker/files/supervisord.conf /etc/supervisor/supervisord.conf
COPY docker/files/nginx.conf /etc/nginx/nginx.conf
COPY docker/files/start.sh /run_crypto/start.sh
COPY scripts/test_prices.py /test_prices.py
COPY scripts/test_accounting.py /test_accounting.py
COPY scripts/server.py /run_crypto/message_server.py

ENTRYPOINT ["/run_crypto/start.sh", "--"]
