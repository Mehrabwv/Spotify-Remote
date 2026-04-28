#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "arduino_secrets.h"

// ---------- OLED ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// ---------- Button ----------
#define BUTTON_PIN 8

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------- Spotify Clients ----------
WiFiSSLClient tokenWifi;
WiFiSSLClient apiWifi;

HttpClient tokenClient(tokenWifi, "accounts.spotify.com", 443);
HttpClient apiClient(apiWifi, "api.spotify.com", 443);

// ---------- Runtime ----------
String accessToken = "";

String currentSong = "Loading...";
String currentArtist = "";
String playingStatus = "Starting";
String debugStatus = "";

unsigned long lastTokenRefresh = 0;
unsigned long lastTrackCheck = 0;

const unsigned long TOKEN_REFRESH_INTERVAL = 50UL * 60UL * 1000UL;
const unsigned long TRACK_CHECK_INTERVAL = 3000;

// ---------- Button Logic ----------
bool lastButtonState = HIGH;
bool buttonIsHeld = false;
bool longPressTriggered = false;
bool wasPlayingBeforeButton = false;

unsigned long buttonPressStart = 0;
unsigned long lastButtonEventTime = 0;

const unsigned long BUTTON_DEBOUNCE_TIME = 80;
const unsigned long LONG_PRESS_TIME = 5000;

void setup() {
  Serial.begin(115200);
  Wire.begin();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(WHITE);

  showOLED("Spotify Remote", "Starting...");

  connectWiFi();

  showOLED("Spotify", "Getting token...");

  if (!refreshAccessToken()) {
    showOLED("Token failed", debugStatus);
    while (true) {
      delay(1000);
    }
  }

  showOLED("Token OK", "Checking song...");
  getCurrentTrack();
}

void loop() {
  handleButton();

  if (millis() - lastTokenRefresh > TOKEN_REFRESH_INTERVAL) {
    refreshAccessToken();
  }

  if (millis() - lastTrackCheck > TRACK_CHECK_INTERVAL) {
    getCurrentTrack();
  }

  drawSpotifyScreen();
  delay(20);
}

// ---------- Button ----------

void handleButton() {
  bool buttonState = digitalRead(BUTTON_PIN);

  // Button just pressed
  if (lastButtonState == HIGH && buttonState == LOW) {
    if (millis() - lastButtonEventTime > BUTTON_DEBOUNCE_TIME) {
      buttonPressStart = millis();
      buttonIsHeld = true;
      longPressTriggered = false;
      lastButtonEventTime = millis();

      // Important fix:
      // Remember the Spotify state BEFORE changing OLED message.
      wasPlayingBeforeButton = (playingStatus == "Playing");

      currentSong = "Hold 5s to skip";
      currentArtist = "Release = play/pause";
      drawSpotifyScreen();
    }
  }

  // Button is being held
  if (buttonIsHeld && buttonState == LOW && !longPressTriggered) {
    unsigned long heldTime = millis() - buttonPressStart;

    if (heldTime >= LONG_PRESS_TIME) {
      longPressTriggered = true;

      Serial.println("Long press: skip track");

      playingStatus = "Skipping...";
      currentSong = "Next track";
      currentArtist = "";
      drawSpotifyScreen();

      skipNextTrack();

      delay(1500);
      getCurrentTrack();
    }
  }

  // Button released
  if (lastButtonState == LOW && buttonState == HIGH) {
    if (millis() - lastButtonEventTime > BUTTON_DEBOUNCE_TIME) {
      lastButtonEventTime = millis();

      if (buttonIsHeld && !longPressTriggered) {
        unsigned long pressDuration = millis() - buttonPressStart;

        if (pressDuration < LONG_PRESS_TIME) {
          Serial.println("Short press: toggle play/pause");

          if (wasPlayingBeforeButton) {
            playingStatus = "Pausing...";
            currentSong = "Sending pause";
            currentArtist = "";
            drawSpotifyScreen();

            pausePlayback();
          } else {
            playingStatus = "Playing...";
            currentSong = "Sending play";
            currentArtist = "";
            drawSpotifyScreen();

            resumePlayback();
          }

          delay(1000);
          getCurrentTrack();
        }
      }

      buttonIsHeld = false;
    }
  }

  lastButtonState = buttonState;
}

