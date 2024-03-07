#!/usr/bin/env bash

container_id="$(docker container ls --filter 'ancestor=crypto-image' --format '{{.Names}}' | head -n 1)"

if [ -n "$container_id" ]; then
  docker container attach "$container_id"
else
  mkdir -p x86-build/
  docker build -t crypto-image -f "docker/cmake-cpp-image.dockerfile" .
  docker run -it --privileged --device=/dev/kvm --network host \
    -v "$(pwd):/crypto/" --workdir "/crypto/x86-build/" --restart=unless-stopped \
    "crypto-image" bash
fi

if [ -n "$1" ]; then
  string="$(docker container ls | tail -n +2)"
  id="$(echo "$string" | awk '{print $1}')"
  if [ -n "$id" ]; then
    echo "Removing container with ID: $id"
    docker container stop "$id"
    docker container rm "$id"
  fi
  sudo rm -rf common/dbus/include/progress_adaptor_server.hpp \
    common/dbus/include/progress_proxy_client.hpp \
    common/dbus/include/time_adaptor_server.hpp \
    common/dbus/include/time_proxy_client.hpp \
    common/dbus/include/price_task_result_client.hpp \
    common/dbus/include/price_task_result_server.hpp
#  sudo rm -rf x86-build
fi
