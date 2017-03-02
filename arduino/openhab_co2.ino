
/**
 * Simple OpenHAB CO2 sensor based on ESP8266-01 and MH-Z19 CO2 sensor.
*/
#include <EEPROM.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <SoftwareSerial.h>

// RX/TX for MH-Z19 sensor.
#define MHZ_TX 0  // MH_Z19 RX PIN
#define MHZ_RX 2  // MH_Z19 TX PIN

// Check URL for WIFI
#define DEFAULT_CHECK_HOST "192.168.128.100"
#define DEFAULT_CHECK_PORT 9000
#define DEFAULT_CHECK_URL  "/rest"

// Default frequency of updating CO2 value from sensor (in seconds)
#define DEFAULT_UPDATE_FREQUENCY 5

// Default frequency of check whether WiFi is alive (in seconds)
#define DEFAULT_CHECK_FREQUENCY 60

// Default number of values to average
#define DEFAULT_NUM_OF_VALUES_AVG 50

// Local network definition; must have static IP address
#define LOCAL_IP IPAddress (192, 168, 128, 209)
#define GATEWAY IPAddress (192, 168, 128, 1)
#define SUBNETMASK IPAddress (255, 255, 255, 0)

// Local Wifi AP for connect
#define DEFAULT_WIFI_AP "******"
#define DEFAULT_WIFI_PASSWORD "*********"

// Error count for reconnect WIFI
#define ERR_COUNT_FOR_RECONNECT 30
#define HTTP_READ_TIMEOUT       10000
#define HTTP_CONNECT_TIMEOUT    5000

// Local WEB server port
#define WEB_SERVER_PORT 80

// Magic for detecting empty (unconfigured) EEPROM
#define MAGIC 0xCB

/**
 * EEPROM structure.
*/
struct OhConfiguration {
  int magic;
  char apName [24];
  char password [48];
  char checkHost [20];
  unsigned int checkPort;
  char checkUrl [50];
  unsigned int checkFrequency;
  unsigned int updateFrequency;
  unsigned int averageCount;
};

// Configuration
OhConfiguration config;

// Configuration server
ESP8266WebServer *server = (ESP8266WebServer *) NULL;

// Last interaction time
long lastInteractionTime;

// Last check time
long lastCheckTime;

// Number of communication errors
int errorCount = 0;

// Last CO2 value
int lastCo2Ppm = -1;

// Serial at 9600 baud for communication with MH-Z19
SoftwareSerial co2Serial (MHZ_RX, MHZ_TX);

// Average index
byte averageIndex;

// Average count
byte averageCount;

// Average array
int *averageValues = (int *) NULL;


/**
 * Setup method.
*/
void setup() {
  Serial.begin (115200);
  Serial.println ("setup()");

  co2Serial.begin (9600);

  // Do not operate with WiFi in initialization phase
  WiFi.disconnect();

  // Read config from EEPROM
  EEPROM.begin (sizeof (OhConfiguration));
  EEPROM.get (0, config);
  if (config.magic != MAGIC) {
    memset (&config, 0, sizeof (OhConfiguration));
    config.magic = MAGIC;
    updateConfigKey (config.apName, 24, String(DEFAULT_WIFI_AP));
    updateConfigKey (config.password, 48, String(DEFAULT_WIFI_PASSWORD));
    config.updateFrequency = DEFAULT_UPDATE_FREQUENCY;
    updateConfigKey (config.checkHost, 20, String (DEFAULT_CHECK_HOST));
    config.checkPort = DEFAULT_CHECK_PORT;
    updateConfigKey (config.checkUrl, 50, String(DEFAULT_CHECK_URL));
    config.checkFrequency = DEFAULT_CHECK_FREQUENCY;
    config.averageCount = DEFAULT_NUM_OF_VALUES_AVG;
  }

  // Init average array
  averageValues = (int *) calloc (config.averageCount, sizeof(int));
  averageIndex = 0;
  averageCount = 0;

  delay (5000);

  // Warm sensor
  warmSensor();

  // Fill average values
  fillAverageValues();

  // Connect to WiFi and create HTTP server
  reconnectWifi();
  createServer();

  lastInteractionTime = millis();
  lastCheckTime = lastInteractionTime;
}

