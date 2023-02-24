// (c) F.Bisinger 2023

// Import required libraries
#include <Arduino.h>
#include "defines.h"
#include <ModbusMaster.h>
#include <SoftwareSerial.h>
#include <SPI.h>
#include <Ethernet.h>
#include "ThingSpeak.h" // always include thingspeak header file after other header files and custom macros


// Initialize the Ethernet client object
EthernetClient client;

ModbusMaster growattInterface;
SoftwareSerial *serial;

uint8_t result;

struct modbus_input_registers
{
  int status;
  float solarpower, pv1voltage, pv1current, pv1power, pv2voltage, pv2current, pv2power, outputpower, gridfrequency, gridvoltage;
  float energytoday, energytotal, totalworktime, pv1energytoday, pv1energytotal, pv2energytoday, pv2energytotal, opfullpower;
  float tempinverter, tempipm, tempboost;
  int ipf, realoppercent, deratingmode, faultcode, faultbitcode, warningbitcode;
};
struct modbus_input_registers modbusdata;

void preTransmission()
{
  digitalWrite(MAX485_DE, 1);
}

void postTransmission()
{
  digitalWrite(MAX485_DE, 0);
}

// does not read every register, only the most important ones...
void readModbus() {
  result = growattInterface.readInputRegisters(0, 64);
  if (result == growattInterface.ku8MBSuccess)   {
      // Status and PV data
      modbusdata.status = growattInterface.getResponseBuffer(0);
      modbusdata.solarpower = ((growattInterface.getResponseBuffer(1) << 16) | growattInterface.getResponseBuffer(2)) * 0.1;

      modbusdata.pv1voltage = growattInterface.getResponseBuffer(3) * 0.1;
      modbusdata.pv1current = growattInterface.getResponseBuffer(4) * 0.1;
      modbusdata.pv1power = ((growattInterface.getResponseBuffer(5) << 16) | growattInterface.getResponseBuffer(6)) * 0.1;

      modbusdata.pv2voltage = growattInterface.getResponseBuffer(7) * 0.1;
      modbusdata.pv2current = growattInterface.getResponseBuffer(8) * 0.1;
      modbusdata.pv2power = ((growattInterface.getResponseBuffer(9) << 16) | growattInterface.getResponseBuffer(10)) * 0.1;

      // Output
      modbusdata.outputpower = ((growattInterface.getResponseBuffer(35) << 16) | growattInterface.getResponseBuffer(36)) * 0.1;
      modbusdata.gridfrequency = growattInterface.getResponseBuffer(37) * 0.01;
      modbusdata.gridvoltage = growattInterface.getResponseBuffer(38) * 0.1;

      // Energy
      modbusdata.energytoday = ((growattInterface.getResponseBuffer(53) << 16) | growattInterface.getResponseBuffer(54)) * 0.1;
      modbusdata.energytotal = ((growattInterface.getResponseBuffer(55) << 16) | growattInterface.getResponseBuffer(56)) * 0.1;
      modbusdata.totalworktime = ((growattInterface.getResponseBuffer(57) << 16) | growattInterface.getResponseBuffer(58)) * 0.5;

      modbusdata.pv1energytoday = ((growattInterface.getResponseBuffer(59) << 16) | growattInterface.getResponseBuffer(60)) * 0.1;
      modbusdata.pv1energytotal = ((growattInterface.getResponseBuffer(61) << 16) | growattInterface.getResponseBuffer(62)) * 0.1;
      //overflow = growattInterface.getResponseBuffer(63);
    }
}


unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;

void setup()
{
  Ethernet.init(ETH_CS);
  
  Serial.begin(115200);
  while (!Serial) {
    ; // wait for serial port to connect. Needed for native USB port only
  }

  uint16_t index = millis() % NUMBER_OF_MAC;
  // start the Ethernet connection:
  Serial.println("Initialize Ethernet with DHCP:");
  if (Ethernet.begin(mac[index]) == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // Check for Ethernet hardware present
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
      while (true) {
        delay(1); // do nothing, no point running without Ethernet hardware
      }
    }
    if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("Ethernet cable is not connected.");
    }
    // try to configure using IP address instead of DHCP:
    //Ethernet.begin(mac, ip, myDns);
  } else {
    Serial.print("  DHCP assigned IP ");
    Serial.println(Ethernet.localIP());
  }  

  serial = new SoftwareSerial(MAX485_RX, MAX485_TX, false); //RX, TX
  serial->begin(MODBUS_RATE);
  growattInterface.begin(SLAVE_ID , *serial);

  pinMode(MAX485_DE, OUTPUT);
  pinMode(LED_R, OUTPUT);
  digitalWrite(LED_R, LOW);

  growattInterface.preTransmission(preTransmission);
  growattInterface.postTransmission(postTransmission);


  // give the Ethernet a second to initialize:
  delay(1000);
  
  ThingSpeak.begin(client);  // Initialize ThingSpeak
}


void loop()
{  
  Ethernet.maintain();
  
  readModbus();
  
  if (modbusdata.status == 0) {
    Serial.println("Inverter Powered Off -- No PV-Energy");
    ThingSpeak.setStatus("Inverter Powered Off -- No PV-Energy");
    ThingSpeak.setField(1, float(0.0));
    ThingSpeak.setField(2, float(0.0));
    ThingSpeak.setField(3, float(0.0));
    ThingSpeak.setField(4, float(0.0));
    ThingSpeak.setField(5, float(0.0));
  }

  else {
    // set status
    Serial.println("Inverter Powered On -- Status OK");
    Serial.println("Power: " + String(modbusdata.outputpower));
    Serial.println("PV-Voltage: " + String(modbusdata.pv1voltage));
    Serial.println("EnergyToday: " + String(modbusdata.energytoday));
    Serial.println("Gridvoltage: " + String(modbusdata.gridvoltage));
    Serial.println("Gridfreuency: " + String(modbusdata.gridfrequency));
    ThingSpeak.setStatus("Inverter Powered On -- Status OK");
    // set the fields with the values
    ThingSpeak.setField(1, modbusdata.outputpower);
    ThingSpeak.setField(2, modbusdata.pv1voltage);
    ThingSpeak.setField(3, modbusdata.energytoday);
    ThingSpeak.setField(4, modbusdata.gridvoltage);
    ThingSpeak.setField(5, modbusdata.gridfrequency);
  }

  // write to the ThingSpeak channel
  Serial.println("Writing to Thingspeak Channel " + String(myChannelNumber));
  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if(x == 200){
    Serial.println("Channel update successful.");
  }
  else{
    Serial.println("Problem updating channel. HTTP error code " + String(x));
  }
  
  // wait 120 seconds
  delay(120000);
}
