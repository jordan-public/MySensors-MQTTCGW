/**
 * Created by Jordan Stojanovski https://github.com/jordan-public
 * Copyright (C) 2016 Logyx Compuer Corp.
 * 
 * Based on MQTTGateway example from http://www.mysensors.org
 * 
 * Rererences:
 * MySensors: http://www.mysensors.org
 * MQTT Client: http://forum.mysensors.org
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 *******************************
 *
 * REVISION HISTORY
 * Version 0.9 - Created by Jordan Stojanovski https://github.com/jordan-public
 * 
 * DESCRIPTION
 * This is a MySensors Gateway which acts as a MQTT client.  It runs on ESP8266.
 * After establishing a WiFi connection to the local network, it connects to
 * the designated MQTT broker.  Connection announcement messages are sent at 
 * initialization.  All messages between each MySensors sensor and the MQTT broker 
 * are communicated in both directions.  Finally, upon disconnection (orderly or
 * disorderly) the MQTT broker receives a message, using the last will MQTT
 * feature.  
 *
 * How-to:
 * https://github.com/jordan-public/MySensors-MQTTCGW
 * 
 * Security WARNING: (solvable with proper configuration/care)
 * While this Gateway uses WPA2 WiFi authentication and encryption, as well as
 * authentication to the MQTT broker, the traffic to the MQTT btoker is not
 * encrypted.  This may be OK if the MQTT broker is inside the trusted Local
 * Area Network, but dangerous if we try to connect to an outside MQTT server.
 * The traffic to the remote MQTT broker should be protected via VPN, stunnel,
 * or similar.  Maybe even a local MQTT broker can provide secure bridging
 * to the outside MQTT broker.  For example, local Mosquitto MQTT broker runnng
 * on PC, Mac or Raspberry Pi can connect as bridge to Amazon AWS IoT MQTT via 
 * WebSockets over HTTPS and solve this security problem.
 * 
 * Tested on:
 * - NodeMCU 0.9
 * - With and without WITH_LEDS_BLINKING
 * - Connecting to Mosquitto MQTT Broker (http://mosquitto.org).  
 * Not (yet) tested:
 * - Message signing (using MySigningAtsha204Soft).
 * Not (yet) implemented:
 * - Inclusion mode.  
 * 
 * Rererences:
 * Arduino ESP8266: https://github.com/esp8266/Arduino
 * MySensors: http://www.mysensors.org
 * MQTT Client: http://pubsubclient.knolleary.net
 */
#include <SPI.h>
#ifdef WITH_LEDS_BLINKING
#include <Ticker.h> // From https://github.com/esp8266/Arduino/tree/master/libraries/Ticker
#endif
#include <ESP8266WiFi.h> // From https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WiFi
#include <PubSubClient.h> // From http://pubsubclient.knolleary.net

// From https://www.mysensors.org
#include <MySigningNone.h>
#include <MyTransportRFM69.h>
#include <MyTransportNRF24.h>
#include <MyHwESP8266.h>
#include <MySigningAtsha204Soft.h>
#include <MySigningAtsha204.h>
#include <MySensor.h>
#include <examples/MQTTGateway/MyMQTT.h> // Using this from MySensors MQTTGateway for compatibility

// Configuration for connecting to WiFi and the MQTT Broker
const char *ssid =  "...";
const char *wifiPassword =  "...";
const char* mqtt_server = "...";
const int mqtt_port = 1883;
const char *mqttUser = NULL;
const char *mqttPassword = NULL;

#ifdef DEBUG
#define MQTTDUMP
#endif

// Future use - Inclusion Mode not yet implemented
// #define INCLUSION_MODE_TIME 1 // Number of minutes inclusion mode is enabled
// #define INCLUSION_MODE_PIN  0 // Digital pin used for inclusion mode button // Use the FLASH (GPIO0) switch on the NodeMCU

// According to: https://www.mysensors.org/build/esp8266_gateway
#define RADIO_CE_PIN        4   // radio chip enable
#define RADIO_SPI_SS_PIN    15  // radio SPI serial select

#ifdef WITH_LEDS_BLINKING
#define RADIO_ERROR_LED_PIN 5 // Error led pin
#define RADIO_RX_LED_PIN    0 // Receive led pin
#define RADIO_TX_LED_PIN    2 // the PCB, on board LED
#endif

