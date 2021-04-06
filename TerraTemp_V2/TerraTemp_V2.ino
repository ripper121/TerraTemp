#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <LittleFS.h>
#include "base64.h"
#include <sys/time.h> // struct timeval
#include <TZ.h>
#define MYTZ TZ_Europe_London

#define MAX_CHANNELS 5
#define LED 13
#define RELAIS 12
#define BUTTON 0
#define SI7021
//#define AM2301

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
float offsetTemperature = 0.0;
float offsetHumidity = 0.0;

ESP8266WebServer server(80);

String getContentType(String filename); // convert the file extension to the MIME type
bool handleFileRead(String path);       // send the right file to the client (if it exists)

char mySSID[64];
char myPassword[64];
String ntpServer = "pool.ntp.org";

typedef struct
{
  bool activ;
  byte minute[60];
  byte hour[24];
  byte dayOfMonth[31];
  byte month[12];
  byte dayOfWeek[7];
  String channel;
  String function;
  bool onOff;
  float temperature;
  float humidity;
}  timer_entry;

typedef struct
{
  int function;
  float value;
  bool activ;
}  channel_entry;


void getSensor() {

#ifdef SI7021
  dht.read();
  switch (dht.getState()) {
    // всё OK
    case DHT_OK:
      Serial.print("Temperature = ");
      temperature = dht.getTemperatureC() + offsetTemperature;
      Serial.print(temperature);
      Serial.println(" C \t");
      Serial.print("Humidity = ");
      humidity = dht.getHumidity() + offsetHumidity;
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
    temperature = event.temperature + offsetTemperature;
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
    humidity = event.relative_humidity + offsetHumidity;
    Serial.print(humidity);
    Serial.println(F("%"));
  }
#endif
}


bool readFile(const char * path) {
  Serial.printf("Reading file: %s\n", path);

  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return false;
  }

  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
  return true;
}

bool writeFile(const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);

  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return false;
  }

  bool state = false;
  if (file.print(message)) {
    Serial.println("File written");
    state =  true;
  } else {
    Serial.println("Write failed");
  }
  file.close();
  return state;
}

bool appendFile(const char * path, const char * message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = LittleFS.open(path, "a");
  if (!file) {
    Serial.println("Failed to open file for appending");
    return false;
  }
  bool state = false;
  if (file.print(message)) {
    Serial.println("Message appended");
    state =  true;
  } else {
    Serial.println("Append failed");
  }
  file.close();
  return state;
}

bool renameFile(const char * path1, const char * path2) {
  Serial.printf("Renaming file %s to %s\n", path1, path2);
  if (LittleFS.rename(path1, path2)) {
    Serial.println("File renamed");
    return true;
  } else {
    Serial.println("Rename failed");
    return false;
  }
}

bool deleteFile(const char * path) {
  Serial.printf("Deleting file: %s\n", path);
  if (LittleFS.remove(path)) {
    Serial.println("File deleted");
    return true;
  } else {
    Serial.println("Delete failed");
    return false;
  }
}

