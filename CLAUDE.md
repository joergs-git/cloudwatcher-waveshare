# CloudWatcher Waveshare - Project Instructions

## Flashing
After every successful build, always flash the firmware to the device automatically without asking.
- Find the USB serial port: `ls /dev/cu.usbmodem*` (CH340, name changes on reconnect)
- Flash command: `bash -c 'source ~/esp/esp-idf/export.sh 2>/dev/null && idf.py -p <PORT> flash 2>&1'`
- If no usbmodem port is found, ask the user to check the USB connection
