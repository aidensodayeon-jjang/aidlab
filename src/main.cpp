#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <SD.h>

// --- 설정 ---
const char *ssid = "SODATA02AD";
const char *password = "EEB75002AC";
const char *apiKey = "2adbcdbbad353a31d8a2437622de087b";
const char *city = "Seoul,kr";
const char *fontPath = "/NanumBarunGothic-20.vlw"; 

struct Stock { String name; String price; String change; };
Stock myStocks[4]; Stock myIndices[2]; String myNews[5];  
int infoIndex = 0; unsigned long lastInfoUpdate = 0;
String weatherKR = "확인중"; float currentTemp = 0.0;
bool isWifiConnected = false; unsigned long lastWeatherUpdate = 0;

int centerX = 160, centerY = 120, eyeDistance = 70, eyeSize = 60;
float lookX = 0, lookY = 0;
unsigned long lastBlink = 0, lastExpressionChange = 0, touchTimer = 0, modeTimer = 0;
enum Emotion { NEUTRAL, SLEEPY, BORED, SURPRISED, HAPPY };
Emotion currentEmotion = NEUTRAL;
enum DisplayMode { FACE_MODE, CLOCK_MODE };
DisplayMode currentMode = CLOCK_MODE;

bool isSdFontLoaded = false;
uint8_t* globalFontBuffer = nullptr;

void fetchNews();
void fetchStocks();
void fetchWeather();
void drawClock();

void loadLocalData() {
  if (!SPIFFS.begin(true)) return;
  if (!SPIFFS.exists("/data.json")) return;
  File file = SPIFFS.open("/data.json", "r");
  DynamicJsonDocument doc(3072);
  deserializeJson(doc, file);
  file.close();
  for (int i = 0; i < 2; i++) {
    myIndices[i].name = doc["indices"][i]["name"].as<String>();
    myIndices[i].price = doc["indices"][i]["price"].as<String>();
    myIndices[i].change = doc["indices"][i]["change"].as<String>();
  }
  for (int i = 0; i < 4; i++) {
    myStocks[i].name = doc["stocks"][i]["name"].as<String>();
    myStocks[i].price = doc["stocks"][i]["price"].as<String>();
    myStocks[i].change = doc["stocks"][i]["change"].as<String>();
  }
  for (int i = 0; i < 5; i++) { myNews[i] = doc["news"][i].as<String>(); }
}

void fetchStocks() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  const char* symbols[] = {"^KS11", "^KQ11", "005930.KS", "000660.KS", "085660.KQ", "AAPL"};
  const char* names[] = {"코스피", "코스닥", "삼성전자", "SK하이닉스", "차바이오텍", "애플"};
  for (int i = 0; i < 6; i++) {
    String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + String(symbols[i]) + "?interval=1d&range=2d";
    http.begin(url); http.addHeader("User-Agent", "Mozilla/5.0");
    if (http.GET() == 200) {
      DynamicJsonDocument doc(4096); deserializeJson(doc, http.getString());
      if (doc.containsKey("chart")) {
          JsonObject result = doc["chart"]["result"][0]["meta"];
          float current = result["regularMarketPrice"];
          float prev = result["previousClose"];
          if (current == 0) current = result["chartPreviousClose"];
          float change = (prev != 0) ? ((current - prev) / prev) * 100.0 : 0.0;
          String priceStr = (current >= 1000) ? String((int)current) : String(current, 2);
          String changeStr = (change >= 0 ? "+" : "") + String(change, 2);
          if (i < 2) { myIndices[i].name = names[i]; myIndices[i].price = priceStr; myIndices[i].change = changeStr; }
          else { myStocks[i-2].name = names[i]; myStocks[i-2].price = priceStr; myStocks[i-2].change = changeStr; }
      }
    }
    http.end(); delay(100);
  }
}

