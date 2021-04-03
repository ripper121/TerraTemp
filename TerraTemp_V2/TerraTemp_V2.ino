#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <FS.h>
#include <LittleFS.h>

#include <sys/time.h> // struct timeval
#include <TZ.h>
#define MYTZ TZ_Europe_London

ESP8266WebServer server(80);

String getContentType(String filename); // convert the file extension to the MIME type
bool handleFileRead(String path);       // send the right file to the client (if it exists)

char mySSID[64];
char myPassword[64];
String ntpServer = "pool.ntp.org";

#define LED 13
#define RELAIS 12
#define BUTTON 0
#define MAXTIMERS 100

typedef struct
{
  bool activ;
  byte minute[60];
  byte hour[24];
  byte dayOfMonth[31];
  byte month[12];
  byte dayOfWeek[7];
  byte channel;
  byte function;
  bool onOff;
  float temperature;
  float humidity;
}  timer_entry;


void readFile(const char * path) {
  Serial.printf("Reading file: %s\n", path);

  File file = LittleFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  Serial.print("Read from file: ");
  while (file.available()) {
    Serial.write(file.read());
  }
  file.close();
}

void writeFile(const char * path, const char * message) {
  Serial.printf("Writing file: %s\n", path);

  File file = LittleFS.open(path, "w");
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }
  if (file.print(message)) {
    Serial.println("File written");
  } else {
    Serial.println("Write failed");
  }
  delay(2000); // Make sure the CREATE and LASTWRITE times are different
  file.close();
}

void appendFile(const char * path, const char * message) {
  Serial.printf("Appending to file: %s\n", path);

  File file = LittleFS.open(path, "a");
  if (!file) {
    Serial.println("Failed to open file for appending");
    return;
  }
  if (file.print(message)) {
    Serial.println("Message appended");
  } else {
    Serial.println("Append failed");
  }
  file.close();
}

void renameFile(const char * path1, const char * path2) {
  Serial.printf("Renaming file %s to %s\n", path1, path2);
  if (LittleFS.rename(path1, path2)) {
    Serial.println("File renamed");
  } else {
    Serial.println("Rename failed");
  }
}