#ifdef WITH_LEDS_BLINKING
// Add led timer interrupt
Ticker ticker;
void rxBlink(uint8_t cnt);
void txBlink(uint8_t cnt);
void errBlink(uint8_t cnt);
#endif

// NRFRF24L01 radio driver (set low transmit power by default) 
MyTransportNRF24 transport(RADIO_CE_PIN, RADIO_SPI_SS_PIN, RF24_PA_LEVEL_GW);  
//MyTransportRFM69 transport;

// Message signing driver (signer needed if MY_SIGNING_FEATURE is turned on in MyConfig.h)
#ifdef MY_SIGNING_FEATURE
MySigningNone signer;
//MySigningAtsha204Soft signer;
//MySigningAtsha204 signer; // Needs hardware changes.  
#endif

// Hardware profile 
MyHwESP8266 hw;

// Construct MySensors library (signer needed if MY_SIGNING_FEATURE is turned on in MyConfig.h)
MySensor gw(transport, hw
#ifdef MY_SIGNING_FEATURE
    , signer
#endif
#ifdef WITH_LEDS_BLINKING
  , RADIO_RX_LED_PIN, RADIO_TX_LED_PIN, RADIO_ERROR_LED_PIN
#endif
  );

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

MyMessage msg;
#define MQTT_MAX_TOPIC_SIZE 100
char gatewayMqttTopic[MQTT_MAX_TOPIC_SIZE];
char broker[] PROGMEM = MQTT_BROKER_PREFIX;
char lastTopic[MQTT_MAX_TOPIC_SIZE];

#ifdef WITH_LEDS_BLINKING
volatile uint8_t countRx;
volatile uint8_t countTx;
volatile uint8_t countErr;
#endif

void assureConnected() 
{
  while (!mqttClient.connected()) {
    delay(10);
    if (WiFi.status() != WL_CONNECTED) {
#ifdef DEBUG
      Serial.println(); Serial.println();
      Serial.println("MySensors MQTT-client Gateway");
      Serial.print("Connecting to "); Serial.println(ssid);
#endif    
      WiFi.begin(ssid, wifiPassword);
      while (WiFi.status() != WL_CONNECTED)
      {
        delay(500);
#ifdef DEBUG
        Serial.print(".");
#endif
      }
#ifdef DEBUG
      Serial.println("");
      Serial.println("Connected!");
      Serial.print("IP address: "); Serial.println(WiFi.localIP());
#endif
    }
    if (!mqttClient.connected()) {
#ifdef DEBUG
      Serial.print("Connecting to MQTT Server: "); Serial.print(mqtt_server);
      Serial.print(":"); Serial.println(mqtt_port);
#endif
      if (mqttClient.connect("MySensors-Gateway", mqttUser, mqttPassword, gatewayMqttTopic, 1, true, "GATEWAY DISCONNECTED")) {
#ifdef DEBUG
        Serial.println("Connected to MQTT server.");
#endif       
        mqttClient.publish(gatewayMqttTopic, "GATEWAY CONNECTED"); // Announce
        mqttClient.subscribe(MQTT_BROKER_PREFIX"/#", 1); // Subscribe
      } else {
#ifdef DEBUG
        Serial.print("Failed, rc=");
        Serial.print(mqttClient.state());
        Serial.println(" trying again in 5 seconds...");
#endif
        // Wait 5 seconds before retrying
        delay(5000);
      }
    }
  }
}

