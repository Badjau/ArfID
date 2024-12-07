#include <Arduino.h>
#include <SPI.h>
#include <SPIFFS.h>   //Webserver and database
#include <MFRC522.h>  //RFID reader module
#include <WiFi.h>
#include <time.h>               //time tracker
#include <TimeLib.h>            //Time handler
#include <ArduinoJson.h>        //JSON format? Do not know other libraries for JSON
#include <FS.h>
#include <HTTPClient.h>  //For web push notifications
#include <WifiClientSecure.h>
#include <HX711.h>
#include <ESP32Servo.h>
#include <AsyncTCP.h>           //Webserver library
#include <ESPAsyncWebServer.h>  //Webserver library
#include <PID_v1.h> //  PID controller for feeding algorithm

void initServos();
void connectToWiFi();   
void initRFID();
void initSPIFFS();
void initScales();
void webServer();
void connectToWifi();
void printFileContents(const char *filename);
void readPetData();
int matchTagUID(String compareTag);
void sendNotification(String title, String message);
String printLocalTime();
String readJSONFile(const char *path);
bool canFeedingOccur(const char* tagUID);
void checkLoginJSON();
void recordFeedingSchedule(const char *tagUID, int dispensedFood);
void updateFeedingSchedule(const char *tagUID, int consumedFood);
int calculateDispenseAmount(const char *tagUID, int foodValues);
void startMonitoringConsumption(String tagUID, float dispensedAmount);
void monitorConsumption(String tagUID, float dispensedAmount);
void dispenseFood(int targetWeight);
void disposeFood();
time_t convertToUnixTimestamp(const String& dateTimeStr);

//Servo Pins
#define SERVO_1_PIN 26           // Define the pin for servo1
#define SERVO_BOWL_PIN 4        // Define the pin for servo1
Servo servoBowl;                    //bowl
Servo servo1;                    //servo1
Servo servo2;
#define RELAY_DC_MOTOR 0 //relay to vibrating dc motor
#define RELAY_SERVO_MOTOR1 2 //relay to Servo Motor

// HX711 Pins
#define LOADCELL_BOWL_DOUT_PIN 32
#define LOADCELL_BOWL_SCK_PIN 33
#define LOADCELL_1_DOUT_PIN 27
#define LOADCELL_1_SCK_PIN 14
#define LOADCELL_2_DOUT_PIN 15 // hx711 dead?
#define LOADCELL_2_SCK_PIN 2

HX711 scaleBowl;
HX711 scale1;                                   //storage 1 scale (right)
HX711 scale2;                                   //storage 2 scale (left) 
String weightString1 = "";                       //scale1 string holder
String weightString2 = "";                       //scale2 string holder
int currentWeight = 0;                          //holder for dispenseFood()
unsigned long previousMillis = 0;               // Variable to store the last time get_units was called
const unsigned long MAX_MILLIS = 4294967295UL;  // Maximum value for millis()
int foodToDispense = 0; //holder

//monitorConsumption variables
unsigned long startTimeMonitorConsumption = 0;
float initialWeightMonitorConsumption = 0;
bool isMonitoringConsumption = false;
bool isStablePeriod = false;

#define RFID_SS_PIN 5                        //RFID SS
#define RFID_RST_PIN 17                      //RFID RST
MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);  // Create MFRC522 instance.
String tagUID = "";                          //scanned tag string holder
String newTag = "";                          //scanned tag registered/unregistered bool
const int MAX_TAGUIDS = 2;                   //readPetData scan count
char registeredTagUIDs[MAX_TAGUIDS][9];      //Array to store tagUIDs (assuming max length of 8 characters + null terminator)
char registeredNames[MAX_TAGUIDS][20];       //Array to store names
int tagCount  = 0;  // Variable to keep track of the number of stored tags
int pet1food1 = 0;
int pet2food1 = 0;

// Template function declaration
template<typename T>
String getArrayString(T array[], int size);

bool isLoggedIn = false;
bool isRegistered = false;
String username= "";

File root;              //placeholder for "/"

const char *ssid[] = { "ESP32", "hehe" };
const char *password[] = { "12345678", "chimaera flatfish specter" };
const int numNetworks = 2;

const char *apSSID = "ESP32";
const char *apPassword = "12345678";

AsyncWebServer server(80);  // Declare server globally

