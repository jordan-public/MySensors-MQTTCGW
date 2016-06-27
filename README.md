# MySensors-MQTTCGW
MySensors Gateway which connects to MQTT broker as client.  Runs on ESP8266.

DESCRIPTION
 This is a MySensors Gateway which acts as a MQTT client.  It runs on ESP8266.
 After establishing a WiFi connection to the local network, it connects to
 the designated MQTT broker.  Connection announcement messages are sent at 
 initialization.  All messages between each MySensors sensor and the MQTT broker 
 are communicated in both directions.  Finally, upon disconnection (orderly or
 disorderly) the MQTT broker receives a message, using the last will MQTT
 feature.  
 
 Security WARNING: (solvable with proper configuration/care)
 While this Gateway uses WPA2 WiFi authentication and encryption, as well as
 authentication to the MQTT broker, the traffic to the MQTT btoker is not
 encrypted.  This may be OK if the MQTT broker is inside the trusted Local
 Area Network, but dangerous if we try to connect to an outside MQTT server.
 The traffic to the remote MQTT broker should be protected via VPN, stunnel,
 or similar.  Maybe even a local MQTT broker can provide secure bridging
 to the outside MQTT broker.  For example, local Mosquitto MQTT broker runnng
 on PC, Mac or Raspberry Pi can connect as bridge to Amazon AWS IoT MQTT via 
 WebSockets over HTTPS and solve this security problem.

 Tested on:
 - NodeMCU 0.9
 - With and without WITH_LEDS_BLINKING
 - Connecting to Mosquitto MQTT Broker (http://mosquitto.org).  
 Not (yet) tested:
 - Message signing (using MySigningAtsha204Soft).
 Not (yet) implemented:
 - Inclusion mode.
 
 Rererences:
 Arduino ESP8266: https://github.com/esp8266/Arduino
 MySensors: http://www.mysensors.org
 MQTT Client: http://pubsubclient.knolleary.net
