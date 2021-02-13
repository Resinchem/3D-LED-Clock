# MQTT Topic Commands and States

### MQTT use is optional, but if desired, a few steps must be taken

- First, you must have a working MQTT broker.  This can be the Home Assistant integrated broker (Mosquitto) or a standalone broker.
- The clock and MQTT broker must be on the same network, or able to talk to each other if on different VLANs, etc.
- You must specify the broker IP address and port in the settings at the top of the Arduino .ino sketch
- MQTT must be enabled in the Arudino .ino sketch (MQTTMODE = 1)
- You must update the Credentials.h file and specify your MQTT user name and password (if used).

The full list of topic states and commands, and valid payloads, can be found in the MQTT_Topics.pdf file.  Currently, there are over 30 states reported by the clock and over 20 commands that can be issued to the clock over MQTT.