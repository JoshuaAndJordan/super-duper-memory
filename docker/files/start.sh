#!/usr/bin/env bash

cd /crypto/

cmake .
make -j 2

service nginx start
service supervisor start

echo "Sleeping for 10 seconds..."
sleep 10

echo "Starting tests..."
python3 /test_prices.py
python3 /test_accounting.py

bash