/**
 * Prints HEX array; for debug purposes.
 * 
 * @param data data.
 * @param length length;
*/
void printHex8 (uint8_t *data, uint8_t length) {
  char tmp [length*2 + 1];
  byte first;
  byte second;
  for (int i = 0; i < length; i++) {
    first = (data[i] >> 4) & 0x0f;
    second = data[i] & 0x0f;
    tmp [i*2] = first + 48;
    tmp [i*2 + 1] = second + 48;
    if (first > 9) {
      tmp [i*2] += 39;
    }

    if (second > 9) {
      tmp [i*2 + 1] += 39;
    }
  }
  tmp [length*2] = 0;
  Serial.print (tmp);
}

/**
 * Reads CO2 sensor and returns CO2 PPM.
 * 
 * @return int CO2 PPM value or -1 on error.
*/
int readCO2() {
  // cleanup input
  while (co2Serial.available()) co2Serial.read();
  
  // command to get PPM value
  byte cmd[9] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x79};

  byte response[9];

  Serial.print ("Sensor request: ");
  printHex8 (cmd, 9);
  Serial.println();
  
  // write command and read response
  co2Serial.write (cmd, 9);
  co2Serial.readBytes (response, 9);

  Serial.print ("Sensor response: ");
  printHex8 (response, 9);
  Serial.println();
  
  // basic check of response
  if ((response[0] != 0xFF) || (response[1] != 0x86)) {
    Serial.println ("Failed to read from CO2 sensor!");
    return -1;
  }

  // calculate and check checksum
  char i, checksum = 0;
  for (i = 1; i < 8; i++) {
    checksum += response [i];
  }
  checksum = 0xff - checksum;
  checksum += 1;

  if (checksum != response [8]) {
    Serial.print ("Invalid checksum "); Serial.print (checksum); Serial.print (" <> "); Serial.print (response [8]); Serial.println ("!");
    return -1;
  }

  // calculate PPM value
  int responseHigh = (int) response[2];
  int responseLow = (int) response[3];
  int ppm = (256 * responseHigh) + responseLow;

  Serial.print ("CO2 PPM: "); Serial.println (ppm);

  return ppm;
}

/**
 * Adds new measure to average values.
*/
void updateCo2Ppm() {
  int val = readCO2();

  // ocassionaly sensor returns 5000, this value can be quietly ignored
  if ((val == 5000) || (val < 0)) {
    return;
  }

  averageValues [averageIndex++] = val;
  if (averageIndex >= config.averageCount) {
    averageIndex = 0;
  }

  averageCount++;
  if (averageCount > config.averageCount) {
    averageCount = config.averageCount;
  }
}

/**
 * Returns average value of CO2 PPM.
 * 
 * @return int average value.
*/
int readCo2Avg() {
  unsigned long sum = 0L;
  int i;
  for (i = 0; i < averageCount; i++) {
    sum += averageValues [i];
  }
  return sum / averageCount;
}


/**
 * Wait for sensor to warmed. Initialization takes almost one minute, after power on.
*/
void warmSensor() {
  Serial.println ("Waiting for sensor init...");
  delay (500);
  Serial.println ("Waiting for PPM=400...");
  while (true) {
    int rv = readCO2();
    if (rv == 400) {
      break;
    }
    delay (2000);
  }

  delay (1000);
  Serial.println ("Waiting for PPM <> 400...");
  while (true) {
    int rv = readCO2();
    if (rv != 400) {
      break;
    }
    
    delay (2000);
  }

  Serial.println ("Done...");
}

/**
 * Initializes average values. Fills all averageValues.
*/
void fillAverageValues() {
  Serial.println ("Initializing average values...");
  int i;
  for (i = 0; i < config.averageCount; i++) {
    averageValues [i] = readCO2();
    delay (config.updateFrequency * 1000L);
  }
  averageIndex = 0;
  averageCount = config.averageCount;
}


/**
 * Helper routine; updates config key.
 *
 * @param c key.
 * @param len max length.
 * @param val value.
*/
void updateConfigKey (char *c, int len, String val) {
  memset (c, 0, len);
  sprintf (c, "%s", val.c_str());
}