String getValue(String data, char separator, int index)
{
  int found = 0;
  int strIndex[] = {0, -1};
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

timer_entry readTimerFromFile(int timerCount) {
  timer_entry entry;
  entry.activ = false;
  String timer = "";
  String timerPath = "/timer.txt";

  File file = LittleFS.open(timerPath, "r");
  if (!file) {
    Serial.println("Timer.txt Failed to open file for reading");
    return entry;
  }

  int lineCount = 0;
  while (file.available()) {
    String tmp_timer = file.readStringUntil('\n');
    if (lineCount >= timerCount) {
      timer = tmp_timer;
      break;
    }
    lineCount++;
  }
  file.close();
  Serial.println(timer);

  if (timer == "")
    return entry;

  String minute = getValue(timer, ';', 0);
  if (minute == "*") {
    for (int j = 0; j < 60; j++)
      entry.minute[j] = 1;
  } else {
    for (int j = 0; j < 60; j++)
      entry.minute[j] = 0;
    for (int j = 0; j < 60; j++) {
      if (getValue(minute, ',', j) != "")
        entry.minute[getValue(minute, ',', j).toInt()] = 1;
    }
  }

  String hour = getValue(timer, ';', 1);
  if (hour == "*") {
    for (int j = 0; j < 24; j++)
      entry.hour[j] = 1;
  } else {

    for (int j = 0; j < 24; j++)
      entry.hour[j] = 0;
    for (int j = 0; j < 24; j++) {
      if (getValue(hour, ',', j) != "")
        entry.hour[getValue(hour, ',', j).toInt()] = 1;
    }
  }


  String dayOfMonth = getValue(timer, ';', 2);
  if (dayOfMonth == "*") {
    for (int j = 0; j < 31; j++)
      entry.dayOfMonth[j] = 1;
  } else {
    for (int j = 0; j < 31; j++)
      entry.dayOfMonth[j] = 0;
    for (int j = 0; j < 31; j++) {
      if (getValue(dayOfMonth, ',', j) != "") {
        entry.dayOfMonth[getValue(dayOfMonth, ',', j).toInt() - 1] = 1;
      }
    }

  }


  String month = getValue(timer, ';', 3);
  if (month == "*") {
    for (int j = 0; j < 12; j++)
      entry.month[j] = 1;
  } else {
    for (int j = 0; j < 12; j++)
      entry.month[j] = 0;
    for (int j = 0; j < 12; j++) {
      if (getValue(month, ',', j) != "")
        entry.month[getValue(month, ',', j).toInt() - 1] = 1;
    }
  }


  String dayOfWeek = getValue(timer, ';', 4);
  if (dayOfWeek == "*") {
    for (int j = 0; j < 7; j++)
      entry.dayOfWeek[j] = 1;
  } else {
    for (int j = 0; j < 7; j++)
      entry.dayOfWeek[j] = 0;
    for (int j = 0; j < 7; j++) {
      if (getValue(dayOfWeek, ',', j) != "")
        entry.dayOfWeek[getValue(dayOfWeek, ',', j).toInt() - 1] = 1;
    }
  }


  entry.channel = String(getValue(timer, ';', 5));

  entry.function = String(getValue(timer, ';', 6));

  if (entry.function == "Switch") {
    entry.onOff = getValue(timer, ';', 7).toInt();
  }
  if (entry.function == "Temperature") {
    entry.temperature = getValue(timer, ';', 7).toFloat();
  }
  if (entry.function == "Humidity") {
    entry.humidity = getValue(timer, ';', 7).toFloat();
  }

  entry.activ = true;
  return entry;
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";          // If a folder is requested, send the index file
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if (LittleFS.exists(pathWithGz) || LittleFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
    if (LittleFS.exists(pathWithGz))                         // If there's a compressed version available
      path += ".gz";                                         // Use the compressed version
    File file = LittleFS.open(path, "r");                    // Open the file
    size_t sent = server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);
  return false;                                          // If the file doesn't exist, return false
}

String getContentType(String filename) { // convert the file extension to the MIME type
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

String urldecode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == '+') {
      encodedString += ' ';
    } else if (c == '%') {
      i++;
      code0 = str.charAt(i);
      i++;
      code1 = str.charAt(i);
      c = (h2int(code0) << 4) | h2int(code1);
      encodedString += c;
    } else {

      encodedString += c;
    }
    yield();
  }
  return encodedString;
}

unsigned char h2int(char c)
{
  if (c >= '0' && c <= '9') {
    return ((unsigned char)c - '0');
  }
  if (c >= 'a' && c <= 'f') {
    return ((unsigned char)c - 'a' + 10);
  }
  if (c >= 'A' && c <= 'F') {
    return ((unsigned char)c - 'A' + 10);
  }
  return (0);
}

