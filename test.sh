#!/bin/sh

echo "Dumping latency data every 5 seconds..."
while :; do sleep 5; echo "dump"; done | ./lattop