const char *ntpServer = "time.google.com";  //changed from pool.ntp.org, as it does not work
const long gmtOffset_sec = 28800;           //GMT+8 Offset
const int daylightOffset_sec = 0;           //0 for no daylight savings

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
  pinMode(RELAY_DC_MOTOR, OUTPUT);
  pinMode(RELAY_SERVO_MOTOR1, OUTPUT);
  pinMode(RELAY_DC_MOTOR, OUTPUT);
  pinMode(RELAY_SERVO_MOTOR1, OUTPUT);
  digitalWrite(RELAY_DC_MOTOR, LOW);  // Turn relay ON
  digitalWrite(RELAY_SERVO_MOTOR1, LOW);  // Turn relay ON
  Serial.begin(115200);
  //settings();
  initServos();                                              //Init of servo1
  connectToWiFi();                                           //connect to WiFi with defaults in order for printLocalTime to work
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);  //init and get the time
  printLocalTime();
  //startAPMode();
  initSPIFFS();
  webServer();

  initScales();
  initRFID();
  readPetData();
  getArrayString(registeredNames, tagCount);
  for (int i = 0; i < MAX_TAGUIDS; i++) {
    char path[23];
    snprintf(path, sizeof(path), "/sched-%s.json", registeredTagUIDs[i]);
    printFileContents(path);
  }
}

void loop() {
  initRFID();

  // Check for new RFID tags
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
    digitalWrite(LED_BUILTIN, HIGH);  
    tagUID = "";
    newTag = "";

    // Get UID
    for (byte i = 0; i < mfrc522.uid.size; i++) {
      tagUID += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
      tagUID += String(mfrc522.uid.uidByte[i], HEX);
    }
    Serial.println(tagUID);

    int matchIndex = matchTagUID(tagUID);

    if (matchIndex != -1) {
      newTag = "Tag is already registered.";
      Serial.print("loop(): mfrc522: newTag: ");
      Serial.println(newTag);

      if (matchIndex = 0) {
        foodToDispense = pet1food1;
      } 
      if (matchIndex = 1) {
        foodToDispense = pet2food1;
      } 

      //currentWeight = scale1.get_units();
      currentWeight = scale1.get_units();;
      Serial.printf("loop(): Current weight: %.2f\n", currentWeight);

      bool toFeed = canFeedingOccur(tagUID.c_str());
      Serial.println(toFeed);

      if (toFeed) {
        if (currentWeight >= foodToDispense) {
          recordFeedingSchedule(tagUID.c_str(), foodToDispense);
          dispenseFood(foodToDispense);

          // Start monitoring food consumption
          startMonitoringConsumption(tagUID.c_str(), foodToDispense);

          Serial.printf("loop(): Dispensed Food: %d\n", foodToDispense);
          sendNotification(String("Pet: ") + registeredNames[matchIndex] + String(" successfuly fed!"), String("Dispensed Food: ") + foodToDispense + String("g."));
        } else {
          Serial.println("loop(): Not enough food.");
          sendNotification(String("Pet: ") + registeredNames[matchIndex] + String(" attempted to feed."), String("Unable to dispense ") + foodToDispense + String("g. | Remaining: ") + currentWeight);
        }
      } else {
        sendNotification(String("Pet: ") + registeredNames[matchIndex] + String(" attempted to feed."), String("Unable to dispense. Must feed at a later time."));
      }
    } else {
      newTag = "Tag UID:";
      Serial.print("loop(): mfrc522: newTag: ");
      Serial.println(newTag);
    }

    // Halt RFID card and stop encryption
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    digitalWrite(LED_BUILTIN, LOW);
  }

  // Continuously monitor the consumption status
  if (isMonitoringConsumption) {
   monitorConsumption(tagUID.c_str(), foodToDispense);
  }
}


