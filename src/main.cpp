#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <M5Unified.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <SD.h>
#include <JPEGDEC.h>
#include <AnimatedGIF.h>

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
unsigned long lastBlink = 0, lastExpressionChange = 0;

enum Emotion { NEUTRAL, SLEEPY, BORED, SURPRISED, HAPPY, WINK, SAD, ANGRY, LOVE };
Emotion currentEmotion = NEUTRAL;
enum DisplayMode { FACE_MODE, CLOCK_MODE, VIDEO_MODE, GIF_MODE };
DisplayMode currentMode = CLOCK_MODE;

bool isSdFontLoaded = false;
uint8_t* globalFontBuffer = nullptr;
M5Canvas canvas(&M5.Display); // 더블 버퍼링을 위한 캔버스
JPEGDEC jpeg;
File mjpegFile;
AnimatedGIF gif;
File gifFile;

void fetchNews(); void fetchStocks(); void fetchWeather(); void drawClock();

// MJPEG 콜백 함수들
int drawJPEG(JPEGDRAW *pDraw) {
  canvas.pushImage(pDraw->x, pDraw->y, pDraw->iWidth, pDraw->iHeight, pDraw->pPixels);
  return 1;
}

// GIF Draw Callback
void GIFDraw(GIFDRAW *pDraw) {
    uint8_t *s;
    uint16_t *d, *usPalette, usTemp[320];
    int x, y, iWidth;

    iWidth = pDraw->iWidth;
    if (iWidth > 320) iWidth = 320;

    usPalette = pDraw->pPalette;
    y = pDraw->iY + pDraw->y; // Current scanline

    s = pDraw->pPixels;
    d = usTemp;
    for (x = 0; x < iWidth; x++) {
        uint8_t c = *s++;
        if (c != pDraw->ucTransparent) {
            *d++ = usPalette[c];
        } else {
            *d++ = 0x0001; // Transparent pixel placeholder
        }
    }
    M5.Display.pushImage(pDraw->iX, y, iWidth, 1, usTemp, 0x0001);
}

// GIF Callback functions for SD card access
void * GIFOpen(const char *szFilename, int32_t *pSize) {
    gifFile = SD.open(szFilename, "r");
    if (gifFile) {
        *pSize = gifFile.size();
        return (void *)&gifFile;
    }
    return NULL;
}
void GIFClose(void *pHandle) {
    File *f = (File *)pHandle;
    if (f) f->close();
}
int32_t GIFRead(GIFFILE *pFile, uint8_t *pBuf, int32_t iLen) {
    File *f = (File *)pFile->fHandle;
    return f->read(pBuf, iLen);
}
int32_t GIFSeek(GIFFILE *pFile, int32_t iPosition) {
    File *f = (File *)pFile->fHandle;
    f->seek(iPosition);
    return iPosition;
}

void playGif() {
    if (!SD.exists("/f2.gif")) {
        Serial.println("Error: f2.gif not found on SD card!");
        currentMode = CLOCK_MODE;
        drawClock();
        return;
    }

    gif.begin(LITTLE_ENDIAN_PIXELS);

    while (currentMode == GIF_MODE) {
        if (gif.open("/f2.gif", GIFOpen, GIFClose, GIFRead, GIFSeek, GIFDraw)) {
            while (gif.playFrame(true, NULL)) {
                M5.update();
                if (M5.Touch.getDetail().wasPressed()) {
                    currentMode = CLOCK_MODE;
                    gif.close();
                    drawClock();
                    return;
                }
            }
            gif.close();
        } else {
            currentMode = CLOCK_MODE;
            break;
        }
    }
    drawClock();
}

void playMjpeg() {
  if (!mjpegFile) {
    mjpegFile = SPIFFS.open("/flow.mjpeg", "r");
    if (!mjpegFile) return;
  }

  if (jpeg.openRAM((uint8_t*)nullptr, 0, drawJPEG)) { // 파일 핸들러 모드로 설정 (간접적으로 open 함수 사용)
    // JPEGDEC의 open 함수는 사실 직접 파일 핸들링을 제공하지 않으므로, 
    // 파일에서 청크를 읽어 openRAM하거나, 더 간단한 라이브러리를 사용하기도 하지만 
    // 여기서는 M5GFX의 drawJpgFile을 활용하는 방식으로 수정하여 재생하겠습니다.
  }
}