void deleteFile(const char * path) {
  Serial.printf("Deleting file: %s\n", path);
  if (LittleFS.remove(path)) {
    Serial.println("File deleted");
  } else {
    Serial.println("Delete failed");
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

  Serial.println("Open timer.txt");
  File file = LittleFS.open(timerPath, "r");
  if (!file) {
    Serial.println("Timer.txt Failed to open file for reading");
    return entry;
  }

  int lineCount = 0;
  while (file.available()) {
    timer = file.readStringUntil('\n');
    if (lineCount > timerCount)
      break;
    lineCount++;
    timer = "";
  }
  file.close();

  if (timer == "")
    return entry;

  Serial.print("Timer Entry: ");
  Serial.println(timerCount);
  Serial.print("Timer String: ");
  Serial.println(timer);

  String minute = getValue(timer, ';', 0);
  Serial.print("Minutes String: ");
  Serial.println(minute);
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
  Serial.print("Minutes: ");
  for (int j = 0; j < 60; j++) {
    Serial.print(entry.minute[j], DEC);
    Serial.print(",");
  }
  Serial.println("");


  String hour = getValue(timer, ';', 1);
  Serial.print("Hours String: ");
  Serial.println(hour);
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
  Serial.print("Hours: ");
  for (int j = 0; j < 24; j++) {
    Serial.print(entry.hour[j], DEC);
    Serial.print(",");
  }
  Serial.println("");


  String dayOfMonth = getValue(timer, ';', 2);
  Serial.print("dayOfMonth String: ");
  Serial.println(dayOfMonth);
  if (dayOfMonth == "*") {
    for (int j = 0; j < 31; j++)
      entry.dayOfMonth[j] = 1;
  } else {
    for (int j = 0; j < 31; j++)
      entry.dayOfMonth[j] = 0;
    for (int j = 0; j < 31; j++) {
      if (getValue(dayOfMonth, ',', j) != "")
        entry.dayOfMonth[getValue(dayOfMonth, ',', j).toInt() - 1] = 1;
    }
  }
  Serial.print("dayOfMonth: ");
  for (int j = 0; j < 31; j++) {
    Serial.print(entry.dayOfMonth[j], DEC);
    Serial.print(",");
  }
  Serial.println("");


  String month = getValue(timer, ';', 3);
  Serial.print("month String: ");
  Serial.println(month);
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
  Serial.print("month: ");
  for (int j = 0; j < 12; j++) {
    Serial.print(entry.month[j], DEC);
    Serial.print(",");
  }
  Serial.println("");


  String dayOfWeek = getValue(timer, ';', 4);
  Serial.print("dayOfWeek String: ");
  Serial.println(dayOfWeek);
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
  Serial.print("dayOfWeek: ");
  for (int j = 0; j < 7; j++) {
    Serial.print(entry.dayOfWeek[j], DEC);
    Serial.print(",");
  }
  Serial.println("");

  Serial.print("channel: ");
  entry.channel = getValue(timer, ';', 5).toInt();
  Serial.print(entry.channel, DEC);
  Serial.println("");

  Serial.print("function: ");
  entry.function = getValue(timer, ';', 6).toInt();
  Serial.print(entry.function, DEC);
  Serial.println("");
  if (entry.function == 0) {
    Serial.print("onOff: ");
    entry.onOff = getValue(timer, ';', 7).toInt();
    Serial.print(entry.onOff, DEC);
  }
  if (entry.function == 1) {
    Serial.print("temperature: ");
    entry.temperature = getValue(timer, ';', 7).toFloat();
    Serial.print(entry.temperature);
  }
  if (entry.function == 2) {
    Serial.print("humidity: ");
    entry.humidity = getValue(timer, ';', 7).toFloat();
    Serial.print(entry.humidity);
  }
  Serial.println();
  Serial.println();

  entry.activ = true;
  return entry;
}

bool handleFileRead(String path) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  String contentType = getContentType(path);            // Get the MIME type
  if (LittleFS.exists(path)) {                            // If the file exists
    File file = LittleFS.open(path, "r");                 // Open it
    size_t sent = server.streamFile(file, contentType); // And send it to the client
    file.close();                                       // Then close the file again
    return true;
  }
  Serial.println("\tFile Not Found");
  return false;                                         // If the file doesn't exist, return false
}

String getContentType(String filename) { // convert the file extension to the MIME type
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}

void setup() {
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

  /*
    //minute;hour;dayOfMonth;month;dayOfWeek;channel;function;value
    String timerPath = "/timer.txt";
    writeFile(timerPath.c_str(), "1,2,3,57,58,59;1,2,3,21,22,23;1,2,3,29,30,31;1,2,3,10,11,12;1,2,3,5,6,7;1;2;53.1\n");
    for (int i = 1; i < 100; i++) {
      appendFile(timerPath.c_str(), "1,2,3,57,58,59;1,2,3,21,22,23;1,2,3,29,30,31;1,2,3,10,11,12;1,2,3,5,6,7;1;2;53.1\n");

    }
   
  */
  
 writeFile("/wifiSettings.txt", "FRITZ!Box 6490 Cable\n70632825448371492035\n");
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

  timer_entry entry;
  for (int i = 0; i < 100; i++) {
    entry = readTimerFromFile(i);
    if (!entry.activ) {
      Serial.println("END");
      break;
    }
  }

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

  server.onNotFound([]() {                              // If the client requests any URI
    if (!handleFileRead(server.uri()))                  // send it if it exists
      server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
  });

  server.begin();                           // Actually start the server
  Serial.println("HTTP server started");
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

void loop() {
  // put your main code here, to run repeatedly:
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;

    time_t now;
    struct tm * timeinfo;
    time(&now);
    timeinfo = localtime(&now);

    Serial.print(int(timeinfo->tm_hour));
    Serial.print(":");
    Serial.print(int(timeinfo->tm_min));
    Serial.print(":");
    Serial.println(int(timeinfo->tm_sec));
  }
  server.handleClient();
  MDNS.update();
}
