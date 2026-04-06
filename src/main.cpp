#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <M5Unified.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <SD.h>

// --- 설정 ---
const char *ssid = "SODATA02AD";
const char *password = "EEB75002AC";
const char *apiKey = "2adbcdbbad353a31d8a2437622de087b";
const char *city = "Seoul,kr";
const char *fontPath = "/NanumBarunGothic-18.vlw"; 

struct Stock { String name; String price; String change; };
Stock myStocks[6]; Stock myIndices[2]; String myNews[10];  
int infoIndex = 0; unsigned long lastInfoUpdate = 0;
String weatherKR = "확인중"; float currentTemp = 0.0;
bool isWifiConnected = false; unsigned long lastWeatherUpdate = 0;

int centerX = 160, centerY = 120, eyeDistance = 70, eyeSize = 60;
float lookX = 0, lookY = 0;
unsigned long lastBlink = 0, lastExpressionChange = 0, touchTimer = 0, modeTimer = 0;

enum Emotion { NEUTRAL, SLEEPY, BORED, SURPRISED, HAPPY, WINK, SAD, ANGRY, LOVE };
Emotion currentEmotion = NEUTRAL;
enum DisplayMode { FACE_MODE, CLOCK_MODE };
DisplayMode currentMode = CLOCK_MODE;

bool isSdFontLoaded = false;
uint8_t* globalFontBuffer = nullptr;

void fetchNews(); void fetchStocks(); void fetchWeather(); void drawClock();

void loadLocalData() {
  if (!SPIFFS.begin(true)) return;
  File file = SPIFFS.open("/data.json", "r");
  if (!file) return;
  DynamicJsonDocument doc(3072);
  deserializeJson(doc, file); file.close();
  for (int i = 0; i < 2; i++) { myIndices[i].name = doc["indices"][i]["name"].as<String>(); myIndices[i].price = doc["indices"][i]["price"].as<String>(); myIndices[i].change = doc["indices"][i]["change"].as<String>(); }
  for (int i = 0; i < 6; i++) { myStocks[i].name = doc["stocks"][i]["name"].as<String>(); myStocks[i].price = doc["stocks"][i]["price"].as<String>(); myStocks[i].change = doc["stocks"][i]["change"].as<String>(); }
  for (int i = 0; i < 10; i++) { myNews[i] = doc["news"][i].as<String>(); }
}