// MJPEG 재생 최적화 버전 (PSRAM 활용)
void streamMjpeg() {
    const char* filename = "/f2.mjpeg";
    if (!SD.exists(filename)) {
        Serial.printf("Error: %s not found on SD card!\n", filename);
        currentMode = CLOCK_MODE;
        drawClock();
        return;
    }
    
    File f = SD.open(filename, "r");
    if (!f) return;
    size_t fileSize = f.size();

    // PSRAM에 동영상 파일 전체 로드
    uint8_t* videoBuf = (uint8_t*)ps_malloc(fileSize);
    if (!videoBuf) {
        Serial.println("Error: Failed to allocate PSRAM for video!");
        f.close();
        currentMode = CLOCK_MODE;
        drawClock();
        return;
    }

    Serial.println("Loading video to PSRAM...");
    f.read(videoBuf, fileSize);
    f.close();
    Serial.println("Video loaded!");

    size_t pos = 0;
    while(currentMode == VIDEO_MODE) {
        M5.update();
        auto touch = M5.Touch.getDetail();
        if (touch.wasPressed()) {
            currentMode = CLOCK_MODE;
            break;
        }
        
        // JPEG 프레임 시작 찾기 (0xFF 0xD8)
        if (pos + 1 >= fileSize) pos = 0;
        while (pos + 1 < fileSize && !(videoBuf[pos] == 0xFF && videoBuf[pos+1] == 0xD8)) {
            pos++;
        }
        size_t startPos = pos;
        
        // JPEG 프레임 끝 찾기 (0xFF 0xD9)
        pos += 2;
        while (pos + 1 < fileSize && !(videoBuf[pos] == 0xFF && videoBuf[pos+1] == 0xD9)) {
            pos++;
        }
        size_t endPos = pos + 2;

        if (endPos <= fileSize) {
            // 메모리 버퍼에서 직접 그리기
            M5.Display.drawJpg(videoBuf + startPos, endPos - startPos, 0, 0);
            pos = endPos;
        } else {
            pos = 0;
        }
        
        // 프레임 속도 조절 (너무 빠르면 delay 추가 가능)
        yield();
    }
    
    free(videoBuf); // 사용 후 메모리 해제
    drawClock();
}

bool isMarketOpen() {
  auto d = M5.Rtc.getDate(); auto t = M5.Rtc.getTime();
  if (d.weekDay == 0 || d.weekDay == 6) return false;
  if (t.hours >= 9 && t.hours < 16) return true;
  if (t.hours >= 22 || t.hours < 5) {
    if (t.hours == 22 && t.minutes < 30) return false;
    return true;
  }
  return false;
}

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
    
    if (doc.containsKey("dt")) {
      time_t localTime = doc["dt"].as<long>() + doc["timezone"].as<int>();
      struct tm *tm_info = gmtime(&localTime);
      M5.Rtc.setDateTime(tm_info);
    }
    if (currentMode == CLOCK_MODE) drawClock();
  }
  http.end();
}