void webServer() {
  // Route for root / web page
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    initRFID();
    readPetData();
    Serial.printf("\nStack:%d,Heap:%lu\n", uxTaskGetStackHighWaterMark(NULL), (unsigned long)ESP.getFreeHeap());
    checkLoginJSON();
    Serial.printf("isRegistered: %d\n", isRegistered);
    if (isRegistered) {
      // Send the main page
      request->send(SPIFFS, "/index.html", "text/html");
    } else {
      request->redirect("/login");
    }
  });

  // Route to handle the request for registeredTagUIDs
  server.on("/registered-tags", HTTP_GET, [](AsyncWebServerRequest *request) {
    String registeredNamesString = getArrayString(registeredNames, tagCount );
    request->send(200, "text/plain", registeredNamesString);
  });

  // Route to handle feeding data 1 from sched-tagUID1.json
  server.on("/feeding-data1", HTTP_GET, [](AsyncWebServerRequest *request) {
    char path[23];
    snprintf(path, sizeof(path), "/sched-%s.json", registeredTagUIDs[0]);
    String schedules1JSON = readJSONFile(path);
    Serial.print("/feeding-data1(): registeredTagUIDs[0]: ");
    Serial.println(registeredTagUIDs[0]);
    printFileContents(path);
    request->send(200, "application/json", schedules1JSON);
  });

  // Route to handle feeding data 2 from sched-tagUID2.json
  server.on("/feeding-data2", HTTP_GET, [](AsyncWebServerRequest *request) {
    char path[23];
    snprintf(path, sizeof(path), "/sched-%s.json", registeredTagUIDs[1]);
    String schedules2JSON = readJSONFile(path);
    Serial.print("/feeding-data1(): registeredTagUIDs[1]: ");
    Serial.println(registeredTagUIDs[1]);
    printFileContents(path);
    request->send(200, "application/json", schedules2JSON);
  });

  // Route to handle feeding data 2 from sched-tagUID2.json
  server.on("/pet-data1", HTTP_GET, [](AsyncWebServerRequest *request) {
    char path[23];
    snprintf(path, sizeof(path), "/sched-%s.json", registeredTagUIDs[0]);
    String data1JSON = readJSONFile(path);
    Serial.print("/pet-data1(): registeredTagUIDs[0]: ");
    Serial.println(registeredTagUIDs[0]);
    printFileContents(path);
    request->send(200, "application/json", data1JSON);
  });

  // Route to handle feeding data 2 from sched-tagUID2.json
  server.on("/pet-data2", HTTP_GET, [](AsyncWebServerRequest *request) {
    char path[23];
    snprintf(path, sizeof(path), "/sched-%s.json", registeredTagUIDs[1]);
    String data2JSON = readJSONFile(path);
    Serial.print("/pet-data2(): registeredTagUIDs[1]: ");
    Serial.println(registeredTagUIDs[1]);
    printFileContents(path);
    request->send(200, "application/json", data2JSON);
  });

  // Route to load style.css file
  server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/style.css", "text/css");
  });

  // Route to load favicon.png file
  server.on("/favicon.png", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/favicon.png", "image/png");
  });

  server.on("/LOGO-bordered.png", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/LOGO-bordered.png", "image/png");
  });

  server.on("/bcs.jpg", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/bcs.jpg", "image/jpg");
  });

  server.on("/gear.png", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/gear.png", "image/png");
  });

  server.on("/dog-face.png", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/dog-face.png", "image/png");
  });

  // Route to handle AJAX request for tag UID
  server.on("/tag", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", tagUID);
  });

  // Route to handle AJAX request for tag UID and newTag
  server.on("/new-tag", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", newTag);
  });  
  
  // Route to handle the request for registeredTagUIDs
  server.on("/time", HTTP_GET, [](AsyncWebServerRequest *request) {
    String timeNow = printLocalTime();
    request->send(200, "text/plain", timeNow);
  });

  // Route to handle AJAX request for scale1
  server.on("/scale1", HTTP_GET, [](AsyncWebServerRequest *request) {
    float weightReading = scale1.get_units(10);  //scale1
    weightString1 = String(weightReading, 2);
    Serial.print("/scaleBowl: ");
    Serial.println(scaleBowl.get_units(), 1);
    Serial.print("/scale1: ");
    Serial.println(weightString1);    
    request->send(200, "text/plain", weightString1);
  });

  // Route to handle AJAX request for scale1
  server.on("/scale2", HTTP_GET, [](AsyncWebServerRequest *request) {
    float weightReading = scale2.get_units(10);  //scale2
    weightString2 = String(weightReading, 2);
    Serial.print("/scale2: ");
    Serial.println(weightString2);    
    request->send(200, "text/plain", weightString2);
  });

  // Route to handle "/register-tag"
  server.on("/register-tag", HTTP_GET, [](AsyncWebServerRequest *request) {
    tagUID = "";
    newTag = "";
    request->send(SPIFFS, "/register-tag.html", "text/html");
  });

  // Route to handle "/register-pet" button when a tag is found
  server.on("/register-pet", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Get the tag UID from the URL query parameter
    String tagUID = request->arg("tagUID");
    // Proceed to register page if tagUID is not "No tag found"
    if (tagUID != "") {
      // Redirect to the register page with tagUID as a query parameter
      request->send(SPIFFS, "/register-pet.html", "text/html");
    } else {
      // Send a response indicating that tag UID is not found
      request->send(400, "text/plain", "Tag UID not found");
    }
  });

  server.on("/submit", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Retrieve form data
    String tagUID = request->getParam("tagUID", true)->value();
    String name = request->getParam("name", true)->value();
    String food1 = request->getParam("food1", true)->value();
    String food2 = request->hasParam("food2", true) ? request->getParam("food2", true)->value() : "0";
    String calories = request->getParam("calories", true)->value();

    // Create a JSON document
    const size_t JSON_BUFFER_SIZE = 512; // Adjust based on your needs
    StaticJsonDocument<JSON_BUFFER_SIZE> doc;


    if (calories.toInt() > 0) {
      food1 = food1.toInt()/2;
      food2 = food2.toInt()/2;
    }
    
    // Create the petInfo object
    JsonObject petInfo = doc.createNestedObject("petInfo");
    petInfo["tagUID"] = tagUID;
    petInfo["name"] = name;
    petInfo["food1"] = food1.toInt();
    petInfo["food2"] = food2.toInt();
    petInfo["lastUsedFood1"] = true;  // Default to true as per the ideal format

    // Create an empty feedingHistory array
    doc.createNestedArray("feedingHistory");

    if (tagCount == 0) {
      pet1food1 = food1.toInt();
    } else if (tagCount == 1) {
      pet2food1 = food1.toInt();
    }
    // Generate file path
    String filePath = "/sched-" + tagUID + ".json";

    // Save JSON to file
    File file = SPIFFS.open(filePath, FILE_WRITE);
    if (!file) {
      request->send(500, "text/plain", "Internal Server Error");
      return;
    }

    serializeJson(doc, file);
    file.close();

    // Send response
    request->send(200, "application/json", "{\"status\": \"success\"}");
  });

  // Route to handle settings button
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/settings.html", "text/html");
  });

  // ADMIN Route to delete files ex: /delete-data?delete=data-0300682f.json
  server.on("/delete-data", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("delete")) {
      String fileName = "/" + request->getParam("delete")->value();
      if (SPIFFS.remove(fileName)) {
        String redirectPage = "<html><head><meta http-equiv='refresh' content='3;url=/'></head><body>File '" + fileName + "' deleted. You will be redirected in 3 seconds...</body></html>";
        request->send(200, "text/html", redirectPage);
      } else {
        request->send(400, "text/plain", "'" + fileName + "' not found or cannot be deleted");
      }
    } else {
      request->send(400, "text/plain", "No file specified");
    }
  });

  // ADMIN Route to delete all files starting with "sched-"
  server.on("/delete-sched-files", HTTP_GET, [](AsyncWebServerRequest *request) {
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    bool filesDeleted = false;
    String filesList = "";

    while (file) {
      String fileName = file.name();
      if (fileName.startsWith("sched-")) {
        if (SPIFFS.remove(fileName)) {
          filesDeleted = true;
          filesList += fileName + " deleted.<br>";
        }
      }
      file = root.openNextFile();
    }

    if (filesDeleted) {
      String redirectPage = "<html><head><meta http-equiv='refresh' content='3;url=/'></head><body>" + filesList + "You will be redirected in 3 seconds...</body></html>";
      request->send(200, "text/html", redirectPage);
    } else {
      request->send(200, "text/plain", "No files with 'sched-' prefix found or could not be deleted");
    }
  });


  // ADMIN Route to list files
  server.on("/list-files", HTTP_GET, [](AsyncWebServerRequest *request) {
    initSPIFFS();
    String redirectPage = "<html><head><meta http-equiv='refresh' content='3;url=/'></head><body>You will be redirected in 3 seconds...</body></html>";
    request->send(200, "text/html", redirectPage);
  });

  // ADMIN Route to print file contents ex: /print-contents?file=data-0300682f.json
  server.on("/print-contents", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("file")) {
      String filename = request->getParam("file")->value();
      size_t pathLength = filename.length() + 1; // Account for '/'
      char path[pathLength];
      strcpy(path, "/");
      strcat(path, filename.c_str());

      printFileContents(path);
      String redirectPage = "<html><head><meta http-equiv='refresh' content='3;url=/'></head><body>You will be redirected in 3 seconds...</body></html>";
      request->send(200, "text/html", redirectPage);
    } else {
      request->send(400, "text/plain", "No file specified");
    }
  });

  // ADMIN Route to simulate and saving feeding data
  server.on("/simulate-feeding", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("input")) {
      String fileName = request->getParam("input")->value();
      int index = fileName.toInt();  // Convert string to integer index
      if (index >= 0 && index < MAX_TAGUIDS) {
        char path[23];  // Adjust the size according to your needs
        snprintf(path, sizeof(path), "%s", registeredTagUIDs[index]);
        int randAmount = random(80, 200);
        recordFeedingSchedule(path, randAmount);
        String response = "Simulated feeding: " + String(path) + " " + String(randAmount);
        request->send(200, "text/html", response);
      } else {
        request->send(400);  // Bad request if index is out of range
      }
    }
  });

    // ADMIN Route to simulate and saving feeding data
  server.on("/simulate-update-feeding", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->hasParam("input")) {
      String fileName = request->getParam("input")->value();
      int index = fileName.toInt();  // Convert string to integer index
      if (index >= 0 && index < MAX_TAGUIDS) {
        char path[23];  // Adjust the size according to your needs
        snprintf(path, sizeof(path), "%s", registeredTagUIDs[index]);
        int randAmount = random(80, 200);
        updateFeedingSchedule(path, randAmount);
        String response = "Simulated feeding: " + String(path) + " " + String(randAmount);
        request->send(200, "text/html", response);
      } else {
        request->send(400);  // Bad request if index is out of range
      }
    }
  });

  // Start server
  server.begin();
}
// PID Tuning Parameters
double Kp = 2, Ki = 5, Kd = 1;
double Setpoint, Input, Output;

