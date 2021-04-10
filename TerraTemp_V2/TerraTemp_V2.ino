#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ESP8266HTTPClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <sys/time.h> // struct timeval
#include <TZ.h>
#define MYTZ TZ_Europe_London
const char compile_date[] = __FILE__ " " __DATE__ " " __TIME__;

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
const char* domainName = "terra-maid";  // set domain name domain.local

const byte DNS_PORT = 53;
//IPAddress apIP(8, 8, 4, 4); // The default android DNS
DNSServer dnsServer;
bool wifiModeAP = false;

#define MAX_CHANNELS 5
#define MAX_TIMERS 100

#define LED 13
#define RELAIS 12
#define BUTTON 0

bool channelState = LOW;
bool relaisState = LOW;
bool _HIGH = HIGH;
bool _LOW = LOW;

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

unsigned long previousMillis = 0;
unsigned long timerPreviousMillis = 0;
unsigned long logPreviousMillis = 0;
unsigned long timerInterval = 10000;
unsigned long interval = 1000;

struct tm * timeinfo;
String activeTimerInfo = "";

File UploadFile;

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
  unsigned int logInterval;
  String logHttpLink;
  String thingspeakChannelID;
  String thingspeakWriteAPIKey;
  String channel1Link;
  String channel2Link;
  String channel3Link;
  String channel4Link;
}  settings_entry;
settings_entry settings;

void yieldServer() {
  MDNS.update();
  if (wifiModeAP)
    dnsServer.processNextRequest();
  server.handleClient();
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
  Serial.print("readTimerFromFile: ");
  Serial.println(timerCount);
  File file = LittleFS.open(timerPath, "r");
  if (!file) {
    Serial.println("Timer.conf Failed to open file for reading");
    return entry;
  }

  int lineCount = 0;
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (file.available()) {
      String tmp_timer = file.readStringUntil('\n');
      if (lineCount >= timerCount) {
        timer = tmp_timer;
        break;
      }
      lineCount++;
    } else {
      break;
    }
  }
  file.close();

  if (timer == "") {
    Serial.println("No more Timers");
    return entry;
  }

  Serial.println(timer);

  Serial.println("String to Struct");

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

bool handleFileRead(String path, bool modeAP) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (modeAP) {
    if (path.endsWith("/")) path += "indexAP.html";          // If a folder is requested, send the index file
  }
  else {
    if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  }
  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if (LittleFS.exists(pathWithGz) || LittleFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
    if (LittleFS.exists(pathWithGz))                         // If there's a compressed version available
      path += ".gz";                                         // Use the compressed version
    File file = LittleFS.open(path, "r");                    // Open the file
    server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    Serial.println(String("\tSent file: ") + path);
    return true;
  }
  Serial.println(String("\tFile Not Found: ") + path);
  return false;                                          // If the file doesn't exist, return false
}

void handleRootAP() {
  String path = "/indexAP.html";
  Serial.println("handleRootAP: " + path);

  String contentType = getContentType(path);             // Get the MIME type
  String pathWithGz = path + ".gz";
  if (LittleFS.exists(pathWithGz) || LittleFS.exists(path)) { // If the file exists, either as a compressed archive, or normal
    if (LittleFS.exists(pathWithGz))                         // If there's a compressed version available
      path += ".gz";                                         // Use the compressed version
    File file = LittleFS.open(path, "r");                    // Open the file
    server.streamFile(file, contentType);    // Send it to the client
    file.close();                                          // Close the file again
    Serial.println(String("\tSent file: ") + path);
  } else {
    Serial.println(String("\tFile Not Found: ") + path);                                      // If the file doesn't exist, return false
  }
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

String urlencode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (unsigned int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (c == ' ') {
      encodedString += '+';
    } else if (isalnum(c)) {
      encodedString += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) {
        code1 = (c & 0xf) - 10 + 'A';
      }
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) {
        code0 = c - 10 + 'A';
      }
      encodedString += '%';
      encodedString += code0;
      encodedString += code1;
    }
    yield();
  }
  return encodedString;

}