// ---------- WiFi ----------

void connectWiFi() {
  showOLED("Connecting WiFi", ssid);

  while (WiFi.begin(ssid, pass) != WL_CONNECTED) {
    showOLED("WiFi failed", "Retrying...");
    delay(3000);
  }

  IPAddress ip = WiFi.localIP();
  String ipText = String(ip[0]) + "." + String(ip[1]) + "." + String(ip[2]) + "." + String(ip[3]);

  Serial.print("WiFi connected. IP: ");
  Serial.println(ipText);

  showOLED("WiFi connected", ipText);
  delay(1200);
}

// ---------- Spotify Token ----------

bool refreshAccessToken() {
  tokenWifi.stop();

  String body = "";
  body += "grant_type=refresh_token";
  body += "&refresh_token=";
  body += urlEncode(SPOTIFY_REFRESH_TOKEN);
  body += "&client_id=";
  body += urlEncode(SPOTIFY_CLIENT_ID);
  body += "&client_secret=";
  body += urlEncode(SPOTIFY_CLIENT_SECRET);

  tokenClient.beginRequest();
  tokenClient.post("/api/token");
  tokenClient.sendHeader("Content-Type", "application/x-www-form-urlencoded");
  tokenClient.sendHeader("Content-Length", body.length());
  tokenClient.sendHeader("Connection", "close");
  tokenClient.beginBody();
  tokenClient.print(body);
  tokenClient.endRequest();

  int statusCode = tokenClient.responseStatusCode();
  String response = tokenClient.responseBody();

  tokenWifi.stop();

  Serial.println("----- TOKEN RESPONSE -----");
  Serial.print("Token status: ");
  Serial.println(statusCode);

  debugStatus = "Token HTTP " + String(statusCode);

  if (statusCode != 200) {
    playingStatus = "Token error";
    currentSong = "Token failed";
    currentArtist = "HTTP " + String(statusCode);
    return false;
  }

  String token = extractJsonString(response, "access_token");

  if (token.length() < 20) {
    playingStatus = "Token parse error";
    currentSong = "No token found";
    currentArtist = "";
    return false;
  }

  accessToken = token;
  lastTokenRefresh = millis();

  Serial.println("Access token refreshed OK");
  return true;
}

// ---------- Spotify Controls ----------

bool pausePlayback() {
  return sendSpotifyControlPut("/v1/me/player/pause", "Pause");
}

bool resumePlayback() {
  return sendSpotifyControlPut("/v1/me/player/play", "Play");
}

bool skipNextTrack() {
  if (accessToken.length() < 20) {
    if (!refreshAccessToken()) return false;
  }

  apiWifi.stop();

  String authHeader = "Bearer " + accessToken;

  apiClient.beginRequest();
  apiClient.post("/v1/me/player/next");
  apiClient.sendHeader("Authorization", authHeader);
  apiClient.sendHeader("Content-Length", "0");
  apiClient.sendHeader("Connection", "close");
  apiClient.endRequest();

  int statusCode = apiClient.responseStatusCode();

  Serial.println("----- SKIP RESPONSE -----");
  Serial.print("Skip status: ");
  Serial.println(statusCode);

  apiWifi.stop();

  if (statusCode == 204) {
    debugStatus = "Skip OK";
    playingStatus = "Skipped";
    currentSong = "Loading next...";
    currentArtist = "";
    return true;
  }

  handleControlError(statusCode, "Skip");
  return false;
}

bool sendSpotifyControlPut(String path, String label) {
  if (accessToken.length() < 20) {
    if (!refreshAccessToken()) return false;
  }

  apiWifi.stop();

  String authHeader = "Bearer " + accessToken;

  apiClient.beginRequest();
  apiClient.put(path);
  apiClient.sendHeader("Authorization", authHeader);
  apiClient.sendHeader("Content-Length", "0");
  apiClient.sendHeader("Connection", "close");
  apiClient.endRequest();

  int statusCode = apiClient.responseStatusCode();

  Serial.print("----- ");
  Serial.print(label);
  Serial.println(" RESPONSE -----");

  Serial.print(label);
  Serial.print(" status: ");
  Serial.println(statusCode);

  apiWifi.stop();

  if (statusCode == 204) {
    debugStatus = label + " OK";
    return true;
  }

  handleControlError(statusCode, label);
  return false;
}