// Servo control parameters
const int SERVO_CLOSED = 180;  // Fully closed position
const int SERVO_OPEN = 155;      // Fully open position
const int MAX_OPEN_ANGLE = 155; // Maximum opening angle (adjust as needed)

// Dampening factor (0.0 to 1.0, higher values mean more dampening)
const float DAMPENING = 0.3;   // Reduced dampening for more responsive movement

// Create PID instance
PID myPID(&Input, &Output, &Setpoint, Kp, Ki, Kd, DIRECT);

void dispenseFood(int targetWeight) {
  int startingWeight = scale1.get_units();
  if (startingWeight >= targetWeight) {
    digitalWrite(RELAY_DC_MOTOR, HIGH);  // Turn relay ON
    digitalWrite(RELAY_SERVO_MOTOR1, HIGH);  // Turn relay ON
    int currentWeight = startingWeight;
    int currentServoAngle = SERVO_OPEN;
    int targetServoAngle = SERVO_OPEN;
    
    Setpoint = startingWeight - targetWeight;
    Input = currentWeight;
    myPID.SetMode(AUTOMATIC);
    myPID.SetOutputLimits(0, MAX_OPEN_ANGLE);
    
    while (currentWeight > Setpoint) {
      currentWeight = scale1.get_units();
      
      // Check if target weight is reached
      if (currentWeight <= Setpoint) {
          targetServoAngle = SERVO_CLOSED;
          Serial.println("Target weight reached. Closing servo.");
          break;
      }
      
      Input = currentWeight;
      myPID.Compute();
      
      // Calculate target angle based on PID output
      targetServoAngle = SERVO_OPEN + (int)Output;
      
      // Apply dampening to smooth movement
      currentServoAngle = currentServoAngle + (targetServoAngle - currentServoAngle) * (1 - DAMPENING);
      
      // Ensure we're within bounds
      currentServoAngle = constrain(currentServoAngle, SERVO_OPEN, SERVO_CLOSED);
      
      // Move servo to calculated angle
      servo1.write(currentServoAngle);
      
      // Print debugging info
      Serial.print("Servo angle: ");
      Serial.print(currentServoAngle);
      Serial.print(" degrees | Weight: ");
      Serial.print(currentWeight);
      Serial.println(" g");
    }
    
    // Ensure servo is fully closed at the end
    servo1.write(SERVO_CLOSED);
    
    Serial.print("Final Weight: ");
    Serial.print(currentWeight);
    Serial.println(" g");
  } else {
    Serial.print("Not enough food. Weight: ");
    Serial.println(startingWeight);
  }
}