void drawClock() {
  auto d = M5.Rtc.getDate(); auto t = M5.Rtc.getTime();
  const char *daysKR[] = {"일요일", "월요일", "화요일", "수요일", "목요일", "금요일", "토요일"};
  
  canvas.fillScreen(WHITE);
  canvas.fillRect(0, 0, 320, 35, canvas.color565(240, 240, 240));
  canvas.drawFastHLine(0, 35, 320, canvas.color565(200, 200, 200));
  
  if (isSdFontLoaded && globalFontBuffer) canvas.loadFont(globalFontBuffer);
  else canvas.setFont(&fonts::efontKR_14);
  
  canvas.setTextColor(BLACK); char topStr[120];
  sprintf(topStr, "%04d.%02d.%02d %s | %s %.1fC", d.year, d.month, d.date, daysKR[d.weekDay % 7], weatherKR.c_str(), currentTemp);
  canvas.drawCenterString(topStr, 160, -10);
  
  canvas.setFont(&fonts::FreeSansBold24pt7b); canvas.setTextColor(canvas.color565(0, 80, 180)); 
  char timeStr[10]; sprintf(timeStr, "%02d:%02d", t.hours, t.minutes); canvas.drawCenterString(timeStr, 160, 52);
  
  canvas.drawFastHLine(15, 235, 290, canvas.color565(180, 180, 180));
  
  if (isSdFontLoaded && globalFontBuffer) canvas.loadFont(globalFontBuffer);
  else canvas.setFont(&fonts::efontKR_24);
  
  int totalSteps = 8; 
  int step = infoIndex % totalSteps;
  
  if (!isMarketOpen() && (infoIndex / totalSteps) % 4 != 0) {
    if (step >= 5) step = (step - 5) % 5;
  }
  
  if (step < 5) {
    int curY = 83;
    for (int n = 0; n < 2; n++) {
        int newsIdx = step * 2 + n; String newsStr = myNews[newsIdx];
        canvas.setTextColor(canvas.color565(255, 120, 0)); canvas.setCursor(20, curY); canvas.printf("%d.", n + 1);
        canvas.setTextColor(BLACK); canvas.setTextWrap(false);
        int startIdx = 0; int xOff = 45;
        for (int i = 1; i <= newsStr.length(); i++) {
            if (i < newsStr.length() && (newsStr[i] & 0xC0) == 0x80) continue;
            if (canvas.textWidth(newsStr.substring(startIdx, i)) > (300 - xOff)) {
                int endIdx = i - 1; while (endIdx > startIdx && (newsStr[endIdx] & 0xC0) == 0x80) endIdx--;
                canvas.setCursor(xOff, curY); canvas.print(newsStr.substring(startIdx, endIdx));
                curY += 25; startIdx = endIdx; i = startIdx; xOff = 20;
            }
        }
        canvas.setCursor(xOff, curY); canvas.print(newsStr.substring(startIdx));
        curY += 32;
    }
  } else if (step == 5) {
    for (int i = 0; i < 2; i++) {
        int yPos = 95 + (i * 45); canvas.setTextColor(canvas.color565(0, 80, 180)); canvas.setCursor(20, yPos); canvas.print(myIndices[i].name); 
        canvas.setTextColor(BLACK); canvas.setCursor(120, yPos); canvas.print(myIndices[i].price); 
        if (myIndices[i].change.indexOf("+") != -1) canvas.setTextColor(RED); else if (myIndices[i].change.indexOf("-") != -1) canvas.setTextColor(BLUE); else canvas.setTextColor(BLACK);
        canvas.setCursor(220, yPos); canvas.printf("(%s%%)", myIndices[i].change.c_str());
    }
  } else {
    int startIdx = (step == 6) ? 0 : 3;
    for (int i = 0; i < 3; i++) {
        int sIdx = startIdx + i; int yPos = 90 + (i * 36); 
        canvas.setTextColor(canvas.color565(0, 80, 180)); canvas.setCursor(20, yPos); canvas.print(myStocks[sIdx].name); 
        canvas.setTextColor(BLACK); canvas.setCursor(110, yPos); canvas.print(myStocks[sIdx].price);
        if (myStocks[sIdx].change.indexOf("+") != -1) canvas.setTextColor(RED); 
        else if (myStocks[sIdx].change.indexOf("-") != -1) canvas.setTextColor(BLUE); 
        else canvas.setTextColor(BLACK);
        canvas.setCursor(205, yPos); canvas.printf("(%s%%)", myStocks[sIdx].change.c_str());
    }
  }
  canvas.pushSprite(0, 0);
}

void drawLumiFace(float blink) {
  canvas.fillScreen(BLACK);
  int ew = eyeSize, eh = (int)(eyeSize * blink);
  uint16_t eyeColor = WHITE;
  if (currentEmotion == LOVE) eyeColor = canvas.color565(255, 100, 150);
  
  for (int i = -1; i <= 1; i += 2) {
    int x = centerX + (i * eyeDistance) + (int)(lookX * 15);
    int y = centerY + (int)(lookY * 10);
    
    switch (currentEmotion) {
      case HAPPY:
        canvas.fillSmoothCircle(x, y, ew/2, eyeColor);
        canvas.fillSmoothCircle(x, y + 10, ew/2, BLACK);
        break;
      case SLEEPY:
        canvas.fillSmoothRoundRect(x - ew/2, y - 5, ew, 10, 5, eyeColor);
        break;
      case SURPRISED:
        canvas.fillSmoothCircle(x, y, ew/2 + 5, eyeColor);
        canvas.fillSmoothCircle(x, y, ew/2, BLACK);
        canvas.fillSmoothCircle(x, y, ew/2 - 5, eyeColor);
        break;
      case ANGRY:
        canvas.fillSmoothCircle(x, y, ew/2, eyeColor);
        if (i == -1) canvas.fillTriangle(x-ew, y-ew, x+ew, y-ew, x+ew, y-10, BLACK);
        else canvas.fillTriangle(x-ew, y-ew, x+ew, y-ew, x-ew, y-10, BLACK);
        break;
      case SAD:
        canvas.fillSmoothCircle(x, y, ew/2, eyeColor);
        canvas.fillSmoothRoundRect(x-ew/2, y-ew/2, ew, ew/2+5, 2, BLACK);
        break;
      case WINK:
        if (i == -1) canvas.fillSmoothCircle(x, y, ew/2, eyeColor);
        else canvas.fillSmoothRoundRect(x - ew/2, y - 5, ew, 10, 5, eyeColor);
        break;
      case LOVE:
        canvas.fillSmoothCircle(x-10, y-10, 15, eyeColor);
        canvas.fillSmoothCircle(x+10, y-10, 15, eyeColor);
        canvas.fillTriangle(x-25, y-5, x+25, y-5, x, y+25, eyeColor);
        break;
      default:
        if (blink > 0.1) {
          canvas.fillSmoothCircle(x, y, ew/2, eyeColor);
          canvas.fillSmoothCircle(x + (lookX*5), y + (lookY*5), ew/4, BLACK);
        } else {
          canvas.fillSmoothRoundRect(x - ew/2, y - 2, ew, 4, 2, eyeColor);
        }
        break;
    }
  }
  canvas.pushSprite(0, 0);
}

