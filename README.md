# ESP8266 Internet Garage Door Opener

This repo contains all the design files relate to the ESP8266-based garage
door opener project I recently created. My basic requirements were:

- Be functonal and deployed as quickly as possible, rather than being as clean
and well-engineered as possible.

- Not interfere with the electronics of my existing manual-switch-triggered
opener.

Quick list of features/parts:

- ESP8266 WiFi SoC for the Internet connection. NodeMCU board.

- Arduino-based software stack.

- HTTP/HTML-based user interface.

- WiFi credentials entered at "run-time" using a web browser and saved to
flash, rather than being hard-coded into the application code.

- PCB created in Kicad to mount the ESP8266 and connectors.

- Standalone commercial relay board to trigger the door opener.

- HC-SR04 sensors used to detect whether the door is open or closed.

- HC-SR04 wall mounts designed using OpenSCAD and 3D-printed.

Look on my blog at http://rabbithole.wwwdotorg.org for more details.