template<typename T>
String getArrayString(T array[], int size) {
  String arrayString = "";
  if (size > 0) {
    for (int i = 0; i < size; i++) {
      arrayString += array[i];
      if (i < size - 1) {
        arrayString += ",";
      }
    }
  }
  Serial.print("getArrayString():");
  Serial.println(arrayString);
  return arrayString;
}

int matchTagUID(String compareTag) {
  for (int i = 0; i < tagCount ; i++) {
    // Convert registeredTagUIDs[i] to a String object for comparison
    String tagUIDString = String(registeredTagUIDs[i]);
    if (tagUIDString == compareTag) {
      // Return the index if a match is found
      return i;
    }
  }
  // Return -1 if no match is found
  return -1;
}

void recordFeedingSchedule(const char *tagUID, int dispensedFood) {
  // Generate file path
  String filePath = "/sched-" + String(tagUID) + ".json";

  // Open the file in read mode
  File file = SPIFFS.open(filePath, FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  // Load existing JSON from file
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println("Failed to parse JSON file");
    return;
  }

  // Access the feedingHistory array
  JsonArray feedingHistory = doc["feedingHistory"];

  // Create a new feeding entry
  JsonObject newFeeding = feedingHistory.createNestedObject();
  newFeeding["timestamp"] = printLocalTime();
  newFeeding["dispensedAmount"] = dispensedFood;
  newFeeding["consumedAmount"] = 0;    // Initially set to 0

  // Open the file in write mode to save updated data
  file = SPIFFS.open(filePath, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  // Save the updated JSON document to file
  serializeJson(doc, file);
  file.close();

  Serial.println("Feeding schedule recorded successfully");
}

void updateFeedingSchedule(const char *tagUID, int consumedFood) {
  // Generate file path
  String filePath = "/sched-" + String(tagUID) + ".json";

  // Open the file in read mode
  File file = SPIFFS.open(filePath, FILE_READ);
  if (!file) {
    Serial.println("Failed to open file for reading");
    return;
  }

  // Load existing JSON from file
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.println("Failed to parse JSON file");
    return;
  }

  // Access the feedingHistory array
  JsonArray feedingHistory = doc["feedingHistory"];

  // Check if feedingHistory is not empty
  if (feedingHistory.size() > 0) {
    // Get the last entry in the array
    JsonObject lastFeeding = feedingHistory[feedingHistory.size() - 1];

    // Update the consumedAmount
    lastFeeding["consumedAmount"] = consumedFood;
  } else {
    Serial.println("No feeding history available to update.");
    return;
  }

  // Open the file in write mode to save the updated data
  file = SPIFFS.open(filePath, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to open file for writing");
    return;
  }

  // Save the updated JSON document to file
  serializeJson(doc, file);
  file.close();

  Serial.println("Feeding schedule updated successfully");
}