void setup()  
{
  // Setup console
  hw_init(); // Initialize serial port at speed BAUD_RATE defined in <MyConfig.h>

  mqttClient.setServer(mqtt_server, mqtt_port);
  mqttClient.setCallback(mqttReceiveCallback); 
          
#ifdef WITH_LEDS_BLINKING
  ticker.attach(0.3, ledTimersInterrupt);
#endif

#ifdef WITH_LEDS_BLINKING
  countRx = 0;
  countTx = 0;
  countErr = 0;

  // Setup led pins
  pinMode(RADIO_RX_LED_PIN, OUTPUT);
  pinMode(RADIO_TX_LED_PIN, OUTPUT);
  pinMode(RADIO_ERROR_LED_PIN, OUTPUT);
  digitalWrite(RADIO_RX_LED_PIN, LOW);
  digitalWrite(RADIO_TX_LED_PIN, LOW);
  digitalWrite(RADIO_ERROR_LED_PIN, LOW);
 
  // Set initial state of leds
  digitalWrite(RADIO_RX_LED_PIN, HIGH);
  digitalWrite(RADIO_TX_LED_PIN, HIGH);
  digitalWrite(RADIO_ERROR_LED_PIN, HIGH);
#endif

  strcpy(gatewayMqttTopic,MQTT_BROKER_PREFIX"/0/0/V_");
  strcat_P(gatewayMqttTopic,vType[V_UNKNOWN]);

//  setupGateway(INCLUSION_MODE_PIN, INCLUSION_MODE_TIME, output); // Inclusion mode not yet implemented

  // Initialize gateway at maximum PA level, channel 70 and callback for write operations 
  gw.begin(sendMyMessageToMQTT, 0, true, 0);  

#ifdef DEBUG
  Serial.println("setup() complete");
#endif
}

void loop()
{
  assureConnected();
  mqttClient.loop();
  gw.process();    
}

void mqttReceiveCallback(char* topic, byte* payload, unsigned int length) {
#ifdef MQTTDUMP
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) Serial.print((char)payload[i]);
  Serial.println();
#endif
  if (0 == strcmp(topic, lastTopic) || 0 == strcmp(topic, gatewayMqttTopic)) { // Ignore gateway's own messages.
    lastTopic[0] = '\0'; // So we don't ignore subsequent messages on this topic.
#ifdef MQTTDUMP
    Serial.println("Mesage ignored.");
    Serial.println();
#endif
    return;
  }
  char *str, *p;
  uint8_t i = 0;
  // TODO: Check if we should send ack or not.
  for (str = strtok_r(topic,"/",&p) ; str && i<4 ; str = strtok_r(NULL,"/",&p)) {
    if (i == 0) {
      if (strcmp_P(str,broker)!=0) {  //look for MQTT_BROKER_PREFIX
        return;     //Message not for us or malformatted!
      }
    } else if (i==1) {
      msg.destination = atoi(str);  //NodeID
    } else if (i==2) {
      msg.sensor = atoi(str);   //SensorID
    } else if (i==3) {
      unsigned char match=255;      //SensorType
#ifdef MQTT_TRANSLATE_TYPES       
      for (uint8_t j=0; j<V_TOTAL; j++) {
        if (0==strcmp_P(str+2,vType[j])) { //Strip V_ and compare
          match=j;
          break;
        }
      }
#endif
      if ( atoi(str)!=0 || (str[0]=='0' && str[1] =='\0') ) {
        match=atoi(str);
      }

      if (match==255) {
        match=V_UNKNOWN;
      }
      msg.type = match;
    }
    i++;
  }
  msg.set(payload, length); //Payload type is P_CUSTOM now
  mSetPayloadType(msg, P_STRING); // Make it P_STRING
#ifdef WITH_LEDS_BLINKING
  txBlink(1);
#endif
  if (!gw.sendRoute(build(msg, msg.destination, msg.sensor, C_SET, msg.type, 0))) 
#ifdef WITH_LEDS_BLINKING
    errBlink(1)
#endif
    ;
}

inline MyMessage& build (MyMessage &msg, uint8_t destination, uint8_t sensor, uint8_t command, uint8_t type, bool enableAck) {
	msg.destination = destination;
	msg.sender = GATEWAY_ADDRESS;
	msg.sensor = sensor;
	msg.type = type;
	mSetCommand(msg,command);
	mSetRequestAck(msg,enableAck);
	mSetAck(msg,false);
	return msg;
}