void setup() {
  M5.begin(); Serial.begin(115200); M5.Display.setRotation(1); M5.Display.fillScreen(BLACK);
  canvas.createSprite(320, 240); // 캔버스 생성
  if (SPIFFS.begin(true)) { if (SPIFFS.exists("/sodalogo.png")) { File f = SPIFFS.open("/sodalogo.png", "r"); if (f) { M5.Display.drawPng(&f, 0, 56); f.close(); } } }
  if (SD.begin(GPIO_NUM_4, SPI, 15000000)) { if (SD.exists(fontPath)) { File f = SD.open(fontPath, "r"); size_t fSize = f.size(); globalFontBuffer = (uint8_t*)ps_malloc(fSize); if (globalFontBuffer) { f.read(globalFontBuffer, fSize); f.close(); isSdFontLoaded = M5.Display.loadFont(globalFontBuffer); } } }
  WiFi.begin(ssid, password); unsigned long wifiStart = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiStart < 15000) delay(500);
  if (WiFi.status() == WL_CONNECTED) { fetchWeather(); fetchStocks(); fetchNews(); } else loadLocalData();
  delay(1000); 
  
  // MJPEG 모드로 즉시 시작 (f2.mjpeg)
  currentMode = VIDEO_MODE; 
  streamMjpeg(); 
}

void loop() {
  M5.update();
  auto touch = M5.Touch.getDetail();
  
  // 터치가 안될 경우를 대비해 각 모드 함수 재진입 로직 추가
  if (currentMode == GIF_MODE) {
    playGif();
  } else if (currentMode == VIDEO_MODE) {
    streamMjpeg();
  }
  
  if (touch.wasPressed()) {
    if (currentMode == CLOCK_MODE) {
      currentMode = FACE_MODE;
      currentEmotion = (Emotion)random(0, 9);
      drawLumiFace(1.0);
    } else if (currentMode == FACE_MODE) {
      currentMode = VIDEO_MODE;
      streamMjpeg();
    } else if (currentMode == VIDEO_MODE) {
      currentMode = GIF_MODE;
      playGif();
    } else {
      currentMode = CLOCK_MODE;
      drawClock();
    }
  }

  unsigned long now = millis();
  if (currentMode == CLOCK_MODE) {
    if (now - lastInfoUpdate > 8000) { infoIndex++; drawClock(); lastInfoUpdate = now; }
    if (now - lastWeatherUpdate > 1800000 && WiFi.status() == WL_CONNECTED) { fetchWeather(); fetchStocks(); fetchNews(); lastWeatherUpdate = now; }
  } else if (currentMode == FACE_MODE) {
    if (now - lastExpressionChange > 4000) {
      currentEmotion = (Emotion)random(0, 9);
      lookX = random(-10, 11) / 10.0; lookY = random(-10, 11) / 10.0;
      lastExpressionChange = now;
    }
    if (now - lastBlink > 5000) {
      for (float b = 1.0; b >= 0.0; b -= 0.2) { drawLumiFace(b); }
      for (float b = 0.0; b <= 1.0; b += 0.2) { drawLumiFace(b); }
      lastBlink = now + random(2000, 6000);
    } else {
      drawLumiFace(1.0);
    }
  }
  delay(30);
}