// Function to read and parse a JSON file as string
String readJSONFile(const char *path) {

  File file = SPIFFS.open(path, "r");
  if (!file) {
    Serial.println("Failed to open file for reading");
    return "";
  }

  String content = file.readString();
  file.close();
  return content;
}


//Function to print file for debugging purposes
void printFileContents(const char *filename) {
  File file = SPIFFS.open(filename, "r");
  if (SPIFFS.exists(filename) && file) {  // Check if the file object is valid
    Serial.println("printFileContents(): '" + String(filename) + "': ");
    while (file.available()) {
      Serial.write(file.read());
    }
    Serial.println();
    file.close();
  } else {
    Serial.println("printFileContents(): Failed to open file: " + String(filename));
  }
}

//!!Change to setup-like page with wifi settings, etc.
void checkLoginJSON() {

  File file = SPIFFS.open("/login.json", "r");
  if (!file) {
    Serial.println("Failed to open login.json file");
    return;
  }

  StaticJsonDocument<256> doc;  // Adjust the size as per your JSON data
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.print("Failed to parse JSON: ");
    Serial.println(error.c_str());
    return;
  }

  // Check if "username" or "password" is empty
  if (doc["username"].as<String>().isEmpty() || doc["password"].as<String>().isEmpty()) {
    isRegistered = false;
    isLoggedIn = false;
    Serial.println("checkLoginJSON(): Username or password is empty. Login failed.");
  } else {
    username = doc["username"].as<String>();//Correct way of setting var username from JSON??
    isRegistered = true;
    Serial.println("checkLoginJSON(): Login successful.");
  }
}

//!!NEVER TRUE
// Function to check if feeding can occur based on the schedules
bool canFeedingOccur(const char* tagUID) {
  char filePath[32];
  snprintf(filePath, sizeof(filePath), "/sched-%s.json", tagUID);
  
  File file = SPIFFS.open(filePath, "r");
  if (!file) {
    Serial.println(F("Failed to open file for reading"));
    return false;
  }

  StaticJsonDocument<512> doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();

  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return false;
  }

  JsonArray feedingHistory = doc["feedingHistory"];
  if (feedingHistory.size() == 0) {
    Serial.println(F("Feeding history is empty"));
    return true;
  }

  time_t currentTime = time(nullptr);
  time_t lastFeedingTime = convertToUnixTimestamp(feedingHistory[feedingHistory.size() - 1]["timestamp"].as<const char*>());

  if (currentTime - lastFeedingTime < 10 * 3600) {
    Serial.println(F("Last feeding was less than 10 hours ago"));
    return false;
  }

  int feedingsToday = 0;
  time_t startOfDay = currentTime - (currentTime % 86400);

  for (JsonObject feeding : feedingHistory) {
    time_t feedingTime = convertToUnixTimestamp(feeding["timestamp"].as<const char*>());
    if (feedingTime >= startOfDay) {
      feedingsToday++;
    }
    yield(); // Allow other tasks to run
  }

  if (feedingsToday >= 2) {
    Serial.println(F("More than 1 feeding today"));
    return false;
  }

  Serial.println(F("Feeding conditions met"));
  return true;
}

// Helper function to convert ISO 8601 datetime string to Unix timestamp
time_t convertToUnixTimestamp(const String& dateTimeStr) {
  tmElements_t tm;
  int year, month, day, hour, minute, second;
  sscanf(dateTimeStr.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second);
  tm.Year = year - 1970;
  tm.Month = month;
  tm.Day = day;
  tm.Hour = hour;
  tm.Minute = minute;
  tm.Second = second;
  return makeTime(tm);
}

String printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return "";
  }
  
  char timeStringBuff[50]; // Create a buffer to hold the formatted time string
  strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%dT%H:%M:%S", &timeinfo); // Format time into ISO 8601

  return String(timeStringBuff); // Convert char array to String and return
}


int calculateDispenseAmount(const char *tagUID, int foodValues) {
  // File name for feeding data, e.g., "/sched-93df62ee.json"
  char fileName[23];
  snprintf(fileName, sizeof(fileName), "/sched-%s.json", tagUID);

  Serial.printf("calculateDispenseAmount: FileName: %s\n", fileName);

  // Open the file for reading or if the file is empty
  File file = SPIFFS.open(fileName, "r");
  if (!file || file.size() == 0) {
    Serial.printf("calculateDispenseAmount: Failed to open file %s. Returning foodValues: %d\n", fileName, foodValues);
    return (foodValues / 2);  // If the file does not exist, return the foodValues
  }

  // Allocate a buffer to store the file contents
  size_t size = file.size();
  std::unique_ptr<char[]> buf(new char[size + 1]);

  // Read the file into the buffer
  size_t bytesRead = file.readBytes(buf.get(), size);
  if (bytesRead != size) {
    Serial.printf("calculateDispenseAmount: Failed to read file %s\n", fileName);
    file.close();
    return (foodValues / 2);  // If reading fails, return the foodValues
  }
  buf[bytesRead] = '\0';  // Null-terminate the buffer

  // Close the file
  file.close();

  // Parse the JSON data
  const size_t JSON_BUFFER_SIZE = 512; // Adjust based on your needs
  StaticJsonDocument<JSON_BUFFER_SIZE> doc;  // Adjust the capacity as needed
  DeserializationError error = deserializeJson(doc, buf.get());
  if (error) {
    Serial.printf("calculateDispenseAmount: JSON parsing error: %s\n", error.c_str());
    return (foodValues / 2);  // If parsing fails, return the foodValues
  }

  JsonArray schedules = doc["schedules"];
  unsigned int scheduleCount = schedules.size();

  // Get the last schedule
  JsonObject lastSchedule = schedules[scheduleCount - 1];
  int consumedFood = lastSchedule["consumedFood"];
  int rationedFood = (foodValues / 2); 
  int returnValue = (rationedFood - consumedFood) + (rationedFood);
  Serial.printf("calculateDispenseAmount: foodValues = %d, consumedFood = %d, returnValue = %d\n", foodValues, consumedFood, returnValue);

  return returnValue;
}

void startMonitoringConsumption(String tagUID, float dispensedAmount) {
  initialWeightMonitorConsumption = scaleBowl.get_units();
  startTimeMonitorConsumption = millis();
  isMonitoringConsumption = true;
  isStablePeriod = false;
}

void monitorConsumption(String tagUID, float dispensedAmount) {
  const float margin = 5; // Adjust this value as needed
  const float zeroThreshold = 1; // Threshold for considering a reading as near zero
  const unsigned long resetDuration = 600000; // 10 minutes timeout
  const unsigned long stableDuration = 30000; // 30 seconds stable weight duration
  unsigned long currentMillis = millis();
  
  if (isMonitoringConsumption) {
    float weightReading = scaleBowl.get_units();

    // Check if the weight has been stable for the stable duration
    if (!isStablePeriod && currentMillis - startTimeMonitorConsumption >= stableDuration) {
      Serial.printf("monitorConsumption(): weight has been stable for %lu milliseconds\n", stableDuration);
      isStablePeriod = true; // Mark the stable period
    }

    // Check if the total resetDuration has passed or stable weight period has been achieved
    if (currentMillis - startTimeMonitorConsumption >= resetDuration || isStablePeriod) {
      float finalWeight = scaleBowl.get_units();
      float consumedFood = ((dispensedAmount + initialWeightMonitorConsumption) / 2) - finalWeight;

      updateFeedingSchedule(tagUID.c_str(), consumedFood);
      disposeFood();

      // Stop monitoring
      isMonitoringConsumption = false;
    }
  }
}

void disposeFood() {
  Serial.println("Disposing food");
  delay(5000);
  servoBowl.write(140);
  delay(3000);
  servoBowl.write(0);
}

