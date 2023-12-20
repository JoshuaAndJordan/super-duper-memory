#!/usr/bin/env bash

docker build -t crypto-image -f "docker/cmake-cpp-image.dockerfile" .

docker run -it --privileged --device=/dev/kvm --network host \
  -v "`pwd`:/crypto/" --workdir "/crypto/" --restart=unless-stopped \
  --entrypoint "" "crypto-image" bash

string="$(docker container ls | tail -n +2)"
id="$(echo "$string" | awk '{print $1}')"
if [ -n "$id" ]; then
  echo "Removing container with ID: $id"
  docker container stop $id
  docker container rm $id
fi

sudo rm -rf CMakeCache.txt CMakeFiles/ Makefile account_monitor/CMakeFiles/ account_monitor/Makefile \
  account_monitor/cmake_install.cmake account_process_delegator/CMakeFiles/ account_process_delegator/Makefile \
  account_process_delegator/cmake_install.cmake cmake_install.cmake common/CMakeFiles/ common/Makefile \
  common/cmake_install.cmake price_monitor/CMakeFiles/ price_monitor/Makefile price_monitor/cmake_install.cmake bin/ lib/