void fetchNews() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  http.begin("https://www.yonhapnewstv.co.kr/browse/feed/");
  http.addHeader("User-Agent", "Mozilla/5.0");
  if (http.GET() == 200) {
    String payload = http.getString();
    int startIdx = 0; int newsCount = 0;
    for (int i = 0; i < 5; i++) {
      int itemIdx = payload.indexOf("<item>", startIdx);
      if (itemIdx == -1) break;
      int titleStart = payload.indexOf("<title>", itemIdx);
      if (titleStart == -1) break;
      titleStart += 7;
      int titleEnd = payload.indexOf("</title>", titleStart);
      if (titleEnd == -1) break;
      String title = payload.substring(titleStart, titleEnd);
      title.replace("<![CDATA[", ""); title.replace("]]>", "");
      title.replace("▶", " "); title.replace("·", " "); title.replace("…", "...");
      title.replace("\"", "'"); title.trim();
      if (title.length() > 5) { myNews[newsCount] = title; newsCount++; }
      startIdx = titleEnd;
    }
    if (currentMode == CLOCK_MODE) drawClock();
  }
  http.end();
}

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED) return;
  HTTPClient http;
  String url = "http://api.openweathermap.org/data/2.5/weather?q=" + String(city) + "&units=metric&lang=kr&appid=" + String(apiKey);
  http.begin(url);
  if (http.GET() == 200) {
    DynamicJsonDocument doc(1024); deserializeJson(doc, http.getString());
    if (doc.containsKey("main")) currentTemp = doc["main"]["temp"];
    if (doc.containsKey("weather")) weatherKR = doc["weather"][0]["description"].as<String>();
    if (doc.containsKey("dt")) {
        long dt = doc["dt"]; int tz = doc.containsKey("timezone") ? doc["timezone"].as<int>() : 32400;
        time_t local = dt + tz; struct tm *tm_ptr = gmtime(&local); M5.Rtc.setDateTime(tm_ptr);
    }
  }
  http.end();
}

void drawClock() {
  auto d = M5.Rtc.getDate(); auto t = M5.Rtc.getTime();
  const char *daysKR[] = {"일요일", "월요일", "화요일", "수요일", "목요일", "금요일", "토요일"};

  M5.Display.startWrite();
  M5.Display.fillScreen(WHITE);
  
  // 1. 상단 바: 깔끔한 그레이 & 블랙
  M5.Display.fillRect(0, 0, 320, 35, M5.Display.color565(240, 240, 240));
  M5.Display.drawFastHLine(0, 35, 320, M5.Display.color565(200, 200, 200));
  
  if (isSdFontLoaded && globalFontBuffer) M5.Display.loadFont(globalFontBuffer);
  else M5.Display.setFont(&fonts::efontKR_14);
  
  M5.Display.setTextColor(BLACK);
  char topStr[120];
  sprintf(topStr, "%04d.%02d.%02d %s | %s %.1fC", d.year, d.month, d.date, daysKR[d.weekDay % 7], weatherKR.c_str(), currentTemp);
  M5.Display.drawCenterString(topStr, 160, 8);

  // 2. 시계: 세련된 다크 그레이/블루
  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  M5.Display.setTextColor(M5.Display.color565(40, 40, 80)); 
  char timeStr[10]; sprintf(timeStr, "%02d:%02d", t.hours, t.minutes);
  M5.Display.drawCenterString(timeStr, 160, 52);

  // 3. 정보 영역 구분선
  M5.Display.drawFastHLine(15, 115, 290, BLACK);
  M5.Display.drawFastHLine(15, 235, 290, BLACK);

  if (isSdFontLoaded && globalFontBuffer) M5.Display.loadFont(globalFontBuffer);
  else M5.Display.setFont(&fonts::efontKR_24);

  int totalSteps = 8; int step = infoIndex % totalSteps;

  if (step < 5) {
    M5.Display.setTextColor(M5.Display.color565(200, 100, 0)); // 오렌지 포인트
    M5.Display.setCursor(20, 122); M5.Display.printf("NEWS %d/5", step + 1); 
    M5.Display.setTextColor(BLACK); M5.Display.setTextWrap(true); 
    M5.Display.clearClipRect(); M5.Display.setClipRect(20, 148, 280, 85); 
    M5.Display.setCursor(20, 148); M5.Display.print(myNews[step]);
    M5.Display.clearClipRect();
  } else if (step == 5) {
    M5.Display.setTextColor(M5.Display.color565(0, 120, 150)); // 청록 포인트
    M5.Display.setCursor(20, 120); M5.Display.print("MARKET INDEX");
    for (int i = 0; i < 2; i++) {
        int yPos = 155 + (i * 38);
        M5.Display.setTextColor(BLACK); M5.Display.setCursor(20, yPos); M5.Display.print(myIndices[i].name); 
        M5.Display.setCursor(120, yPos); M5.Display.print(myIndices[i].price); 
        if (myIndices[i].change.indexOf("+") != -1) M5.Display.setTextColor(RED);
        else if (myIndices[i].change.indexOf("-") != -1) M5.Display.setTextColor(BLUE);
        else M5.Display.setTextColor(BLACK);
        M5.Display.setCursor(220, yPos); M5.Display.printf("(%s%%)", myIndices[i].change.c_str());
    }
  } else {
    M5.Display.setTextColor(M5.Display.color565(0, 150, 0)); // 그린 포인트
    M5.Display.setCursor(20, 120); M5.Display.printf("STOCKS %d/2", step - 5); 
    int startIdx = (step == 6) ? 0 : 2;
    for (int i = 0; i < 2; i++) {
        int sIdx = startIdx + i; int yPos = 155 + (i * 38);
        M5.Display.setTextColor(BLACK); M5.Display.setCursor(20, yPos); M5.Display.print(myStocks[sIdx].name); 
        M5.Display.setCursor(120, yPos); M5.Display.print(myStocks[sIdx].price);
        if (myStocks[sIdx].change.indexOf("+") != -1) M5.Display.setTextColor(RED);
        else if (myStocks[sIdx].change.indexOf("-") != -1) M5.Display.setTextColor(BLUE);
        else M5.Display.setTextColor(BLACK);
        M5.Display.setCursor(220, yPos); M5.Display.printf("(%s%%)", myStocks[sIdx].change.c_str());
    }
  }
  M5.Display.endWrite();
}

