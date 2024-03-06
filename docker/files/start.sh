#!/usr/bin/env bash

cd /crypto/ || exit

mkdir -p /crypto/x86-build/
cd /crypto/x86-build || exit
cmake -DCMAKE_BUILD_TYPE=Release ../
make -j3

# service nginx start
service dbus restart
service supervisor start
# echo "Sleeping for 10 seconds..."
# sleep 10
# echo "Starting tests..."
# python3 /test_prices.py
# python3 /test_accounting.py

bash
