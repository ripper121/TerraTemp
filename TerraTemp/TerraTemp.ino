#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <EEPROM.h>






#include <sys/time.h> // struct timeval
#include <TZ.h>
#define MYTZ TZ_Europe_London

#define LED 13
#define RELAIS 12
#define BUTTON 0
//#define SI7021
#define AM2301

#define urlMaxLength 2048
#define floatLength 4
#define intLength 2
int dayTime[] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
int nightTime[] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
int summerMonth[] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
int winterMonth[] = {255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255, 255};
String ntpServer = "pool.ntp.org";

ESP8266WebServer server(80);

#ifdef SI7021
#include <TroykaDHT.h>
DHT dht(14, DHT21);
#endif
#ifdef AM2301
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#define DHTPIN 14
#define DHTTYPE DHT21
DHT_Unified dht(DHTPIN, DHTTYPE);
uint32_t delayMS;
#endif
float temperature = 0.0;
float humidity = 0.0;
float dayTemp = 0.0;
float nightSummerTemp = 0.0;
float nightWinterTemp = 0.0;
float targetTemperature = 0.0;
float offsetTemp = 0.0;
bool isDay = false;
bool isSummer = false;
bool heaterOn = false;
bool doReboot = false;

unsigned long previousMillis = 0;
const long interval = 1000;
int rebootTimer = 0;


void getSensor() {

#ifdef SI7021
  dht.read();
  switch (dht.getState()) {
    // всё OK
    case DHT_OK:
      Serial.print("Temperature = ");
      temperature = dht.getTemperatureC() + offsetTemp;
      Serial.print(temperature);
      Serial.println(" C \t");
      Serial.print("Humidity = ");
      humidity = dht.getHumidity();
      Serial.print(humidity);
      Serial.println(" % ");
      break;
    case DHT_ERROR_CHECKSUM:
      Serial.println("Checksum error");
      break;
    case DHT_ERROR_TIMEOUT:
      Serial.println("Time out error");
      break;
    case DHT_ERROR_NO_REPLY:
      Serial.println("Sensor not connected");
      break;
  }
#endif
#ifdef AM2301
  // Get temperature event and print its value.
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) {
    Serial.println(F("Error reading temperature!"));
  }
  else {
    Serial.print(F("Temperature: "));
    temperature = event.temperature;
    Serial.print(temperature);
    Serial.println(F("°C"));
  }
  // Get humidity event and print its value.
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) {
    Serial.println(F("Error reading humidity!"));
  }
  else {
    Serial.print(F("Humidity: "));
    humidity = event.relative_humidity;
    Serial.print(humidity);
    Serial.println(F("%"));
  }
#endif
}


void WriteStringToEEPROM(int beginaddress, String string)
{
  char charBuf[string.length() + 1];
  string.toCharArray(charBuf, string.length() + 1);
  for (int t = 0; t < sizeof(charBuf); t++)
  {
    EEPROM.write(beginaddress + t, charBuf[t]);
  }
}
String ReadStringFromEEPROM(int beginaddress)
{
  byte counter = 0;
  char rChar;
  String retString = "";
  while (1)
  {
    rChar = EEPROM.read(beginaddress + counter);
    if (rChar == 0) break;
    if (counter > 31) break;
    counter++;
    retString.concat(rChar);

  }
  return retString;
}
void EEPROMWritefloat(int address, long value)
{
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);

  //Write the 4 bytes into the eeprom memory.
  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
}
float EEPROMReadfloat(int address)
{
  //Read the 4 bytes from the eeprom memory.
  long four = EEPROM.read(address);
  long three = EEPROM.read(address + 1);
  long two = EEPROM.read(address + 2);
  long one = EEPROM.read(address + 3);

  //Return the recomposed long by using bitshift.
  return ((four << 0) & 0xFF) + ((three << 8) & 0xFFFF) + ((two << 16) & 0xFFFFFF) + ((one << 24) & 0xFFFFFFFF);
}

void EEPROMWriteIntArray(int address, int numbers[], int arraySize)
{
  int addressIndex = address;
  for (int i = 0; i < arraySize; i++)
  {
    EEPROM.write(addressIndex, numbers[i] >> 8);
    EEPROM.write(addressIndex + 1, numbers[i] & 0xFF);
    addressIndex += 2;
  }
}
void EEPROMReadIntArray(int address, int numbers[], int arraySize)
{
  int addressIndex = address;
  for (int i = 0; i < arraySize; i++)
  {
    numbers[i] = (EEPROM.read(addressIndex) << 8) + EEPROM.read(addressIndex + 1);
    addressIndex += 2;
  }
}

