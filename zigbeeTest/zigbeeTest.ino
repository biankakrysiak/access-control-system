#include <Zigbee.h>
#include <ZigbeeCore.h>
#include <ZigbeeEP.h>
#include <ZigbeeTypes.h>

static const uint8_t RELAY_PIN = 3;

#define ZIGBEE_LIGHT_ENDPOINT 10

ZigbeeLight zbLight(ZIGBEE_LIGHT_ENDPOINT);

/**
 * Sets the relay state:
 * @param state true = ON (HIGH), false = OFF (LOW)
 */

void setRelay(bool state) {
  digitalWrite(RELAY_PIN, state ? HIGH : LOW);
}

void setup(){
  // Initialize the relay pin
  pinMode(RELAY_PIN, OUTPUT);
  setRelay(false); // default off

  // optional - set the zigbee device info for identification
  zbLight.setManufacturerAndModel("Espressif", "ZBRelay");

  // specify the function to call when the zigbee on/off state changes
  zbLight.onLightChange(setRelay);

  // register the ZigbeeLight endpoint with the zigbee stack
  Zigbee.addEndpoint(&zbLight);

  // initialize zigbee (default: End Device mode)
  if (!Zigbee.begin()){
    // if zigbee fails to start, restart the device
    ESP.restart();
  }

  // wait until the device connects to the zigbee network
  while(!Zigbee.connected()){
    delay(100);
  }
}

void loop() {
  // no extra logic required, just maintain zigbee operations
}

/**
Settings:
Tools -> Partition Scheme -> Zigbee 4MB with spiffs
Tools -> Zigbee Mode -> Zigbee End Device
**/
