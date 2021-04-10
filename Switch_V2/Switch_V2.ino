#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <FS.h>
#include <LittleFS.h>
#include <sys/time.h> // struct timeval
#include <TZ.h>
#define MYTZ TZ_Europe_London
const char compile_date[] = __FILE__ " " __DATE__ " " __TIME__;

ESP8266WebServer server(80);
ESP8266HTTPUpdateServer httpUpdater;
const char* domainName = "terra-switch";  // set domain name domain.local

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

unsigned long previousMillis = 0;
unsigned long interval = 1000;

File UploadFile;

typedef struct
{
  String wifi_ssid;
  String wifi_psk;
  bool invertInternalOutput;
}  settings_entry;
settings_entry settings;

void yieldServer() {
  MDNS.update();
  if (wifiModeAP)
    dnsServer.processNextRequest();
  server.handleClient();
  ArduinoOTA.handle();
  yield();
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

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);

  if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file

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
  String path = "/index.html";
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
      if (settingsCounter == 2) {
        settings.invertInternalOutput = file.readStringUntil('\n').toInt();
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
    Serial.println("Invert Internal Output");
  }else{
    _HIGH = HIGH;
    _LOW = LOW;
    Serial.println("Not invert Internal Output");
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
        if (appendFile("/settings.conf", urldecode(message).c_str())) {
          state = "appendFile OK";
          readSettingsFile();
        } else {
          state = "appendFile FAILD";
        }
      } else {
        if (writeFile("/settings.conf", urldecode(message).c_str())) {
          state = "writeFile OK";
          readSettingsFile();
        } else {
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

void handleSwitch() {
  Serial.println("handleSwitch: ");
  String state = "";
  String message = urldecode(server.arg("channel0"));

  Serial.println(message);
  if (message != "") {
    if (urldecode(message) == "0") {
      relaisState = _LOW;
    }
    if (urldecode(message) == "1") {
      relaisState = _HIGH;
    }
    Serial.print("relaisState: ");
    Serial.println(relaisState, DEC);
    state = String(relaisState);
  }
  else {
    state = "Message empty or decode ERROR";
  }
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

String getAPName() {
  uint8_t mac[WL_MAC_ADDR_LENGTH];
  WiFi.softAPmacAddress(mac);
  String macID = String(mac[WL_MAC_ADDR_LENGTH - 2], HEX) +
                 String(mac[WL_MAC_ADDR_LENGTH - 1], HEX);
  macID.toLowerCase();
  return String(domainName) + String("-") + String(macID);
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

    ArduinoOTA.setHostname(getAPName().c_str());
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
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });
    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
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

    myIP = WiFi.localIP();
    Serial.print("Local IP address: ");
  } else {
    interval = 100;
    Serial.println("AP Mode");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(getAPName());
    //WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    myIP = WiFi.softAPIP();
    // if DNSServer is started with "*" for domain name, it will reply with
    // provided IP to all DNS request
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(DNS_PORT, "*", myIP);
    Serial.print("AP IP address: ");
  }

  Serial.println(myIP);

  if (!MDNS.begin(getAPName(), myIP)) {
    Serial.println("[ERROR] MDNS responder did not setup");
  } else {
    Serial.println("[INFO] MDNS setup is successful!");
    Serial.print("You can reach it via http://");
    Serial.print(getAPName());
    Serial.println(".local");
    MDNS.addService("http", "tcp", 80);
  }

  if (wifiModeAP) {
    server.on("/gen_204", handleRootAP);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
    server.on("/generate_204", handleRootAP);  //Android captive portal. Maybe not needed. Might be handled by notFound handler.
    server.on("/fwlink", handleRootAP);  //Microsoft captive portal. Maybe not needed. Might be handled by notFound handler.
  }

  server.on("/switch.html", handleSwitch);
  server.on("/reboot.html", handleReboot);
  server.on("/version", []() {
    server.send(200, "text/plain", compile_date);
  });
  server.onNotFound([]() {                              // If the client requests any URI
    if (!handleFileRead(server.uri()))                 // send it if it exists
      server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
  });
  server.on("/saveSetting.html", handleSaveSetting);
  server.on("/fupload", HTTP_POST, []() {
    server.send(200);
  }, handleFileUpload);

  httpUpdater.setup(&server);
  server.begin();                           // Actually start the server
  if (!wifiModeAP)
    ArduinoOTA.begin();
  Serial.println("HTTP server started");
  Serial.println();
}

void loop() {
  // put your main code here, to run repeatedly:
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    Serial.print("real relaisState = ");
    Serial.println(relaisState, DEC);
    digitalWrite(LED, !digitalRead(LED));
  }
  digitalWrite(RELAIS, relaisState);
  yieldServer();
}
