#!/bin/bash

# Script to trigger SAMD21 bootloader and upload firmware
# Usage: ./upload.sh [port]
# If port is not specified, it will try to auto-detect

# Function to find the serial port
find_port() {
    # Try common macOS/Linux serial port patterns
    for port in /dev/ttyACM* /dev/ttyUSB* /dev/cu.usbmodem* /dev/cu.usbserial*; do
        if [ -e "$port" ]; then
            echo "$port"
            return 0
        fi
    done
    return 1
}

# Get port from argument or auto-detect
if [ -n "$1" ]; then
    PORT="$1"
else
    PORT=$(find_port)
    if [ -z "$PORT" ]; then
        echo "Error: Could not find serial port. Please specify it as an argument:"
        echo "  ./upload.sh /dev/ttyACM0"
        echo "  ./upload.sh /dev/cu.usbmodem14101"
        exit 1
    fi
    echo "Auto-detected port: $PORT"
fi

# Check if port exists
if [ ! -e "$PORT" ]; then
    echo "Error: Port $PORT does not exist"
    exit 1
fi

echo "Triggering bootloader on $PORT at 1200 bps..."
# Open serial port at 1200 baud to trigger bootloader mode
# Using stty to set baud rate and then close immediately
stty -f "$PORT" 1200
sleep 0.1
stty -f "$PORT" 1200
sleep 0.1

echo "Waiting 3 seconds for bootloader to activate..."
sleep 3

echo "Uploading firmware with PlatformIO..."
pio run -t upload