void handleControlError(int statusCode, String action) {
  if (statusCode == 401) {
    debugStatus = action + " 401";
    playingStatus = "Refreshing token";
    refreshAccessToken();
  }
  else if (statusCode == 403) {
    debugStatus = action + " 403";
    playingStatus = "Forbidden";
    currentSong = "Need Premium?";
    currentArtist = "Check device";
  }
  else if (statusCode == 404) {
    debugStatus = action + " 404";
    playingStatus = "No device";
    currentSong = "Open Spotify";
    currentArtist = "Start playback";
  }
  else if (statusCode == 429) {
    debugStatus = action + " 429";
    playingStatus = "Rate limited";
    currentSong = "Too many requests";
    currentArtist = "Wait";
  }
  else {
    debugStatus = action + " HTTP " + String(statusCode);
    playingStatus = action + " error";
    currentSong = "HTTP " + String(statusCode);
    currentArtist = "See Serial";
  }
}

// ---------- Spotify Current Track ----------

void getCurrentTrack() {
  if (accessToken.length() < 20) {
    if (!refreshAccessToken()) {
      return;
    }
  }

  apiWifi.stop();

  String authHeader = "Bearer " + accessToken;

  apiClient.beginRequest();
  apiClient.get("/v1/me/player/currently-playing?market=SG");
  apiClient.sendHeader("Authorization", authHeader);
  apiClient.sendHeader("Accept", "application/json");
  apiClient.sendHeader("Connection", "close");
  apiClient.endRequest();

  int statusCode = apiClient.responseStatusCode();

  Serial.println("----- CURRENT TRACK RESPONSE -----");
  Serial.print("Track status: ");
  Serial.println(statusCode);

  debugStatus = "Track HTTP " + String(statusCode);

  if (statusCode == 204) {
    playingStatus = "No playback";
    currentSong = "Nothing playing";
    currentArtist = "Open Spotify first";
    lastTrackCheck = millis();
    apiWifi.stop();
    return;
  }

  if (statusCode == 401) {
    playingStatus = "Refreshing token";
    currentSong = "Token expired";
    currentArtist = "Trying again";
    refreshAccessToken();
    lastTrackCheck = millis();
    apiWifi.stop();
    return;
  }

  if (statusCode == 403) {
    playingStatus = "Forbidden";
    currentSong = "Check scopes";
    currentArtist = "or Premium/device";
    lastTrackCheck = millis();
    apiWifi.stop();
    return;
  }

  if (statusCode == 429) {
    playingStatus = "Rate limited";
    currentSong = "Too many requests";
    currentArtist = "Wait a bit";
    lastTrackCheck = millis();
    apiWifi.stop();
    return;
  }

  if (statusCode != 200) {
    playingStatus = "Spotify error";
    currentSong = "HTTP " + String(statusCode);
    currentArtist = "See Serial Monitor";
    lastTrackCheck = millis();
    apiWifi.stop();
    return;
  }

  apiClient.skipResponseHeaders();

  parseCurrentTrackStream(apiClient);

  lastTrackCheck = millis();
  apiWifi.stop();
}

// ---------- Stream Parser ----------