String floatToSring(float val) {
  char str[urlMaxLength];
  dtostrf(val, 4, 2, str);
  return str;
}

void handleRoot() {
  time_t tnow = time(nullptr);

  String message = "";
  message += "<form action='save' id='configuration'>";
  message += "<fieldset style='width:710px'>";
  message += "<legend>Configuration</legend>";
  message += "<fieldset>";
  message += "<legend>Temperatures</legend>";
  message += "<label for='dayTemp'>Day:</label><br />";
  message += "<input type='text' name='dayTemp' id='dayTemp' value='" + floatToSring(dayTemp) + "' /><br />";
  message += "<label for='nightSummerTemp'>Night Summer:</label><br />";
  message += "<input type='text' name='nightSummerTemp' id='nightSummerTemp' value='" + floatToSring(nightSummerTemp) + "' /><br />";
  message += "<label for='nightWinterTemp'>Night Winter:</label><br />";
  message += "<input type='text' name='nightWinterTemp' id='nightWinterTemp' value='" + floatToSring(nightWinterTemp) + "' /><br />";
  message += "<label for='offsetTemp'>Offset:</label><br />";
  message += "<input type='text' name='offsetTemp' id='offsetTemp' value='" + floatToSring(offsetTemp) + "' /><br />";
  message += "</fieldset>";
  message += "<fieldset>";
  message += "<legend>Date / Time</legend>";
  message += "<label for='ntpServer'>NTP Server:</label><br />";
  message += "<input type='text' name='ntpServer' id='ntpServer' value='" + ntpServer + "' /><br />";
  message += "<br />";
  message += "<fieldset>";
  message += "<legend>Time of day in Hours</legend>";
  message += "<fieldset style='width:610px'>";
  message += "<legend>Day</legend>";
  message += "<table>";
  message += "<tr>";
  for (byte i = 0; i < 24; i++) {
    message += "<td><label><input type='radio' name='dh" + String(i) + "' value='" + String(i) + "' id='dh" + String(i) + "' onclick='document.getElementById(\"nh" + String(i) + "\").checked = false;'" + String((dayTime[i] != 255) ? "checked" : "") + "/>" + String(i) + "</label></td>";
  }
  message += "</tr>";
  message += "</table>";
  message += "</fieldset>";
  message += "<fieldset style='width:610px'>";
  message += "<legend>Night</legend>";
  message += "<table>";
  for (byte i = 0; i < 24; i++) {
    message += "<td><label><input type='radio' name='nh" + String(i) + "' value='" + String(i) + "' id='nh" + String(i) + "' onclick='document.getElementById(\"dh" + String(i) + "\").checked = false;'" + String((nightTime[i] != 255) ? "checked" : "") + "/>" + String(i) + "</label></td>";
  }
  message += "</tr>";
  message += "</table>";
  message += "</fieldset>";
  message += "</fieldset>";
  message += "<fieldset>";
  message += "<legend>Season in Month</legend>";
  message += "<fieldset style='width:310px'>";
  message += "<legend>Summer</legend>";
  message += "<table>";
  message += "<tr>";
  for (byte i = 0; i < 12; i++) {
    message += "<td><label><input type='radio' name='sm" + String(i) + "' value='" + String(i) + "' id='sm" + String(i) + "' onclick='document.getElementById(\"wm" + String(i) + "\").checked = false;'" + String((summerMonth[i] != 255) ? "checked" : "") + "/>" + String(i + 1) + "</label></td>";
  }
  message += "</tr>";
  message += "</table>";
  message += "</fieldset>";
  message += "<fieldset style='width:310px'>";
  message += "<legend>Winter</legend>";
  message += "<table>";
  for (byte i = 0; i < 12; i++) {
    message += "<td><label><input type='radio' name='wm" + String(i) + "' value='" + String(i) + "' id='wm" + String(i) + "' onclick='document.getElementById(\"sm" + String(i) + "\").checked = false;'" + String((winterMonth[i] != 255) ? "checked" : "") + "/>" + String(i + 1) + "</label></td>";
  }
  message += "</tr>";
  message += "</table>";
  message += "</fieldset>";
  message += "</fieldset>";
  message += "</fieldset>";
  message += "<fieldset>";
  message += "<legend>Info</legend>";
  message += "<table>";
  message += "<tr>";
  message += "<td>Time</td>";
  message += "<td>" + String(asctime(localtime(&tnow))) + "</td>";
  message += "</tr>";
  message += "<tr>";
  message += "<td>Current Temperature</td>";
  message += "<td>" + String(temperature) + " &deg;C</td>";
  message += "</tr>";
  message += "<tr>";
  message += "<td>Target Temperature</td>";
  message += "<td>" + String(targetTemperature) + " &deg;C</td>";
  message += "</tr>";
  message += "<tr>";
  message += "<td>Heater State</td>";
  message += "<td style='background-color:" + String(heaterOn ? "Red" : "SkyBlue") + "'>" + String(heaterOn ? "On" : "Off") + "</td>";
  message += "</tr>";
  message += "<tr>";
  message += "<td>Time of day</td>";
  message += "<td>" + String(isDay ? "Day" : "Night") + "</td>";
  message += "</tr>";
  message += "<tr>";
  message += "<td>Season</td>";
  message += "<td>" + String(isSummer ? "Summer" : "Winter") + "</td>";
  message += "</tr>";
  message += "<tr>";
  message += "<td>Humidity</td>";
  message += "<td>" + String(humidity) + " %</td>";
  message += "</tr>";
  message += "</table>";
  message += "</fieldset>";
  message += "<br />";
  message += "<button type='submit'>Save</button>";
  message += "<button type='button' onclick='location.reload();'>Refresh</button>";
  message += "</fieldset>";
  message += "</form>";
  message += "TerraTemp v.1.0 by Stefan S. (ripper121@gmail.com)";

  server.send(200, "text/html", message);
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void handleSave() {
  String message = "";
  for (byte i = 0; i < 24; i++) {
    dayTime[i] = 255;
    nightTime[i] = 255;
  }
  for (byte i = 0; i < 12; i++) {
    summerMonth[i] = 255;
    winterMonth[i] = 255;
  }

  for (uint8_t i = 0; i < server.args(); i++) {
    if (server.argName(i) == "dayTemp") {
      dayTemp = server.arg(i).toFloat();
    }
    if (server.argName(i) == "nightSummerTemp") {
      nightSummerTemp = server.arg(i).toFloat();
    }
    if (server.argName(i) == "nightWinterTemp") {
      nightWinterTemp = server.arg(i).toFloat();
    }
    if (server.argName(i) == "offsetTemp") {
      offsetTemp = server.arg(i).toFloat();
    }
    if (server.argName(i) == "ntpServer") {
      ntpServer = server.arg(i);
    }

    for (int n = 0; n < 24; n++) {
      String dString = "dh";
      dString += String(n);
      if (server.argName(i) == dString)
        dayTime[n] = server.arg(i).toInt();

      dString = "nh";
      dString += String(n);
      if (server.argName(i) == dString)
        nightTime[n] = server.arg(i).toInt();
    }
    for (int n = 0; n < 13; n++) {
      String dString = "sm";
      dString += String(n);
      if (server.argName(i) == dString)
        summerMonth[n] = server.arg(i).toInt();

      dString = "wm";
      dString += String(n);
      if (server.argName(i) == dString)
        winterMonth[n] = server.arg(i).toInt();
    }
  }

  long addr = 0;
  EEPROMWritefloat(addr, dayTemp);
  addr += floatLength;
  EEPROMWritefloat(addr, nightSummerTemp);
  addr += floatLength;
  EEPROMWritefloat(addr, nightWinterTemp);
  addr += floatLength;
  EEPROMWritefloat(addr, offsetTemp);
  addr += floatLength;
  EEPROMWriteIntArray(addr, dayTime, 24);
  addr += intLength * 24;
  EEPROMWriteIntArray(addr, nightTime, 24);
  addr += intLength * 24;
  EEPROMWriteIntArray(addr, summerMonth, 12);
  addr += intLength * 12;
  EEPROMWriteIntArray(addr, winterMonth, 12);
  addr += intLength * 12;
  WriteStringToEEPROM(addr, ntpServer);
  delay(200);

  if (EEPROM.commit()) {
    message = "EEPROM successfully saved\nReboot\n";
    doReboot = true;
  } else {
    message = "ERROR! EEPROM Save failed";
  }
  server.send(200, "text/plain", message);
}




void handleGet() {
  String message = "";
  time_t tnow = time(nullptr);
  message += asctime(localtime(&tnow));
  message += "<br>";
  message += "<br>Temperature: ";
  message += String(temperature);
  message += "<br>Target Temperature: ";
  message += String(targetTemperature);
  message += "<br>Heater State: ";
  message += String(heaterOn);
  message += "<br>Time: ";
  message += isDay ? "Day" : "Night";
  message += "<br>";
  message += isSummer ? "Summer" : "Winter";
  message += "<br>Humidity: ";
  message += String(humidity);
  message += "<br>NTPServer:";
  message += ntpServer;
  message += "<br>Day/Night:";
  for (byte i = 0; i < 24; i++) {
    message += "D>";
    message += String(dayTime[i]);
    message += ":N>";
    message += String(nightTime[i]);
    message += ", ";
  }
  message += "<br>Summer/Winter:";
  for (byte i = 0; i < 12; i++) {
    message += "S>";
    message += String(summerMonth[i]);
    message += ":W>";
    message += String(winterMonth[i]);
    message += ", ";
  }
  server.send(200, "text/html", message);
}

void setup() {
  pinMode(LED, OUTPUT);
  pinMode(RELAIS, OUTPUT);
  pinMode(BUTTON, INPUT);

  EEPROM.begin(8192);
  delay(200);
  long addr = 0;
  dayTemp = EEPROMReadfloat(addr);
  addr += floatLength;
  nightSummerTemp = EEPROMReadfloat(addr);
  addr += floatLength;
  nightWinterTemp = EEPROMReadfloat(addr);
  addr += floatLength;
  offsetTemp = EEPROMReadfloat(addr);
  addr += floatLength;
  EEPROMReadIntArray(addr, dayTime, 24);
  addr += (intLength * 24);
  EEPROMReadIntArray(addr, nightTime, 24);
  addr += (intLength * 24);
  EEPROMReadIntArray(addr, summerMonth, 12);
  addr += (intLength * 12);
  EEPROMReadIntArray(addr, winterMonth, 12);
  addr += (intLength * 12);
  ntpServer = ReadStringFromEEPROM(addr);

  Serial.begin(115200);
  Serial.println("\n\nBooting");
  Serial.println("Connecting Wifi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin();
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Start SmartConfig...");
    delay(5000);
    WiFi.beginSmartConfig();
    while (1) {
      delay(1000);
      if (WiFi.smartConfigDone()) {
        Serial.println("SmartConfig Success");
        break;
      }
    }
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
      Serial.println("Connection Failed! Rebooting...");
      delay(5000);
      ESP.restart();
    }
  }

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname("terratemp");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }
    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: % u % % \r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[ % u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", handleRoot);
  server.on("/get", handleGet);
  server.on("/save", handleSave);
  server.onNotFound(handleNotFound);

  char buf[urlMaxLength];
  configTime(MYTZ, ntpServer.c_str());
  server.begin();
#ifdef SI7021
  dht.begin();
#endif
#ifdef AM2301
  // Initialize device.
  dht.begin();
  // Print temperature sensor details.
  sensor_t sensor;
  dht.temperature().getSensor(&sensor);
  // Print humidity sensor details.
  dht.humidity().getSensor(&sensor);
  // Set delay between sensor readings based on sensor details.
  delayMS = sensor.min_delay / 1000;
#endif
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  unsigned long currentMillis = millis();

  if (rebootTimer >= 5)
    ESP.restart();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    time_t now;
    struct tm * timeinfo;
    time(&now);
    timeinfo = localtime(&now);
    int timeHour = int(timeinfo->tm_hour);
    int timeMonth = int(timeinfo->tm_mon);
    int timeYear = int(timeinfo->tm_year);

    for (byte i = 0; i < 24; i++) {
      if (timeHour == dayTime[i]) {
        targetTemperature = dayTemp;
        isDay = true;
        break;
      }
    }
    for (byte i = 0; i < 24; i++) {
      if (timeHour == nightTime[i]) {
        isDay = false;
        break;
      }
    }
    for (byte i = 0; i < 12; i++) {
      if (timeMonth == (summerMonth[i])) {
        isSummer = true;
        break;
      }
    }
    for (byte i = 0; i < 12; i++) {
      if (timeMonth == (winterMonth[i])) {
        isSummer = false;
        break;
      }
    }

    if (isDay) //Day Temp Temperature
      targetTemperature = dayTemp;
    if (!isDay && isSummer) //Night Summer Temperature
      targetTemperature = nightSummerTemp;
    if (!isDay && !isSummer) //Night Winter Temperature
      targetTemperature = nightWinterTemp;

    getSensor();

    if (timeYear == 70) {
      targetTemperature = dayTemp;
    }

    if (temperature < targetTemperature)
      heaterOn = true;
    else
      heaterOn = false;

    if (!digitalRead(BUTTON))
      ESP.restart();

    if (doReboot)
      rebootTimer++;

    digitalWrite(LED, !digitalRead(LED));
    digitalWrite(RELAIS, heaterOn);
  }
}