/**
 * Reconects WIFI.
*/
void reconnectWifi() {
  Serial.println ("WiFi disconnected...");
  WiFi.disconnect();
  String hostname = String("openhab_co2 sensor");
  WiFi.hostname (hostname);
  WiFi.config (LOCAL_IP, GATEWAY, SUBNETMASK);
  WiFi.mode (WIFI_STA);
  delay (1000);

  Serial.print ("Connecting to "); Serial.print (config.apName); Serial.print (' ');
  WiFi.begin (config.apName, config.password);
  delay (1000);
  while (WiFi.status() != WL_CONNECTED) {
    yield();
    delay (500);
    Serial.print (".");
  }
  delay (500);
  Serial.println();
  Serial.println ("WiFi connected...");
}

/**
 * Creates HTTP server.
*/
void createServer() {
  if (server) {
    server->close();
    delete server;
    server = NULL;
  }
  
  // ... and run HTTP server for setup
  server = new ESP8266WebServer (LOCAL_IP, WEB_SERVER_PORT);
  server->on ("/co2", wsGetCo2);
  server->on ("/", wsConfig);
  server->on ("/update", wsUpdate);
  server->on ("/reconnect", wsReconnect);
  server->begin();

  Serial.println ("Created server...");
}

/**
 * WS GETs CO2 PPM.
*/
void wsGetCo2() {
  int avg = readCo2Avg();
  Serial.print ("wsGetCo2() -> "); Serial.println (avg);
  server->send (200, "text/html", String(avg));
}

/**
 * Configuration page.
*/
void wsConfig() {
  yield();

  String resp = "<html><head><title>OpenHAB CO2 sensor configuration</title>";
  resp += "<meta name=\"viewport\" content=\"initial-scale=1.0, width = device-width, user-scalable = no\">";
  resp += "</head><body>";
  resp += "<h1>OpenHAB CO2 sensor configuration</h1>";
  resp += "<form method=\"post\" action=\"/update\" id=\"form\">";
  resp += "<table border=\"0\" cellspacing=\"0\" cellpadding=\"5\">";
  resp += "<tr><td>AP SSID:</td><td><input type=\"text\" name=\"apName\" value=\"" + String(config.apName) + "\" maxlength=\"24\"></td><td></td></tr>";
  resp += "<tr><td>AP Password:</td><td><input type=\"password\" name=\"password\" value=\"" + String(config.password) + "\" maxlength=\"48\"></td><td></td></tr>";
  resp += "<tr><td>Update sensor time [sec]:</td><td><input type=\"text\" name=\"updateFrequency\" value=\"" + String(config.updateFrequency) + "\"></td><td></td></tr>";
  resp += "<tr><td>Check host IP:</td><td><input type=\"text\" name=\"checkHost\" value=\"" + String(config.checkHost) + "\" maxlength=\"20\"></td><td></td></tr>";
  resp += "<tr><td>Check port:</td><td><input type=\"text\" name=\"checkPort\" value=\"" + String(config.checkPort) + "\" maxlength=\"5\"></td><td></td></tr>";
  resp += "<tr><td>Check URL:</td><td><input type=\"text\" name=\"checkUrl\" value=\"" + String(config.checkUrl) + "\" maxlength=\"50\"></td><td></td></tr>";
  resp += "<tr><td>Check frequency [sec]:</td><td><input type=\"text\" name=\"checkFrequency\" value=\"" + String(config.checkFrequency) + "\"></td><td></td></tr>";
  resp += "<tr><td>Average num of values:</td><td><input type=\"text\" name=\"averageCount\" value=\"" + String(config.averageCount) + "\"></td><td></td></tr>";

  resp += "<tr><td colspan=\"3\" align=\"center\"><input type=\"submit\" value=\"Save\"></td></tr>";
  resp += "</table></form>";
  resp += "<p><a href=\"/reconnect\">Reconnect WiFi...</a></p>";
  resp += "</body></html>";

  server->send (200, "text/html", resp);
}

/**
 * Reconnects WiFi with new parameters.
*/
void wsReconnect() {
  yield();
  String resp = "<script>window.alert ('Reconnecting WiFi...'); window.location.replace ('/');</script>";
  server->send (200, "text/html", resp);
  reconnectWifi();
  createServer();
}