void parseCurrentTrackStream(HttpClient &client) {
  currentSong = "Reading...";
  currentArtist = "";
  playingStatus = "Unknown";

  int objectDepth = 0;
  int arrayDepth = 0;

  int itemDepth = -1;
  int albumDepth = -1;
  int artistArrayDepth = -1;

  bool inString = false;
  bool escapeNext = false;
  bool pendingStringReady = false;
  bool expectingValue = false;

  bool gotSong = false;
  bool gotArtist = false;

  String token = "";
  String pendingString = "";
  String lastKey = "";

  unsigned long startTime = millis();

  while (millis() - startTime < 12000) {
    int value = client.read();

    if (value < 0) {
      if (!client.connected()) {
        break;
      }
      delay(1);
      continue;
    }

    char c = (char)value;

    if (inString) {
      if (escapeNext) {
        if (token.length() < 120) token += c;
        escapeNext = false;
      }
      else if (c == '\\') {
        escapeNext = true;
      }
      else if (c == '"') {
        inString = false;
        pendingString = token;
        pendingStringReady = true;
        token = "";
      }
      else {
        if (token.length() < 120) token += c;
      }

      continue;
    }

    if (c == '"') {
      inString = true;
      token = "";
      continue;
    }

    if (isJsonWhitespace(c)) {
      continue;
    }

    if (pendingStringReady) {
      if (c == ':') {
        lastKey = pendingString;
        expectingValue = true;
        pendingStringReady = false;
        continue;
      } else {
        if (expectingValue) {
          processStringValue(
            lastKey,
            pendingString,
            objectDepth,
            itemDepth,
            albumDepth,
            artistArrayDepth,
            gotSong,
            gotArtist
          );

          expectingValue = false;
        }

        pendingStringReady = false;
      }
    }

    if (expectingValue && lastKey == "is_playing") {
      if (c == 't') {
        playingStatus = "Playing";
        expectingValue = false;
      }
      else if (c == 'f') {
        playingStatus = "Paused";
        expectingValue = false;
      }
    }

    if (c == '{') {
      int newDepth = objectDepth + 1;

      if (expectingValue && lastKey == "item") {
        itemDepth = newDepth;
      }

      if (expectingValue && lastKey == "album" && itemDepth > 0 && objectDepth == itemDepth) {
        albumDepth = newDepth;
      }

      objectDepth = newDepth;
      expectingValue = false;
    }
    else if (c == '}') {
      if (objectDepth == albumDepth) {
        albumDepth = -1;
      }

      if (objectDepth == itemDepth) {
        itemDepth = -1;
      }

      objectDepth--;
      if (objectDepth < 0) objectDepth = 0;

      expectingValue = false;
    }
    else if (c == '[') {
      int newArrayDepth = arrayDepth + 1;

      if (expectingValue &&
          lastKey == "artists" &&
          itemDepth > 0 &&
          albumDepth < 0 &&
          objectDepth == itemDepth) {
        artistArrayDepth = newArrayDepth;
      }

      arrayDepth = newArrayDepth;
      expectingValue = false;
    }
    else if (c == ']') {
      if (arrayDepth == artistArrayDepth) {
        artistArrayDepth = -1;
      }

      arrayDepth--;
      if (arrayDepth < 0) arrayDepth = 0;

      expectingValue = false;
    }
    else if (c == ',') {
      expectingValue = false;
    }

    if (gotSong && gotArtist && playingStatus != "Unknown") {
      break;
    }
  }

  if (!gotSong) {
    currentSong = "Parse failed";
  }

  if (!gotArtist) {
    currentArtist = "Unknown artist";
  }

  if (playingStatus == "Unknown") {
    playingStatus = "Status unknown";
  }

  Serial.print("Parsed song: ");
  Serial.println(currentSong);

  Serial.print("Parsed artist: ");
  Serial.println(currentArtist);

  Serial.print("Status: ");
  Serial.println(playingStatus);
}

void processStringValue(
  String key,
  String value,
  int objectDepth,
  int itemDepth,
  int albumDepth,
  int artistArrayDepth,
  bool &gotSong,
  bool &gotArtist
) {
  if (!gotSong &&
      key == "name" &&
      itemDepth > 0 &&
      objectDepth == itemDepth &&
      albumDepth < 0 &&
      artistArrayDepth < 0) {
    currentSong = cleanSpotifyText(value);
    gotSong = true;
  }

  if (!gotArtist &&
      key == "name" &&
      itemDepth > 0 &&
      artistArrayDepth > 0 &&
      objectDepth == itemDepth + 1) {
    currentArtist = cleanSpotifyText(value);
    gotArtist = true;
  }
}

// ---------- Text Cleaner ----------

