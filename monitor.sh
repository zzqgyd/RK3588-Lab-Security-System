#!/bin/bash
watch -n 1 'cat /sys/kernel/debug/rknpu/load 2>/dev/null;cat /sys/kernel/debug/rkrga/load 2>/dev/null'
