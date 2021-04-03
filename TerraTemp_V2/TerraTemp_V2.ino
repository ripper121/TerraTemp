#include <ESP8266WiFi.h>
#include <FS.h>
#include <LittleFS.h>

#include <sys/time.h> // struct timeval
#include <TZ.h>
#define MYTZ TZ_Europe_London

char mySSID[64];
char myPassword[64];
String ntpServer = "pool.ntp.org";

#define LED 13
#define RELAIS 12
#define BUTTON 0

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
}  entry;

entry entrys[100];

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

void setup() {
  Serial.begin(115200);

  //Serial.println("Formatting LittleFS filesystem");
  //LittleFS.format();

  Serial.println("Mount LittleFS");
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed");
    return;
  }

  //writeFile("/wifiSettings.txt", "FRITZ!Box 6490 Cable\n70632825448371492035\n");
  //minute;hour;dayOfMonth;month;dayOfWeek;channel;function;value
  //writeFile("/timer.txt", "*;8,9,10;*;1,2,3,4,5,6;*;0;1;15\n");

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


  //read timer from FS
  file = LittleFS.open("/timer.txt", "r");
  if (!file) {
    Serial.println("Timer.txt Failed to open file for reading");
    return;
  }
  byte timerCount = 0;
  String timer[100];
  while (file.available()) {
    timer[timerCount] = file.readStringUntil('\n');
    timerCount++;
  }
  file.close();

  //write timer into Struct
  for (byte i = 0; i < timerCount; i++) {
    Serial.println(timer[i]);
    entrys[i].activ = true;
    if (getValue(timer[i], ';', 0) == "*") {
      for (int j = 0; j < 59; j++)
        entrys[i].minute[j] = 1;
    }
    if (getValue(timer[i], ';', 1) == "*") {
      for (int j = 0; j < 23; j++)
        entrys[i].hour[j] = 1;
    }
    if (getValue(timer[i], ';', 2) == "*") {
      for (int j = 1; j < 31; j++)
        entrys[i].dayOfMonth[j] = 1;
    }
    if (getValue(timer[i], ';', 3) == "*") {
      for (int j = 1; j < 12; j++)
        entrys[i].month[j] = 1;
    }
    if (getValue(timer[i], ';', 4) == "*") {
      for (int j = 1; j < 7; j++)
        entrys[i].dayOfWeek[j] = 1;
    }
    entrys[i].channel = getValue(timer[i], ';', 5).toInt();
    entrys[i].function = getValue(timer[i], ';', 6).toInt();
    if (entrys[i].function == 0)
      entrys[i].onOff = getValue(timer[i], ';', 7).toInt();
    if (entrys[i].function == 1)
      entrys[i].temperature = getValue(timer[i], ';', 7).toFloat();
    if (entrys[i].function == 2)
      entrys[i].humidity = getValue(timer[i], ';', 7).toFloat();
  }

  for (byte i = 0; i < timerCount; i++) {
    Serial.print(entrys[i].function);
    Serial.println(entrys[i].temperature);
  }

  Serial.println();
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
}