void handleSave() {
  String state = "";
  Serial.println("handleSave: ");
  String message = urldecode(server.arg("data"));
  Serial.println(message);
  if (message != "")
    if (message == "deleteTimerFile") {
      if (deleteFile("/timer.txt"))
        state = "Delete OK";
      else
        state = "Delete FAILD";
    }
    else
    {
      if (message == "FileEnd") {
        if (LittleFS.exists("/timer.txt")) {
          if (appendFile("/timer.txt", "\n"))
            state = "FileEnd OK";
          else
            state = "FileEnd FAILD";
        }
      } else {
        if (LittleFS.exists("/timer.txt")) {
          if (appendFile("/timer.txt", urldecode(message).c_str()))
            state = "appendFile OK";
          else
            state = "appendFile FAILD";
        } else {
          if (writeFile("/timer.txt", urldecode(message).c_str()))
            state = "writeFile OK";
          else
            state = "writeFile FAILD";
        }
      }
    }
  else {
    state = "Message empty or decode ERROR";
  }
  Serial.println(state);
  server.send(200, "text/plain", state);
}

void setup() {
  pinMode(LED, OUTPUT);
  pinMode(RELAIS, OUTPUT);
  pinMode(BUTTON, INPUT);

  Serial.begin(115200);
  Serial.println();
  Serial.println();
  //Serial.println("Formatting LittleFS filesystem");
  //LittleFS.format();

  Serial.println("Mount LittleFS");
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    return;
  }

  //read Wifi Settings form FS
  String tempSSID, tempPassword;
  File file = LittleFS.open("/wifiSettings.txt", "r");
  if (!file) {
    Serial.println("WifiSettings.txt Failed to open file for reading");
    return;
  }
  while (file.available()) {
    tempSSID = file.readStringUntil('\n');
    tempPassword = file.readStringUntil('\n');
  }
  file.close();
  //write Wifi Settings to char Array
  tempSSID.toCharArray(mySSID, tempSSID.length() + 1);
  tempPassword.toCharArray(myPassword, tempPassword.length() + 1);

  Serial.println();
  // Get all information of your LittleFS
  FSInfo fs_info;
  LittleFS.info(fs_info);
  Serial.println("File sistem info.");
  Serial.print("Total space:      ");
  Serial.print(fs_info.totalBytes);
  Serial.println("byte");

  Serial.print("Total space used: ");
  Serial.print(fs_info.usedBytes);
  Serial.println("byte");

  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(mySSID);
  Serial.println(myPassword);

  WiFi.mode(WIFI_STA);
  WiFi.begin((const char*)mySSID, (const char*)myPassword);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  configTime(MYTZ, ntpServer.c_str());

  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }

  server.on("/save.html", handleSave);
  server.onNotFound([]() {                              // If the client requests any URI
    if (!handleFileRead(server.uri()))                  // send it if it exists
      server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
  });

  server.begin();                           // Actually start the server
  Serial.println("HTTP server started");

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

unsigned long previousMillis = 0;
const long interval = 1000;

/*
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
*/
float targetTemperature = 0.0;
float targetHumidity = 0.0;
bool targetState = LOW;
bool relaisState = LOW;