void readPetData() {
  tagCount = 0;

  // Initialize arrays
  for (int i = 0; i < MAX_TAGUIDS; i++) {
    registeredTagUIDs[i][0] = '\0';
    registeredNames[i][0] = '\0';
  }

  File root = SPIFFS.open("/");
  if (!root) {
    return;
  }

  File file = root.openNextFile();

  while (file && tagCount < MAX_TAGUIDS) {
    String fileName = file.name();

    // Check if the file name starts with "sched-" and ends with ".json"
    if (fileName.startsWith("sched-") && fileName.endsWith(".json")) {
      // Read file content
      String fileContent = file.readString();

      // Parse JSON
      const size_t JSON_BUFFER_SIZE = 512; // Adjust based on your needs
      StaticJsonDocument<JSON_BUFFER_SIZE> doc;
      DeserializationError error = deserializeJson(doc, fileContent);

      if (!error) {
        // Extract tagUID and name
        const char* tagUID = doc["petInfo"]["tagUID"];
        const char* name = doc["petInfo"]["name"];

        if (tagUID && name) {
          // Store tagUID and name in arrays
          strncpy(registeredTagUIDs[tagCount], tagUID, 8);
          registeredTagUIDs[tagCount][8] = '\0';  // Ensure null-termination
          strncpy(registeredNames[tagCount], name, 19);
          registeredNames[tagCount][19] = '\0';  // Ensure null-termination

          tagCount++;
        }
      }
    }

    file = root.openNextFile();
  }
}

void initSPIFFS() {
  SPIFFS.begin(true);
  if (!SPIFFS.begin(true)) {
    Serial.println("Failed to mount file system");
    return;
  }
  Serial.print("Total Bytes: ");
  Serial.println(SPIFFS.totalBytes());
  Serial.print("Total Bytes Used: ");
  Serial.println(SPIFFS.usedBytes());
  Serial.println("------------");
  /*
  if (!SPIFFS.begin(true)) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
 
  File root = SPIFFS.open("/");
 
  File file = root.openNextFile();
 
  while(file){
 
      Serial.print("FILE: ");
      Serial.println(file.name());
 
      file = root.openNextFile();
  }*/
}

void initScales() {
  scaleBowl.begin(LOADCELL_BOWL_DOUT_PIN, LOADCELL_BOWL_SCK_PIN);
  scaleBowl.set_offset(4294896511);
  scaleBowl.set_scale(380.392365);
  scaleBowl.tare();

  scale1.begin(LOADCELL_1_DOUT_PIN, LOADCELL_1_SCK_PIN);
  scale1.set_offset(312799);
  scale1.set_scale(392.317871);

  scale2.begin(LOADCELL_2_DOUT_PIN, LOADCELL_2_SCK_PIN);
  scale2.set_offset(388129);
  scale2.set_scale(396.729462);
}

void initRFID() {
  SPI.begin();         //VSPI_SCK, VSPI_MISO, VSPI_MOSI, VSPI_SS
  mfrc522.PCD_Init();  //VSPI_SS, RST_PIN
}

void initServos() {
  Serial.println("Init Servos");
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  servo1.setPeriodHertz(50);                 // Standard 50 Hz servo
  servoBowl.setPeriodHertz(50);                 // Standard 50hz servo
  servo1.attach(SERVO_1_PIN, 771, 2740);     // Attaches the servo on pin 25
  servoBowl.attach(SERVO_BOWL_PIN, 771, 2740);  // Attaches the servo on pin 25
  servoBowl.write(0);                           // 0 position (close)
}

void sendNotification(String title, String message) {
  const char *host = "pushme.vercel.app";
  const uint16_t port = 443;
  String code = (username == "") ? "95570dcjsy6" : username;  //your push.me code here
  String path = "/api/sendNotification";
  Serial.println("sendNotification(): Sending notification: " + message);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  if (https.begin(client, host, port, path)) {
    https.addHeader("Content-Type", "application/json");
    int httpsCode = https.POST("{\"code\":\"" + code + "\",\"title\":\"" + title + "\",\"message\":\"" + message + "\"}");
    if (httpsCode > 0) {
      Serial.println(httpsCode);
      if (httpsCode == HTTP_CODE_OK) {
        Serial.println(https.getString());
      }
    } else {
      Serial.println("failed to POST");
    }
    https.end();
  } else {
    Serial.print("failed to connect to server");
  }
}

void connectToWiFi() {
  int i = 0;
  while (i < sizeof(ssid) / sizeof(ssid[0])) {
    Serial.printf("Connecting to %s\n", ssid[i]);
    WiFi.begin(ssid[i], password[i]);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 3) {
      delay(1000);
      Serial.print(".");
      attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nWiFi connected!");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());
      
      sendNotification(String("SSID: ") + ssid[i], String("IP: ") + String(WiFi.localIP().toString()));
      break;
    } else {
      Serial.println("\nFailed to connect to WiFi");
      i++;
    }
    //startAPMode();
  }
}