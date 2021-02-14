# Arduino Files

There are some additional steps you must complete before uploading the .ino file to your board.

### Required additions and libraries

Add support for the ESP8266:
- Add: https:\//arduino.esp8266.com/stable/package_esp8266com_index.json to additional board manager URLs
- Install the ESP8266 platform via board manager
- Select the Wemos D1 R1 board (or as appropriate for the board you use.

Install the following additional libraries:
- FastLED
- PubSubClient
- Rtc by Makuna

Upload the contents of the /data folder to flash memory:
  see: [Uploading Files to Flash Memory](http://arduino.esp8266.com/Arduino/versions/2.3.0/doc/filesystem.html#uploading-files-to-file-system) for instructions.
  (This step only needs to be done once per board. It is not necessary to repeat when loading new .ino files)
  
### Credential.h`

Open this file with the IDE or text editor.  Update the SSID and password for your wifi.  If enabling MQTT, also update the MQTT user and password. Save the file

### Additional Settings and Options

Within the clock_main.ino file are addition settings and a number of options you should review.  Each of these are documented within the .ino file and are self-explanatory. At a minimum, you should review and update the following:
- MILLI_AMPS: Update this to match the maximum millamps of your power supply
- MQTTMODE: 1 to enable MQTT, 0 to disable 
- MQTTSERVER: Set to IP or URL of your MQTT broker if MQTT enabled
- MQTTPORT: Set to port of your MQTT broker (1883 by default) if MQTT enabled

If you used a different board or wired differently from the original design, update the assigned pins.  Otherwise, the rest of the options can be used to set various defaults upon boot, such as colors, 12 or 24 hour clock display, etc.

### Additional Information:
Full details on the build, including parts list, wiring schematics and more can be found at [ResinChem Tech Blog](https://resinchemtech.blogspot.com/2021/02/3d-printed-clock-scoreboard-and-more.html)