String urldecode(String str)
{
  String encodedString = "";
  char c;
  char code0;
  char code1;
  for (unsigned int i = 0; i < str.length(); i++) {
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
    if (int(timeinfo->tm_isdst))
      message += " |Daylight Saving Time|";
    message += " |Weekday->";
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

void handleReboot() {
  String state = "";
  Serial.println("handleReboot: ");
  String message = urldecode(server.arg("data"));
  Serial.println(message);
  if (message == "reboot") {
    server.send(200, "text/plain", "Reboot Now!");
    delay(100);
    ESP.restart();
  } else {
    server.send(200, "text/plain", "Wrong Command!");
  }
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

/*
  bool file_exists(String path) {
  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.print(path);
    Serial.println(" not exists");
    return false;
  }
  Serial.print(path);
  Serial.println(" exists");
  return true;
  }
*/
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
        settings.thingspeakChannelID = file.readStringUntil('\n');
      if (settingsCounter == 16)
        settings.thingspeakWriteAPIKey = file.readStringUntil('\n');
      if (settingsCounter == 17)
        settings.channel1Link = file.readStringUntil('\n');
      if (settingsCounter == 18)
        settings.channel2Link = file.readStringUntil('\n');
      if (settingsCounter == 19)
        settings.channel3Link = file.readStringUntil('\n');
      if (settingsCounter == 20) {
        settings.channel4Link = file.readStringUntil('\n');
        break;
      }
    } else {
      break;
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
    UploadFile = LittleFS.open(filename, "w");  // Open the file for writing in LittleFS (create it, if doesn't exist)
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

int httpGet(String address, int timeout) {
  WiFiClient client;
  HTTPClient http;

  Serial.print("address: ");
  Serial.println(address);

  http.setTimeout(timeout);
  http.begin(client, address);
  int httpCode = http.GET();                                  //Send the request

  if (httpCode > 0) {
    String payload = http.getString();
    Serial.print("httpCode: ");
    Serial.println(httpCode);
    //Serial.print("payload: ");
    //Serial.println(payload);
  } else {
    Serial.print("HTTP-Error: ");
    Serial.println(http.errorToString(httpCode));
  }
  http.end();   //Close connection

  return httpCode;
}

String getAPName() {
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toUpperCase();
  return domainName + macID;
}

void setup() {
  pinMode(LED, OUTPUT);
  pinMode(RELAIS, OUTPUT);
  pinMode(BUTTON, INPUT);

  Serial.begin(115200);
  Serial.println();
  Serial.println();
  Serial.println(compile_date);

  Serial.println("Mount LittleFS");
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    return;
  }
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


  digitalWrite(LED, LOW);
  delay(1000);
  digitalWrite(LED, HIGH);
  delay(1000);
  if (LittleFS.exists("/settings.conf")) {
    for (byte i = 0; i < 3; i++) {
      digitalWrite(LED, LOW);
      if (digitalRead(BUTTON)) {
        wifiModeAP = false;
      } else {
        wifiModeAP = true;
      }
      delay(1000);
    }
    digitalWrite(LED, HIGH);

    readSettingsFile();
    relaisState = _LOW;
  } else {
    wifiModeAP = true;
  }

  WiFi.disconnect();
  IPAddress myIP;
  if (!wifiModeAP) {
    Serial.println("Connecting to ");
    Serial.println(settings.wifi_ssid);
    Serial.println(settings.wifi_psk);

    WiFi.mode(WIFI_STA);
    WiFi.hostname(getAPName());
    WiFi.begin(settings.wifi_ssid, settings.wifi_psk);

    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    configTime(settings.timeZone.c_str(), settings.ntpServer.c_str());

    myIP = WiFi.localIP();
    Serial.print("Local IP address: ");
  } else {
    interval = 100;
    Serial.println("AP Mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(domainName);
    //WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    myIP = WiFi.softAPIP();
    // if DNSServer is started with "*" for domain name, it will reply with
    // provided IP to all DNS request
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", myIP);
    Serial.print("AP IP address: ");
  }

  Serial.println(myIP);

  if (!MDNS.begin(domainName, myIP)) {
    Serial.println("[ERROR] MDNS responder did not setup");
  } else {
    Serial.println("[INFO] MDNS setup is successful!");
    Serial.print("You can reach it via http://");
    Serial.print(domainName);
    Serial.println(".local");
    MDNS.addService("http", "tcp", 80);
  }

  if (!wifiModeAP) {
    server.on("/getStatus.html", handleStatus);
    server.on("/saveTimer.html", handleSaveTimer);
  } else {
    server.on("/gen_204", handleRootAP);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
    server.on("/generate_204", handleRootAP);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
    server.on("/fwlink", handleRootAP);  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  }

  server.on("/reboot.html", handleReboot);
  server.on("/version", []() {
    server.send(200, "text/plain", compile_date);
  });
  server.onNotFound([]() {                              // If the client requests any URI
    if (!handleFileRead(server.uri(), wifiModeAP))                 // send it if it exists
      server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
  });
  server.on("/saveSetting.html", handleSaveSetting);
  server.on("/fupload", HTTP_POST, []() {
    server.send(200);
  }, handleFileUpload);

  httpUpdater.setup(&server);
  server.begin();                           // Actually start the server
  Serial.println("HTTP server started");
  Serial.println();
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

channel_entry lastChannel[MAX_CHANNELS];

void loop() {
  // put your main code here, to run repeatedly:
  unsigned long currentMillis = millis();
  if (currentMillis - timerPreviousMillis >= timerInterval) {
    timerPreviousMillis = currentMillis;
    if (!wifiModeAP) {
      Serial.println();
      Serial.println("----------START----------");
      Serial.println();

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

      Serial.println("Read Timers from File to Struct");
      activeTimerInfo = "";
      for (int i = 0; i < MAX_TIMERS; i++) {
        entry = readTimerFromFile(i);
        if (entry.activ) {
          Serial.print("entry.month[int(timeinfo->tm_mon)] ");
          Serial.print(entry.month[int(timeinfo->tm_mon)], DEC);
          Serial.print(", entry.dayOfMonth[int(timeinfo->tm_mday) - 1] ");
          Serial.print(entry.dayOfMonth[int(timeinfo->tm_mday) - 1], DEC);
          Serial.print(", entry.dayOfWeek[int(timeinfo->tm_wday) - 1] ");
          Serial.print(entry.dayOfWeek[int(timeinfo->tm_wday) - 1], DEC);
          Serial.print(", entry.hour[int(timeinfo->tm_hour)] ");
          Serial.print(entry.hour[int(timeinfo->tm_hour)], DEC);
          Serial.print(", entry.minute[int(timeinfo->tm_min)] ");
          Serial.println(entry.minute[int(timeinfo->tm_min)], DEC);
          if (entry.month[int(timeinfo->tm_mon)] && entry.dayOfMonth[int(timeinfo->tm_mday) - 1] && entry.dayOfWeek[int(timeinfo->tm_wday) - 1] && entry.hour[int(timeinfo->tm_hour)] && entry.minute[int(timeinfo->tm_min)]) {
            activeTimerInfo += "channel: ";
            activeTimerInfo += String(entry.channel);
            if (entry.channel == "Internal")
              channelNum = 0;
            if (entry.channel == "External0")
              channelNum = 1;
            if (entry.channel == "External1")
              channelNum = 2;
            if (entry.channel == "External2")
              channelNum = 3;
            if (entry.channel == "External3")
              channelNum = 4;

            activeTimerInfo += ", function: ";
            activeTimerInfo += String(entry.function);
            activeTimerInfo += ", value: ";
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
        } else {
          break;
        }
        yieldServer();
      }
      Serial.println();
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
      Serial.println();

      getSensor();
      Serial.print(F("Humidity: "));
      Serial.print(humidity);
      Serial.println(F("%"));
      Serial.print(F("Temperature: "));
      Serial.print(temperature);
      Serial.println(F("°C"));
      Serial.println();

      Serial.println("Read Channel Entrys from Struct");
      for (byte i = 0; i < MAX_CHANNELS; i++) {
        Serial.print("Channel");
        Serial.print(i);
        Serial.print(", activ ");
        Serial.print(channel[i].activ, DEC);
        Serial.print(", function ");
        Serial.print(channel[i].function, DEC);
        Serial.print(", value ");
        Serial.println(String(channel[i].value));
      }
      Serial.println();

      for (byte i = 0; i < MAX_CHANNELS; i++) {
        if (i == 0) {
          Serial.print("Internal");
          Serial.print(i);
          Serial.print(" ");
          if (channel[i].function == 0) {
            Serial.print("Switch: ");
            if (channel[i].value > 0 && channel[i].value < 2) { //looks stupid but secure option for float values
              relaisState = _HIGH;
              Serial.println(" ON");
            } else {
              relaisState = _LOW;
              Serial.println(" OFF");
            }
          }
          if (channel[i].function == 1) {
            Serial.print("Temperature: ");
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
            Serial.print("Humidity: ");
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
          if ((temperature <= 0 || humidity <= 0) && (channel[i].function != 0))
            relaisState = settings.outputOnSensorFail;
        }
        Serial.println();

        if (i > 0) {
          bool doHttpGet = false;
          Serial.print("External");
          Serial.print(i - 1);
          Serial.print(" ");
          if (channel[i].function == 0) {
            Serial.print("Switch: ");
            if (channel[i].value > 0 && channel[i].value < 2) { //looks stupid but secure option for float values
              doHttpGet = true;
              channelState = HIGH;
              Serial.println(" ON");
            } else {
              doHttpGet = true;
              channelState = LOW;
              Serial.println(" OFF");
            }
          }
          if (channel[i].function == 1) {
            Serial.print("Temperature: ");
            if (int(timeinfo->tm_year) == 70) {
              temperature = settings.failsafeTemperature; //set a backup Temperature if no NTP Server was reached
            }
            if ((temperature) <= (channel[i].value - settings.hysteresisTemperature)) {
              doHttpGet = true;
              channelState = HIGH;
              Serial.println(" ON");
            } else {
              if ((temperature) >= (channel[i].value + settings.hysteresisTemperature)) {
                doHttpGet = true;
                channelState = LOW;
                Serial.println(" OFF");
              }
            }
          }
          if (channel[i].function == 2) {
            Serial.print("Humidity: ");
            if (int(timeinfo->tm_year) == 70) {
              humidity = settings.failsafeHumidity; //set a backup humidity if no NTP Server was reached
            }
            if ((humidity) <= (channel[i].value - settings.hysteresisHumidity)) {
              doHttpGet = true;
              channelState = HIGH;
              Serial.println(" ON");
            } else {
              if ((humidity) >= (channel[i].value + settings.hysteresisHumidity)) {
                doHttpGet = true;
                channelState = LOW;
                Serial.println(" OFF");
              }
            }
          }
          if ((temperature <= 0 || humidity <= 0) && (channel[i].function != 0)) {
            doHttpGet = true;
            channelState = settings.outputOnSensorFail;
          }

          if (doHttpGet) {
            String address = "";
            if (i == 1) {
              address += settings.channel1Link;
            }
            if (i == 2) {
              address += settings.channel2Link;
            }
            if (i == 3) {
              address += settings.channel3Link;
            }
            if (i == 4) {
              address += settings.channel4Link;
            }
            if (address != "") {
              address += "/index.html?channel" + urlencode(String(i)) + "=";
              if (channelState == HIGH)
                address += urlencode("1");
              else
                address += urlencode("0");
              httpGet(address, 3000);
            }
          }
        }
      }

      Serial.println();
      Serial.println("----------END----------");
      Serial.println();
    }
  }

  if ((currentMillis - logPreviousMillis >= (settings.logInterval * 1000)) && (settings.logInterval > 0) && (WiFi.status() == WL_CONNECTED)) {
    logPreviousMillis = currentMillis;
    if (settings.thingspeakChannelID != "" && settings.thingspeakWriteAPIKey != "") {
      Serial.print("thingspeakChannelID: ");
      Serial.println(settings.thingspeakChannelID);
      Serial.print("thingspeakWriteAPIKey: ");
      Serial.println(settings.thingspeakWriteAPIKey);

      String address = "http://api.thingspeak.com/update?channel_id=" + urlencode(settings.thingspeakChannelID) + "&api_key=" + urlencode(settings.thingspeakWriteAPIKey) + "&field1=" + urlencode(String(temperature)) + "&field2=" + urlencode(String(humidity));
      if (activeTimerInfo != "")
        address += "&status=" + urlencode(activeTimerInfo);
      else
        address += "&status=" + urlencode("None active Timers.");

      Serial.print("thingspeakHttpLink: ");
      httpGet(address, 5000);
    }
    if (settings.logHttpLink != "") {
      Serial.print("logHttpLink: ");
      Serial.println(settings.logHttpLink);
      String logHttpLink = settings.logHttpLink;
      logHttpLink.replace("%temperature%", urlencode(String(temperature)));
      logHttpLink.replace("%humidity%", urlencode(String(humidity)));
      logHttpLink.replace("%activeTimerInfo%", urlencode(activeTimerInfo));
      Serial.print("logHttpLink with Values: ");
      httpGet(logHttpLink, 5000);
    }
  }
  
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    digitalWrite(RELAIS, relaisState);
    digitalWrite(LED, !digitalRead(LED));
  }
  yieldServer();
}