void drawLumiFace(float blink) {
  M5.Display.startWrite(); M5.Display.fillScreen(BLACK);
  for (int i = -1; i <= 1; i += 2) {
    int x = centerX + (i * eyeDistance) + (int)(lookX * 20);
    int y = centerY + (int)(lookY * 15);
    if (currentEmotion == HAPPY) { M5.Display.fillCircle(x, y + 10, eyeSize / 2, WHITE); M5.Display.fillCircle(x, y + 22, eyeSize / 2 + 2, BLACK); }
    else { M5.Display.fillEllipse(x, y, eyeSize / 2, (int)((eyeSize / 2) * blink), WHITE); M5.Display.fillCircle(x - (i * 4), y, 6, BLACK); }
  }
  M5.Display.endWrite();
}

void setup() {
  M5.begin(); Serial.begin(115200);
  M5.Display.setRotation(1); M5.Display.fillScreen(BLACK);

  if (SPIFFS.begin(true)) {
      if (SPIFFS.exists("/sodalogo.png")) {
          File f = SPIFFS.open("/sodalogo.png", "r");
          if (f) { M5.Display.drawPng(&f, 0, 56); f.close(); }
      }
  }

  if (SD.begin(GPIO_NUM_4, SPI, 15000000)) {
      if (SD.exists(fontPath)) {
          File f = SD.open(fontPath, "r"); size_t fSize = f.size();
          globalFontBuffer = (uint8_t*)ps_malloc(fSize);
          if (globalFontBuffer) { f.read(globalFontBuffer, fSize); f.close(); isSdFontLoaded = M5.Display.loadFont(globalFontBuffer); }
      }
  }

  WiFi.begin(ssid, password);
  unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) delay(500);
  
  if (WiFi.status() == WL_CONNECTED) { fetchWeather(); fetchStocks(); fetchNews(); }
  else loadLocalData();
  
  delay(1000); currentMode = CLOCK_MODE; drawClock(); modeTimer = millis();
}

void loop() {
  M5.update();
  if (millis() - lastWeatherUpdate > 600000 && WiFi.status() == WL_CONNECTED) {
    fetchWeather(); fetchStocks(); fetchNews(); lastWeatherUpdate = millis();
  }
  if (millis() - modeTimer > 30000) {
    currentMode = (currentMode == FACE_MODE) ? CLOCK_MODE : FACE_MODE;
    if (currentMode == CLOCK_MODE) drawClock(); else drawLumiFace(1.0);
    modeTimer = millis();
  }
  if (currentMode == CLOCK_MODE && millis() - lastInfoUpdate > 10000) { infoIndex++; drawClock(); lastInfoUpdate = millis(); }
  if (currentMode == FACE_MODE && millis() > lastBlink) { drawLumiFace(0.1); delay(150); drawLumiFace(1.0); lastBlink = millis() + random(5000, 10000); }
  yield();
}