void fetchStocks() {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  const char* symbols[] = {"^KS11", "^KQ11", "005930.KS", "000660.KS", "042700.KS", "161890.KS", "085660.KQ", "AAPL"};
  const char* names[] = {"코스피", "코스닥", "삼성전자", "SK하이닉스", "한미반도체", "한국콜마", "차바이오텍", "애플"};
  for (int i = 0; i < 8; i++) {
    String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + String(symbols[i]) + "?interval=1d&range=2d";
    http.begin(client, url); http.addHeader("User-Agent", "Mozilla/5.0");
    if (http.GET() == 200) {
      DynamicJsonDocument doc(4096); deserializeJson(doc, http.getString());
      if (doc.containsKey("chart")) {
          JsonObject result = doc["chart"]["result"][0]["meta"];
          float current = result["regularMarketPrice"];
          float prev = result.containsKey("previousClose") ? result["previousClose"].as<float>() : (result.containsKey("chartPreviousClose") ? result["chartPreviousClose"].as<float>() : 0.0);
          if (current == 0) current = prev;
          float change = (prev != 0) ? ((current - prev) / prev) * 100.0 : 0.0;
          String priceStr = (current >= 1000) ? String((long)current) : String(current, 2);
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
  WiFiClientSecure client; client.setInsecure();
  HTTPClient http;
  http.begin(client, "https://www.yonhapnewstv.co.kr/browse/feed/");
  http.addHeader("User-Agent", "Mozilla/5.0");
  if (http.GET() == 200) {
    String payload = http.getString();
    int startIdx = 0; int newsCount = 0;
    for (int i = 0; i < 10; i++) {
      int itemIdx = payload.indexOf("<item>", startIdx);
      if (itemIdx == -1) break;
      int titleStart = payload.indexOf("<title>", itemIdx);
      if (titleStart == -1) break;
      titleStart += 7;
      int titleEnd = payload.indexOf("</title>", titleStart);
      if (titleEnd == -1) break;
      String title = payload.substring(titleStart, titleEnd);
      title.replace("<![CDATA[", ""); title.replace("]]>", "");
      title.replace("“", "\""); title.replace("”", "\""); title.replace("…", "..."); 
      title.replace("中", "중"); title.replace("日", "일"); title.replace("美", "미");
      title.trim();
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
  http.begin("http://api.openweathermap.org/data/2.5/weather?q=" + String(city) + "&appid=" + String(apiKey) + "&units=metric");
  if (http.GET() == 200) {
    DynamicJsonDocument doc(1024); deserializeJson(doc, http.getString());
    currentTemp = doc["main"]["temp"];
    String w = doc["weather"][0]["main"].as<String>();
    if (w == "Clear") weatherKR = "맑음"; else if (w == "Clouds") weatherKR = "흐림"; else if (w == "Rain") weatherKR = "비"; else weatherKR = w;
    if (currentMode == CLOCK_MODE) drawClock();
  }
  http.end();
}

void drawClock() {
  auto d = M5.Rtc.getDate(); auto t = M5.Rtc.getTime();
  const char *daysKR[] = {"일요일", "월요일", "화요일", "수요일", "목요일", "금요일", "토요일"};
  M5.Display.startWrite(); M5.Display.fillScreen(WHITE);
  M5.Display.fillRect(0, 0, 320, 35, M5.Display.color565(240, 240, 240));
  M5.Display.drawFastHLine(0, 35, 320, M5.Display.color565(200, 200, 200));
  if (isSdFontLoaded && globalFontBuffer) M5.Display.loadFont(globalFontBuffer);
  else M5.Display.setFont(&fonts::efontKR_14);
  M5.Display.setTextColor(BLACK); char topStr[120];
  sprintf(topStr, "%04d.%02d.%02d %s | %s %.1fC", d.year, d.month, d.date, daysKR[d.weekDay % 7], weatherKR.c_str(), currentTemp);
  M5.Display.drawCenterString(topStr, 160, -10);
  M5.Display.setFont(&fonts::FreeSansBold24pt7b); M5.Display.setTextColor(M5.Display.color565(0, 80, 180)); 
  char timeStr[10]; sprintf(timeStr, "%02d:%02d", t.hours, t.minutes); M5.Display.drawCenterString(timeStr, 160, 52);
  M5.Display.drawFastHLine(15, 95, 290, M5.Display.color565(180, 180, 180)); M5.Display.drawFastHLine(15, 235, 290, M5.Display.color565(180, 180, 180));
  if (isSdFontLoaded && globalFontBuffer) M5.Display.loadFont(globalFontBuffer);
  else M5.Display.setFont(&fonts::efontKR_24);
  int totalSteps = 8; int step = infoIndex % totalSteps;
  if (step < 5) {
    int curY = 105;
    for (int n = 0; n < 2; n++) {
        int newsIdx = step * 2 + n; String newsStr = myNews[newsIdx];
        M5.Display.setTextColor(M5.Display.color565(255, 120, 0)); M5.Display.setCursor(20, curY); M5.Display.printf("%d.", n + 1);
        M5.Display.setTextColor(BLACK); M5.Display.setTextWrap(false);
        int startIdx = 0; int xOff = 45;
        for (int i = 1; i <= newsStr.length(); i++) {
            if (i < newsStr.length() && (newsStr[i] & 0xC0) == 0x80) continue;
            if (M5.Display.textWidth(newsStr.substring(startIdx, i)) > (300 - xOff)) {
                int endIdx = i - 1; while (endIdx > startIdx && (newsStr[endIdx] & 0xC0) == 0x80) endIdx--;
                M5.Display.setCursor(xOff, curY); M5.Display.print(newsStr.substring(startIdx, endIdx));
                curY += 21; startIdx = endIdx; i = startIdx; xOff = 20;
            }
        }
        M5.Display.setCursor(xOff, curY); M5.Display.print(newsStr.substring(startIdx));
        curY += 32; // 뉴스 기사 간 간격 확대 (26 -> 32)
        if (n == 0) M5.Display.drawFastHLine(40, curY - 16, 240, M5.Display.color565(230, 230, 230));
    }
    M5.Display.setTextWrap(true);
  } else if (step == 5) {
    for (int i = 0; i < 2; i++) {
        int yPos = 110 + (i * 45); M5.Display.setTextColor(M5.Display.color565(0, 80, 180)); M5.Display.setCursor(20, yPos); M5.Display.print(myIndices[i].name); 
        M5.Display.setTextColor(BLACK); M5.Display.setCursor(120, yPos); M5.Display.print(myIndices[i].price); 
        if (myIndices[i].change.indexOf("+") != -1) M5.Display.setTextColor(RED); else if (myIndices[i].change.indexOf("-") != -1) M5.Display.setTextColor(BLUE); else M5.Display.setTextColor(BLACK);
        M5.Display.setCursor(220, yPos); M5.Display.printf("(%s%%)", myIndices[i].change.c_str());
    }
  } else {
    int startIdx = (step == 6) ? 0 : 3;
    for (int i = 0; i < 3; i++) {
        int sIdx = startIdx + i; int yPos = 105 + (i * 36); 
        M5.Display.setTextColor(M5.Display.color565(0, 80, 180)); M5.Display.setCursor(20, yPos); M5.Display.print(myStocks[sIdx].name); 
        M5.Display.setTextColor(BLACK); M5.Display.setCursor(110, yPos); M5.Display.print(myStocks[sIdx].price);
        if (myStocks[sIdx].change.indexOf("+") != -1) M5.Display.setTextColor(RED); 
        else if (myStocks[sIdx].change.indexOf("-") != -1) M5.Display.setTextColor(BLUE); 
        else M5.Display.setTextColor(BLACK);
        M5.Display.setCursor(205, yPos); M5.Display.printf("(%s%%)", myStocks[sIdx].change.c_str());
    }
  }
  M5.Display.endWrite();
}

void drawLumiFace(float blink) {
  M5.Display.startWrite(); M5.Display.fillScreen(BLACK);
  int ew = eyeSize, eh = (int)(eyeSize * blink);
  uint16_t eyeColor = WHITE;
  if (currentEmotion == LOVE) eyeColor = M5.Display.color565(255, 100, 150);
  
  for (int i = -1; i <= 1; i += 2) {
    int x = centerX + (i * eyeDistance) + (int)(lookX * 15);
    int y = centerY + (int)(lookY * 10);
    
    switch (currentEmotion) {
      case HAPPY:
        M5.Display.fillSmoothCircle(x, y, ew/2, eyeColor);
        M5.Display.fillSmoothCircle(x, y + 10, ew/2, BLACK);
        break;
      case SLEEPY:
        M5.Display.fillSmoothRoundRect(x - ew/2, y - 5, ew, 10, 5, eyeColor);
        break;
      case SURPRISED:
        M5.Display.fillSmoothCircle(x, y, ew/2 + 5, eyeColor);
        M5.Display.fillSmoothCircle(x, y, ew/2, BLACK);
        M5.Display.fillSmoothCircle(x, y, ew/2 - 5, eyeColor);
        break;
      case ANGRY:
        M5.Display.fillSmoothCircle(x, y, ew/2, eyeColor);
        if (i == -1) M5.Display.fillTriangle(x-ew, y-ew, x+ew, y-ew, x+ew, y-10, BLACK);
        else M5.Display.fillTriangle(x-ew, y-ew, x+ew, y-ew, x-ew, y-10, BLACK);
        break;
      case SAD:
        M5.Display.fillSmoothCircle(x, y, ew/2, eyeColor);
        M5.Display.fillSmoothRoundRect(x-ew/2, y-ew/2, ew, ew/2+5, 2, BLACK);
        break;
      case WINK:
        if (i == -1) M5.Display.fillSmoothCircle(x, y, ew/2, eyeColor);
        else M5.Display.fillSmoothRoundRect(x - ew/2, y - 5, ew, 10, 5, eyeColor);
        break;
      case LOVE:
        M5.Display.fillSmoothCircle(x-10, y-10, 15, eyeColor);
        M5.Display.fillSmoothCircle(x+10, y-10, 15, eyeColor);
        M5.Display.fillTriangle(x-25, y-5, x+25, y-5, x, y+25, eyeColor);
        break;
      default:
        M5.Display.fillSmoothCircle(x, y, ew/2, eyeColor);
        if (blink < 0.3) M5.Display.fillSmoothCircle(x, y, ew/2 + 2, BLACK);
        break;
    }
  }
  M5.Display.endWrite();
}

void setup() {
  M5.begin(); Serial.begin(115200); M5.Display.setRotation(1); M5.Display.fillScreen(BLACK);
  if (SPIFFS.begin(true)) { if (SPIFFS.exists("/sodalogo.png")) { File f = SPIFFS.open("/sodalogo.png", "r"); if (f) { M5.Display.drawPng(&f, 0, 56); f.close(); } } }
  if (SD.begin(GPIO_NUM_4, SPI, 15000000)) { if (SD.exists(fontPath)) { File f = SD.open(fontPath, "r"); size_t fSize = f.size(); globalFontBuffer = (uint8_t*)ps_malloc(fSize); if (globalFontBuffer) { f.read(globalFontBuffer, fSize); f.close(); isSdFontLoaded = M5.Display.loadFont(globalFontBuffer); } } }
  WiFi.begin(ssid, password); unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) delay(500);
  if (WiFi.status() == WL_CONNECTED) { fetchWeather(); fetchStocks(); fetchNews(); } else loadLocalData();
  delay(1000); currentMode = CLOCK_MODE; drawClock();
}

void loop() {
  M5.update();
  auto touch = M5.Touch.getDetail();
  if (touch.wasPressed()) {
    currentMode = (currentMode == CLOCK_MODE) ? FACE_MODE : CLOCK_MODE;
    M5.Display.fillScreen(currentMode == FACE_MODE ? BLACK : WHITE);
    if (currentMode == CLOCK_MODE) drawClock();
    else { currentEmotion = (Emotion)random(0, 9); drawLumiFace(1.0); }
  }

  unsigned long now = millis();
  if (currentMode == CLOCK_MODE) {
    if (now - lastInfoUpdate > 8000) { infoIndex++; drawClock(); lastInfoUpdate = now; }
    if (now - lastWeatherUpdate > 1800000 && WiFi.status() == WL_CONNECTED) { fetchWeather(); fetchStocks(); fetchNews(); lastWeatherUpdate = now; }
  } else {
    if (now - lastExpressionChange > 4000) {
      currentEmotion = (Emotion)random(0, 9);
      lookX = random(-10, 11) / 10.0; lookY = random(-10, 11) / 10.0;
      lastExpressionChange = now;
    }
    if (now - lastBlink > 5000) {
      for (float b = 1.0; b >= 0.0; b -= 0.25) { drawLumiFace(b); delay(10); }
      for (float b = 0.0; b <= 1.0; b += 0.25) { drawLumiFace(b); delay(10); }
      lastBlink = now + random(2000, 6000);
    } else {
      drawLumiFace(1.0);
    }
  }
  delay(50);
}