void loop() {
  // put your main code here, to run repeatedly:
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    time_t now;
    struct tm * timeinfo;
    time(&now);
    timeinfo = localtime(&now);

    timer_entry entry;
    channel_entry channel[MAX_CHANNELS];
    int channelNum = 0;
    for (byte i = 0; i < MAX_CHANNELS; i++) {
      channel[i].function = 0;
      channel[i].value = 0;
      channel[i].activ = false;
    }

    for (int i = 0; i < 100; i++) {
      entry = readTimerFromFile(i);
      if (!entry.activ) {
        break;
      } else {
        if (entry.month[int(timeinfo->tm_mon)] && entry.dayOfMonth[int(timeinfo->tm_mday) - 1] && entry.dayOfWeek[int(timeinfo->tm_wday) - 1] && entry.hour[int(timeinfo->tm_hour) + int(timeinfo->tm_isdst)] && entry.minute[int(timeinfo->tm_min)]) {
          Serial.print("channel ");
          Serial.println(entry.channel);
          if (entry.channel == "Internal")
            channelNum = 0;
          if (entry.channel == "External 0")
            channelNum = 1;
          if (entry.channel == "External 1")
            channelNum = 2;
          if (entry.channel == "External 2")
            channelNum = 3;
          if (entry.channel == "External 3")
            channelNum = 4;

          Serial.print("function ");
          Serial.println(entry.function);

          if (entry.function == "Switch") {
            Serial.print("Switch ");
            Serial.println(entry.onOff);
            channel[channelNum].function = 0;
            channel[channelNum].value = entry.onOff;
            channel[i].activ = true;
          }
          if (entry.function == "Temperature") {
            Serial.print("Temperature ");
            Serial.println(entry.temperature);
            channel[channelNum].function = 1;
            channel[channelNum].value = entry.temperature;
            channel[i].activ = true;
          }
          if (entry.function == "Humidity") {
            Serial.print("Humidity ");
            Serial.println(entry.humidity);
            channel[channelNum].function = 2;
            channel[channelNum].value = entry.humidity;
            channel[i].activ = true;
          }
        }
      }
      Serial.println();

      Serial.print("timeinfo: tm_sec ");
      Serial.print(int(timeinfo->tm_sec));
      Serial.print(",tm_min ");
      Serial.print(int(timeinfo->tm_min));
      Serial.print(",tm_hour ");
      Serial.print(int(timeinfo->tm_hour) + int(timeinfo->tm_isdst));
      Serial.print(",tm_wday ");
      Serial.print(int(timeinfo->tm_wday));
      Serial.print(",tm_mday ");
      Serial.print(int(timeinfo->tm_mday));
      Serial.print(",tm_mon ");
      Serial.print(int(timeinfo->tm_mon));
      Serial.print(",tm_isdst ");
      Serial.println(int(timeinfo->tm_isdst));

    }
    Serial.println();

    getSensor();

    relaisState = LOW;
    for (byte i = 0; i < MAX_CHANNELS; i++) {
      if (i == 0 && channel[i].activ) {
        if (channel[i].function == 0) {
          if (channel[i].value > 0 && channel[i].value < 2) //looks stupid but secure option for float values
            relaisState = HIGH;
          else
            relaisState = LOW;
        }
        if (channel[i].function == 1) {
          if (int(timeinfo->tm_year) == 70) {
            //targetTemperature = ; //set a backup Temperature if no NTP Server was reached
          }
          if (temperature < channel[i].value)
            relaisState = HIGH;
          else
            relaisState = LOW;
        }
        if (channel[i].function == 2) {
          if (int(timeinfo->tm_year) == 70) {
            //targetHumidity = ; //set a backup humidity if no NTP Server was reached
          }
          if (humidity < channel[i].value)
            relaisState = HIGH;
          else
            relaisState = LOW;
        }
      }

      if (i > 0  && channel[i].activ) {
        if (channel[i].function == 0) {
          if (channel[i].value > 0 && channel[i].value < 2) { //looks stupid but secure option for float values
            //send GET request to HTTP-Link defined via settings
          } else {
            //send GET request to HTTP-Link defined via settings
          }
        }
        if (channel[i].function == 1) {
          if (int(timeinfo->tm_year) == 70) {
            //targetTemperature = ; //set a backup Temperature if no NTP Server was reached
          }
          if (temperature < channel[i].value) {
            //send GET request to HTTP-Link defined via settings
          } else {
            //send GET request to HTTP-Link defined via settings
          }
        }
        if (channel[i].function == 2) {
          if (int(timeinfo->tm_year) == 70) {
            //targetHumidity = ; //set a backup humidity if no NTP Server was reached
          }
          if (humidity < channel[i].value) {
            //send GET request to HTTP-Link defined via settings
          } else {
            //send GET request to HTTP-Link defined via settings

          }
        }
      }

    }

    digitalWrite(LED, !digitalRead(LED));
    digitalWrite(RELAIS, relaisState);
  }

  server.handleClient();
  MDNS.update();
}