String cleanSpotifyText(String text) {
  String output = "";

  for (int i = 0; i < text.length(); i++) {
    uint8_t c = (uint8_t)text[i];

    if (c < 128) {
      output += (char)c;
      continue;
    }

    if (c == 0xC2 && i + 1 < text.length()) {
      uint8_t c2 = (uint8_t)text[i + 1];

      if (c2 == 0xA0) {
        output += ' ';
        i++;
        continue;
      }
    }

    if (c == 0xC3 && i + 1 < text.length()) {
      uint8_t c2 = (uint8_t)text[i + 1];

      if (c2 == 0xA9 || c2 == 0xA8 || c2 == 0xAA || c2 == 0xAB) output += 'e';
      else if (c2 == 0xA1 || c2 == 0xA0 || c2 == 0xA2 || c2 == 0xA4 || c2 == 0xA5) output += 'a';
      else if (c2 == 0xAD || c2 == 0xAC || c2 == 0xAE || c2 == 0xAF) output += 'i';
      else if (c2 == 0xB3 || c2 == 0xB2 || c2 == 0xB4 || c2 == 0xB6) output += 'o';
      else if (c2 == 0xBA || c2 == 0xB9 || c2 == 0xBB || c2 == 0xBC) output += 'u';
      else if (c2 == 0xB1) output += 'n';
      else if (c2 == 0xA7) output += 'c';
      else output += '?';

      i++;
      continue;
    }

    if (c == 0xE2 && i + 2 < text.length()) {
      uint8_t c2 = (uint8_t)text[i + 1];
      uint8_t c3 = (uint8_t)text[i + 2];

      if (c2 == 0x80 && (c3 == 0x98 || c3 == 0x99)) {
        output += '\'';
        i += 2;
        continue;
      }

      if (c2 == 0x80 && (c3 == 0x9C || c3 == 0x9D)) {
        output += '"';
        i += 2;
        continue;
      }

      if (c2 == 0x80 && (c3 == 0x93 || c3 == 0x94)) {
        output += '-';
        i += 2;
        continue;
      }

      if (c2 == 0x80 && c3 == 0xA6) {
        output += "...";
        i += 2;
        continue;
      }
    }

    output += '?';
  }

  return output;
}

// ---------- Helpers ----------

bool isJsonWhitespace(char c) {
  return c == ' ' || c == '\n' || c == '\r' || c == '\t';
}

String extractJsonString(String json, String key) {
  String searchKey = "\"" + key + "\"";
  int keyIndex = json.indexOf(searchKey);

  if (keyIndex < 0) return "";

  return extractStringValueAt(json, keyIndex);
}

String extractStringValueAt(String json, int keyIndex) {
  int colonIndex = json.indexOf(":", keyIndex);
  if (colonIndex < 0) return "";

  int firstQuote = json.indexOf("\"", colonIndex + 1);
  if (firstQuote < 0) return "";

  int secondQuote = findClosingQuote(json, firstQuote + 1);
  if (secondQuote < 0) return "";

  return json.substring(firstQuote + 1, secondQuote);
}

int findClosingQuote(String text, int start) {
  bool escaped = false;

  for (int i = start; i < text.length(); i++) {
    char c = text[i];

    if (escaped) {
      escaped = false;
      continue;
    }

    if (c == '\\') {
      escaped = true;
      continue;
    }

    if (c == '"') {
      return i;
    }
  }

  return -1;
}

String urlEncode(const char* input) {
  String encoded = "";

  for (int i = 0; input[i] != '\0'; i++) {
    char c = input[i];

    if (
      (c >= 'A' && c <= 'Z') ||
      (c >= 'a' && c <= 'z') ||
      (c >= '0' && c <= '9') ||
      c == '-' || c == '_' || c == '.' || c == '~'
    ) {
      encoded += c;
    } else {
      encoded += '%';

      char hex1 = "0123456789ABCDEF"[(c >> 4) & 0x0F];
      char hex2 = "0123456789ABCDEF"[c & 0x0F];

      encoded += hex1;
      encoded += hex2;
    }
  }

  return encoded;
}

// ---------- OLED ----------

void drawSpotifyScreen() {
  display.clearDisplay();
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("Spotify Remote");

  display.setCursor(0, 12);
  display.print("State: ");
  display.println(playingStatus);

  display.setCursor(0, 26);
  display.println(shortenText(currentSong, 20));

  display.setCursor(0, 38);
  display.println(shortenText(currentArtist, 20));

  display.setCursor(0, 52);
  display.print(debugStatus);

  display.display();
}

void showOLED(String line1, String line2) {
  display.clearDisplay();
  display.setTextSize(1);

  display.setCursor(0, 18);
  display.println(line1);

  display.setCursor(0, 34);
  display.println(line2);

  display.display();
}

String shortenText(String text, int maxLen) {
  if (text.length() <= maxLen) return text;
  return text.substring(0, maxLen - 3) + "...";
}