void sendMyMessageToMQTT(const MyMessage &inMsg) {
#ifdef WITH_LEDS_BLINKING
   rxBlink(1);
#endif
  MyMessage msg = inMsg;
  if (msg.isAck()) {
//		if (msg.sender==255 && mGetCommand(msg)==C_INTERNAL && msg.type==I_ID_REQUEST) {
// TODO: sending ACK request on id_response fucks node up. doesn't work.
// The idea was to confirm id and save to EEPROM_LATEST_NODE_ADDRESS.
//  }
	} else {
		// we have to check every message if its a newly assigned id or not.
		// Ack on I_ID_RESPONSE does not work, and checking on C_PRESENTATION isn't reliable.
		uint8_t newNodeID = gw.loadState(EEPROM_LATEST_NODE_ADDRESS)+1;
		if (newNodeID <= MQTT_FIRST_SENSORID) newNodeID = MQTT_FIRST_SENSORID;
		if (msg.sender==newNodeID) {
			gw.saveState(EEPROM_LATEST_NODE_ADDRESS,newNodeID);
		}
		if (mGetCommand(msg)==C_INTERNAL) {
#ifdef DEBUG
      Serial.println("Processing command: C_INTERNAL");
#endif
			if (msg.type==I_CONFIG) {
#ifdef WITH_LEDS_BLINKING
				txBlink(1);
#endif
      if (!gw.sendRoute(build(msg, msg.sender, 255, C_INTERNAL, I_CONFIG, 0).set(MQTT_UNIT))) 
#ifdef WITH_LEDS_BLINKING
        errBlink(1)
#endif
        ;
				return;
			} else if (msg.type==I_ID_REQUEST && msg.sender==255) {
				uint8_t newNodeID = gw.loadState(EEPROM_LATEST_NODE_ADDRESS)+1;
				if (newNodeID <= MQTT_FIRST_SENSORID) newNodeID = MQTT_FIRST_SENSORID;
				if (newNodeID >= MQTT_LAST_SENSORID) return; // Sorry no more id's left :(
#ifdef WITH_LEDS_BLINKING
				txBlink(1);
#endif
        if (!gw.sendRoute(build(msg, msg.sender, 255, C_INTERNAL, I_ID_RESPONSE, 0).set(newNodeID))) 
#ifdef WITH_LEDS_BLINKING
          errBlink(1)
#endif
          ;
				return;
			}
		}
		if (mGetCommand(msg)!=C_PRESENTATION) {
			if (mGetCommand(msg)==C_INTERNAL) msg.type=msg.type+(S_FIRSTCUSTOM-10);	//Special message
      strcpy_P(lastTopic, broker);
#ifdef MQTT_TRANSLATE_TYPES
			if (msg.type > V_TOTAL) msg.type=V_UNKNOWN;// If type > defined types set to unknown.
      sprintf(lastTopic+strlen(lastTopic),"/%i/%i/V_",msg.sender,msg.sensor);
      strcat_P(lastTopic, vType[msg.type]);
#else
			sprintf(lastTopic+strlen(lastTopic),"/%i/%i/%i",msg.sender,msg.sensor,msg.type);
#endif
      char payloadBuf[MAX_PAYLOAD*2+1];
			msg.getString(payloadBuf);
			mqttClient.publish(lastTopic, payloadBuf);
		}
	}
}

#ifdef WITH_LEDS_BLINKING
void ledTimersInterrupt() {
  if(countRx && countRx != 255) {
    // switch led on
    digitalWrite(RADIO_RX_LED_PIN, LOW);
  } else if(!countRx) {
     // switching off
     digitalWrite(RADIO_RX_LED_PIN, HIGH);
   }
   if(countRx != 255) { countRx--; }

  if(countTx && countTx != 255) {
    // switch led on
    digitalWrite(RADIO_TX_LED_PIN, LOW);
  } else if(!countTx) {
     // switching off
     digitalWrite(RADIO_TX_LED_PIN, HIGH);
   }
   if(countTx != 255) { countTx--; }

  if(countErr && countErr != 255) {
    // switch led on
    digitalWrite(RADIO_ERROR_LED_PIN, LOW);
  } else if(!countErr) {
     // switching off
     digitalWrite(RADIO_ERROR_LED_PIN, HIGH);
   }
   if(countErr != 255) { countErr--; }

}

void rxBlink(uint8_t cnt) {
  if(countRx == 255) { countRx = cnt; }
}
void txBlink(uint8_t cnt) {
  if(countTx == 255) { countTx = cnt; }
}
void errBlink(uint8_t cnt) {
  if(countErr == 255) { countErr = cnt; }
}
#endif
