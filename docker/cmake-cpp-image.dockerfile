FROM ubuntu:latest
LABEL maintainer="joshua.ogunyinka@codethink.co.uk"

# This block represents the core dependencies
RUN export DEBIAN_FRONTEND=noninteractive; \
    apt update && \
    apt install -y git cmake libssl-dev g++ make libboost-all-dev nano && \
    apt clean

RUN export DEBIAN_FRONTEND=noninteractive; \
    apt install -y libmsgpackc2 libmsgpack-dev pkg-config libzmq3-dev gperf && \
    apt install -y libcap-dev libmount-dev rsync dbus systemd tmux && \
    apt install -y unixodbc-dev supervisor nginx python3 python3-pip meson && \
    pip install requests flask && \
    apt clean

RUN git clone "https://github.com/msgpack/msgpack-c"
RUN git clone "https://github.com/zeromq/libzmq"
RUN git clone "https://github.com/zeromq/cppzmq"
RUN git clone "https://github.com/Kistler-Group/sdbus-cpp"

# install msgpack
WORKDIR /msgpack-c/
RUN git checkout cpp-6.1.0 && git submodule update --init
RUN cmake -DMSGPACK_USE_BOOST=ON -DMSGPACK_USE_STD_VARIANT_ADAPTOR=ON \
    -DMSGPACK_USE_STATIC_BOOST=ON -DMSGPACK_CXX17=ON .
RUN make -j 4 && make install

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

WORKDIR /sdbus-cpp
RUN git checkout v1.5.0
RUN git submodule update --init
RUN mkdir -p /sdbus-cpp/build
WORKDIR /sdbus-cpp/build
RUN cmake -DBUILD_LIBSYSTEMD=ON ../ && make install -j4
WORKDIR /sdbus-cpp/tools/
RUN cmake . && make install -j 2
WORKDIR /

RUN mkdir -p run_crypto
WORKDIR run_crypto
RUN mkdir -p log log/account_monitor log/account_tasks log/http_stream \
    log/price_result_stream log/price_monitor log/progress_tasks log/nginx \
    log/time_tasks

COPY docker/files/supervisord.conf /etc/supervisor/supervisord.conf
COPY docker/files/nginx.conf /etc/nginx/nginx.conf
COPY docker/files/start.sh /run_crypto/start.sh
COPY scripts/test_prices.py /test_prices.py
COPY scripts/test_accounting.py /test_accounting.py
COPY scripts/server.py /run_crypto/message_server.py
COPY docker/files/keep.*.conf /etc/dbus-1/system.d/

RUN service dbus start
ENTRYPOINT ["/run_crypto/start.sh", "--"]
# ENTRYPOINT [""]
