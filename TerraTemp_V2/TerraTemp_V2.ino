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

ESP8266WebServer server(80);

String getContentType(String filename); // convert the file extension to the MIME type
bool handleFileRead(String path);       // send the right file to the client (if it exists)

char mySSID[64];
char myPassword[64];
String ntpServer = "pool.ntp.org";

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

bool relaisState = LOW;
bool channelState = LOW;
bool _HIGH = HIGH;
bool _LOW = LOW;
struct tm * timeinfo;
String activeTimerInfo = "";

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

typedef struct
{
  String wifi_ssid;
  String wifi_psk;
  String ntpServer;
  String timeZone;
  String timeZoneText;
  float hysteresisTemperature;
  float hysteresisHumidity;
  float temperatureOffset;
  float humidityOffset;
  bool invertInternalOutput;
  bool outputOnSensorFail;
  float failsafeTemperature;
  float failsafeHumidity;
  int logInterval;
  String logHttpLink;
  String thingspeakLink;
  String channel1Link;
  String channel2Link;
  String channel3Link;
  String channel4Link;
}  settings_entry;
settings_entry settings;

void yieldServer() {
  server.handleClient();
  MDNS.update();
  yield();
}


void getSensor() {
  temperature = 0.0;
  humidity = 0.0;
#ifdef SI7021
  dht.read();
  switch (dht.getState()) {
    // всё OK
    case DHT_OK:
      temperature = dht.getTemperatureC() + settings.temperatureOffset;
      humidity = dht.getHumidity() + settings.humidityOffset;
      break;
    case DHT_ERROR_CHECKSUM:
      temperature = 0.0;
      humidity = 0.0;
      Serial.println("Checksum error");
      break;
    case DHT_ERROR_TIMEOUT:
      temperature = 0.0;
      humidity = 0.0;
      Serial.println("Time out error");
      break;
    case DHT_ERROR_NO_REPLY:
      temperature = 0.0;
      humidity = 0.0;
      Serial.println("Sensor not connected");
      break;
  }
#endif
#ifdef AM2301
  // Get temperature event and print its value.
  sensors_event_t event;
  dht.temperature().getEvent(&event);
  if (isnan(event.temperature)) {
    temperature = 0.0;
    Serial.println(F("Error reading temperature!"));
  }
  else {
    temperature = event.temperature + settings.temperatureOffset;
  }
  // Get humidity event and print its value.
  dht.humidity().getEvent(&event);
  if (isnan(event.relative_humidity)) {
    humidity = 0.0;
    Serial.println(F("Error reading humidity!"));
  }
  else {
    humidity = event.relative_humidity + settings.humidityOffset;
  }
#endif
  yieldServer();
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
    yieldServer();
  }

  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