/**
 * Saves configuration.
*/
void wsUpdate() {
  yield();

  String apName = server->arg ("apName");
  String password = server->arg ("password");
  unsigned int updateFrequency = atoi (server->arg ("updateFrequency").c_str());
  String checkHost = server->arg ("checkHost");
  unsigned int checkPort = atoi (server->arg ("checkPort").c_str());
  String checkUrl = server->arg ("checkUrl");
  unsigned int checkFrequency = atoi (server->arg("checkFrequency").c_str());
  int averageCount = atoi (server->arg("averageCount").c_str());
  
  if (apName.length() > 1) {
    updateConfigKey (config.apName, 24, apName);
    updateConfigKey (config.password, 48, password);
    config.updateFrequency = constrain (updateFrequency, 1, 600);
    updateConfigKey (config.checkHost, 20, checkHost);
    config.checkPort = constrain (checkPort, 1, 65535);
    updateConfigKey (config.checkUrl, 50, checkUrl);
    config.checkFrequency = constrain (checkFrequency, 0, 30000);
    config.averageCount = constrain (averageCount, 1, 120);

    // init average array
    free (averageValues);
    averageValues = (int *) calloc (config.averageCount, sizeof(int));
    averageValues [0] = readCO2();
    averageIndex = 1;
    averageCount = 1;
  
    // store configuration
    EEPROM.begin (sizeof (OhConfiguration));
    EEPROM.put (0, config);
    EEPROM.end();
  
    String resp = "<script>window.alert ('Configuration updated...'); window.location.replace ('/');</script>";
    server->send (200, "text/html", resp);
  } else {
    server->send (200, "text/html", "");
  }
}

/**
 * Checks WIFI connection by getting check URL.
*/
void checkWiFi() {
  if ((strlen (config.checkHost) < 1) || (config.checkFrequency < 1)) {
    return;
  }

  WiFiClient client;

  // connect to OpenHAB
  Serial.print ("Connecting to http://"); Serial.print (config.checkHost); Serial.print (':'); Serial.print (config.checkPort); Serial.print (config.checkUrl); Serial.println (" ...");
  if (client.connect (config.checkHost, config.checkPort)) {
    // send request
    String req = String("GET ") + config.checkUrl + String (" HTTP/1.1\r\n")
      + String("Host: ") + String (config.checkHost) + String ("\r\nConnection: close\r\n\r\n");
    client.print (req);
    Serial.print (req);

    bool isError = false;
    
    // wait HTTP_CONNECT_TIMEOUT for response
    unsigned long connectStartTime = millis();
    while (client.available() == 0) {
      if (millis() - connectStartTime > HTTP_CONNECT_TIMEOUT) {
        errorCount++;
        isError = true;
        break;
      }

      yield();
    }

    Serial.print ("Reading response -> ");
    if (!isError) {
      // read response lines
      unsigned long readStartTime = millis();

      int ch;
      while ((ch = client.read()) != -1) {
        if (millis() - readStartTime > HTTP_READ_TIMEOUT) {
          errorCount++;
          isError = true;
          break;
        }

        yield();
      }

      Serial.println ("OK");
      
      client.stop();
    } else {
      Serial.println ("ERROR");
      client.stop();
    }
  }
}

/**
 * Loop.
*/
void loop() {
  if (server) {
    server->handleClient();
  }
  
  if (millis() - lastInteractionTime > config.updateFrequency * 1000L) {
    yield();

    updateCo2Ppm();

    lastInteractionTime = millis();
  }

  if (millis() - lastCheckTime > config.checkFrequency * 1000L) {
    yield();
    
    if (WiFi.status() == WL_CONNECTED) {
      checkWiFi();
    } else {
      Serial.println ("Wifi not connected!");
      errorCount++;
    }

    // when error count reaches ERR_COUNT_FOR_RECONNECT, reconnect WiFi.
    if (errorCount > ERR_COUNT_FOR_RECONNECT) {
      errorCount = 0;
      reconnectWifi();
      createServer();
    }

    lastCheckTime = millis();
  }

  yield();
}