timer_entry readTimerFromFile(int timerCount) {
  timer_entry entry;
  entry.activ = false;
  String timer = "";
  String timerPath = "/timer.conf";

  File file = LittleFS.open(timerPath, "r");
  if (!file) {
    Serial.println("Timer.conf Failed to open file for reading");
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
  else if (filename.endsWith(".conf")) return "application/octet-stream";
  else if (filename.endsWith(".gz")) return "application/x-gzip";
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
    yieldServer();
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

void handleStatus() {
  String state = "";
  Serial.println("handleSave: ");
  String message = urldecode(server.arg("data"));
  Serial.println(message);
  if (message == "getStatus") {
    message = String(int(timeinfo->tm_hour));
    message += ":";
    message += String(timeinfo->tm_min);
    message += ":";
    message += String(timeinfo->tm_sec);
    message += " ";
    message += String(timeinfo->tm_mday);
    message += ".";
    message += String(int(timeinfo->tm_mon) + 1);
    message += ".";
    message += String(int(timeinfo->tm_year) + 1900);
    message += " |Daylight Saving Time->";
    message += String(int(timeinfo->tm_isdst));
    message += "| |Weekday->";
    message += String(int(timeinfo->tm_wday));

    message += "|\n\nTemperature: ";
    message += String(temperature);
    message += "°C\nHumidity: ";
    message += String(humidity);

    message += "%\n\n";
    message += activeTimerInfo;
  }
  Serial.println(message);
  server.send(200, "text/plain", message);
}

void handleSaveTimer() {
  String state = "";
  Serial.println("handleSave: ");
  String message = urldecode(server.arg("data"));
  Serial.println(message);
  if (message != "")
    if (message == "deleteTimerFile") {
      if (deleteFile("/timer.conf"))
        state = "Delete OK";
      else
        state = "Delete FAILD";
    }
    else
    {
      if (message == "FileEnd") {
        if (LittleFS.exists("/timer.conf")) {
          if (appendFile("/timer.conf", "\n"))
            state = "FileEnd OK";
          else
            state = "FileEnd FAILD";
        }
      } else {
        if (LittleFS.exists("/timer.conf")) {
          if (appendFile("/timer.conf", urldecode(message).c_str()))
            state = "appendFile OK";
          else
            state = "appendFile FAILD";
        } else {
          if (writeFile("/timer.conf", urldecode(message).c_str()))
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

void readSettingsFile() {
  Serial.println("Reading Settings from settings.conf");
  File file = LittleFS.open("/settings.conf", "r");
  if (!file) {
    Serial.println("settings.conf Failed to open file for reading");
    return;
  }

  byte settingsCounter = 0;
  while (true) {
    if (file.available()) {
      if (settingsCounter == 0)
        settings.wifi_ssid = file.readStringUntil('\n');
      if (settingsCounter == 1)
        settings.wifi_psk = file.readStringUntil('\n');
      if (settingsCounter == 2)
        settings.ntpServer = file.readStringUntil('\n');
      if (settingsCounter == 3)
        settings.timeZone = file.readStringUntil('\n');
      if (settingsCounter == 4)
        settings.timeZoneText = file.readStringUntil('\n');
      if (settingsCounter == 5)
        settings.hysteresisTemperature = file.readStringUntil('\n').toFloat();
      if (settingsCounter == 6)
        settings.hysteresisHumidity = file.readStringUntil('\n').toFloat();
      if (settingsCounter == 7)
        settings.temperatureOffset = file.readStringUntil('\n').toFloat();
      if (settingsCounter == 8)
        settings.humidityOffset = file.readStringUntil('\n').toFloat();
      if (settingsCounter == 9)
        settings.invertInternalOutput = file.readStringUntil('\n').toInt();
      if (settingsCounter == 10)
        settings.outputOnSensorFail = file.readStringUntil('\n').toInt();
      if (settingsCounter == 11)
        settings.failsafeTemperature = file.readStringUntil('\n').toFloat();
      if (settingsCounter == 12)
        settings.failsafeHumidity = file.readStringUntil('\n').toFloat();
      if (settingsCounter == 13)
        settings.logInterval = file.readStringUntil('\n').toInt();
      if (settingsCounter == 14)
        settings.logHttpLink = file.readStringUntil('\n');
      if (settingsCounter == 15)
        settings.thingspeakLink = file.readStringUntil('\n');
      if (settingsCounter == 16)
        settings.channel1Link = file.readStringUntil('\n');
      if (settingsCounter == 17)
        settings.channel2Link = file.readStringUntil('\n');
      if (settingsCounter == 18)
        settings.channel3Link = file.readStringUntil('\n');
      if (settingsCounter == 19) {
        settings.channel4Link = file.readStringUntil('\n');
        break;
      }
    }
    settingsCounter++;
  }
  file.close();

  if (settings.invertInternalOutput) {
    _HIGH = LOW;
    _LOW = HIGH;
  } else {
    _HIGH = HIGH;
    _LOW = LOW;
  }
}

void handleSaveSetting() {
  String state = "";
  Serial.println("handleSaveSettings: ");
  String message = urldecode(server.arg("data"));
  Serial.println(message);
  if (message != "")
    if (message == "deleteSettingsFile") {
      if (deleteFile("/settings.conf"))
        state = "Delete OK";
      else
        state = "Delete FAILD";
    }
    else
    {
      if (LittleFS.exists("/settings.conf")) {
        if (appendFile("/settings.conf", urldecode(message).c_str()))
          state = "appendFile OK";
        else
          state = "appendFile FAILD";
      } else {
        if (writeFile("/settings.conf", urldecode(message).c_str()))
          state = "writeFile OK";
        else
          state = "writeFile FAILD";
      }
      readSettingsFile();
    }
  else {
    state = "Message empty or decode ERROR";
  }
  Serial.println(state);
  server.send(200, "text/plain", state);
}

File UploadFile;
void handleFileUpload() { // upload a new file to the Filing system
  Serial.print("handleFileUpload");
  String webpage;
  HTTPUpload& uploadfile = server.upload(); // See https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer/srcv
  // For further information on 'status' structure, there are other reasons such as a failed transfer that could be used
  if (uploadfile.status == UPLOAD_FILE_START)
  {
    String filename = uploadfile.filename;
    if (!filename.startsWith("/")) filename = "/" + filename;
    Serial.print("Upload File Name: "); Serial.println(filename);
    if (LittleFS.exists(filename))
      LittleFS.remove(filename);                         // Remove a previous version, otherwise data is appended the file again
    UploadFile = LittleFS.open(filename, "w");  // Open the file for writing in SPIFFS (create it, if doesn't exist)
    filename = String();
  }
  else if (uploadfile.status == UPLOAD_FILE_WRITE)
  {
    if (UploadFile) UploadFile.write(uploadfile.buf, uploadfile.currentSize); // Write the received bytes to the file
  }
  else if (uploadfile.status == UPLOAD_FILE_END)
  {
    if (UploadFile)         // If the file was successfully created
    {
      UploadFile.close();   // Close the file again
      Serial.print("Upload Size: "); Serial.println(uploadfile.totalSize);
      server.sendHeader("Location", "/");     // Redirect the client to the success page
      server.send(303);
    }
    else
    {
      Serial.print("Upload ERROR");
      webpage = "";
      webpage += F("<h3>Error on uploaded</h3>");
      server.send(500, "text/html", webpage);
    }
  }
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

  readSettingsFile();
  relaisState = _LOW;
  delay(1000);

  //write Wifi Settings to char Array
  //String(settings.ssid).toCharArray(mySSID, (String(settings.ssid).length() + 1));
  //String(settings.psk).toCharArray(myPassword, (String(settings.psk).length() + 1));

  Serial.println();
  // Get all information of your LittleFS
  FSInfo fs_info;
  LittleFS.info(fs_info);
  Serial.println("File system info.");
  Serial.print("Total space:      ");
  Serial.print(fs_info.totalBytes);
  Serial.println("byte");

  Serial.print("Total space used: ");
  Serial.print(fs_info.usedBytes);
  Serial.println("byte");

  Serial.print("Total space free: ");
  Serial.print(fs_info.totalBytes - fs_info.usedBytes);
  Serial.println("byte");

  Serial.println();
  Serial.println("Connecting to ");
  Serial.println(settings.wifi_ssid);
  Serial.println(settings.wifi_psk);

  WiFi.mode(WIFI_STA);
  WiFi.begin(settings.wifi_ssid, settings.wifi_psk);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());

  configTime(settings.timeZone.c_str(), settings.ntpServer.c_str());

  if (MDNS.begin("esp8266")) {
    Serial.println("MDNS responder started");
  }

  server.on("/getStatus.html", handleStatus);
  server.on("/saveSetting.html", handleSaveSetting);
  server.on("/saveTimer.html", handleSaveTimer);
  server.on("/fupload", HTTP_POST, []() {
    server.send(200);
  }, handleFileUpload);
  server.onNotFound([]() {                              // If the client requests any URI
    if (!handleFileRead(server.uri()))                  // send it if it exists
      server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
  });
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.setDebugOutput(true);
      WiFiUDP::stopAll();
      Serial.printf("Update: %s\n", upload.filename.c_str());
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)) { //start with max available size
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) { //true to set the size to the current progress
        Serial.printf("Update Success: %u\nRebooting...\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
      Serial.setDebugOutput(true);
    }
    yield();
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



void loop() {
  // put your main code here, to run repeatedly:
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    time_t now;
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

    activeTimerInfo = "Current activ Timers:\n";
    for (int i = 0; i < 100; i++) {
      entry = readTimerFromFile(i);
      if (!entry.activ) {
        break;
      } else {
        if (entry.month[int(timeinfo->tm_mon)] && entry.dayOfMonth[int(timeinfo->tm_mday) - 1] && entry.dayOfWeek[int(timeinfo->tm_wday) - 1] && entry.hour[int(timeinfo->tm_hour)] && entry.minute[int(timeinfo->tm_min)]) {
          activeTimerInfo += "channel: ";
          activeTimerInfo += String(entry.channel);
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

          activeTimerInfo += ",function: ";
          activeTimerInfo += String(entry.function);
          activeTimerInfo += ", ";
          if (entry.function == "Switch") {
            activeTimerInfo += String(entry.onOff);
            channel[channelNum].function = 0;
            channel[channelNum].value = entry.onOff;
            channel[i].activ = true;
          }
          if (entry.function == "Temperature") {
            activeTimerInfo += String(entry.temperature);
            activeTimerInfo += "°C";
            channel[channelNum].function = 1;
            channel[channelNum].value = entry.temperature;
            channel[i].activ = true;
          }
          if (entry.function == "Humidity") {
            activeTimerInfo += String(entry.humidity);
            activeTimerInfo += "%";
            channel[channelNum].function = 2;
            channel[channelNum].value = entry.humidity;
            channel[i].activ = true;
          }
          activeTimerInfo += "\n";
        }
      }

      Serial.println(activeTimerInfo);

      Serial.print("timeinfo: tm_sec ");
      Serial.print(int(timeinfo->tm_sec));
      Serial.print(",tm_min ");
      Serial.print(int(timeinfo->tm_min));
      Serial.print(",tm_hour ");
      Serial.print(int(timeinfo->tm_hour));
      Serial.print(",tm_wday ");
      Serial.print(int(timeinfo->tm_wday));
      Serial.print(",tm_mday ");
      Serial.print(int(timeinfo->tm_mday));
      Serial.print(",tm_mon ");
      Serial.print(int(timeinfo->tm_mon));
      Serial.print(",tm_isdst ");
      Serial.println(int(timeinfo->tm_isdst));

      yieldServer();
    }
    Serial.println();

    getSensor();
    Serial.print(F("Humidity: "));
    Serial.print(humidity);
    Serial.println(F("%"));
    Serial.print(F("Temperature: "));
    Serial.print(temperature);
    Serial.println(F("°C"));

    for (byte i = 0; i < MAX_CHANNELS; i++) {
      if (i == 0 && channel[i].activ) {
        Serial.print("Channel");
        Serial.print(i);
        if (channel[i].function == 0) {
          if (channel[i].value > 0 && channel[i].value < 2) { //looks stupid but secure option for float values
            relaisState = _HIGH;
            Serial.println(" ON");
          } else {
            relaisState = _LOW;
            Serial.println(" OFF");
          }
        }
        if (channel[i].function == 1) {
          if (int(timeinfo->tm_year) == 70) {
            temperature = settings.failsafeTemperature; //set a backup Temperature if no NTP Server was reached
          }
          if ((temperature) <= (channel[i].value - settings.hysteresisTemperature)) {
            relaisState = _HIGH;
            Serial.println(" ON");
          } else {
            if ((temperature) >= (channel[i].value + settings.hysteresisTemperature)) {
              relaisState = _LOW;
              Serial.println(" OFF");
            }
          }
        }
        if (channel[i].function == 2) {
          if (int(timeinfo->tm_year) == 70) {
            humidity = settings.failsafeHumidity; //set a backup humidity if no NTP Server was reached
          }
          if ((humidity) <= (channel[i].value - settings.hysteresisHumidity)) {
            relaisState = _HIGH;
            Serial.println(" ON");
          } else {
            if ((humidity) >= (channel[i].value + settings.hysteresisHumidity)) {
              relaisState = _LOW;
              Serial.println(" OFF");
            }
          }
        }
        if (temperature <= 0 || humidity <= 0)
          relaisState = settings.outputOnSensorFail;
      }

      if (i > 0  && channel[i].activ) {
        Serial.print("Channel");
        Serial.print(i);
        if (channel[i].function == 0) {
          if (channel[i].value > 0 && channel[i].value < 2) { //looks stupid but secure option for float values
            channelState = HIGH;
            Serial.println(" ON");//send GET request to HTTP-Link defined via settings
          } else {
            channelState = LOW;
            Serial.println(" OFF");//send GET request to HTTP-Link defined via settings
          }
        }
        if (channel[i].function == 1) {
          if (int(timeinfo->tm_year) == 70) {
            temperature = settings.failsafeTemperature; //set a backup Temperature if no NTP Server was reached
          }
          if ((temperature) <= (channel[i].value - settings.hysteresisTemperature)) {
            channelState = HIGH;
            Serial.println(" ON");
          } else {
            if ((temperature) >= (channel[i].value + settings.hysteresisTemperature)) {
              channelState = LOW;
              Serial.println(" OFF");
            }
          }
        }
        if (channel[i].function == 2) {
          if (int(timeinfo->tm_year) == 70) {
            humidity = settings.failsafeHumidity; //set a backup humidity if no NTP Server was reached
          }
          if ((humidity) <= (channel[i].value - settings.hysteresisHumidity)) {
            channelState = HIGH;
            Serial.println(" ON");
          } else {
            if ((humidity) >= (channel[i].value + settings.hysteresisHumidity)) {
              channelState = LOW;
              Serial.println(" OFF");
            }
          }
        }
      }

      if (temperature <= 0 || humidity <= 0) {
        channelState = settings.outputOnSensorFail;
      }

      // wget("TargetIP/index.html?channel" + i + "=" + channelState; //Send Httprequest
    }

    digitalWrite(RELAIS, relaisState);
    digitalWrite(LED, !digitalRead(LED));
  }

  yieldServer();
}
