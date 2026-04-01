#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <M5Unified.h>
#include <SPIFFS.h>
#include <WiFi.h>

// --- 설정 ---
const char *ssid = "SODATA02AD";
const char *password = "EEB75002AC";
const char *apiKey = "2adbcdbbad353a31d8a2437622de087b";
const char *city = "Seoul,kr";

// --- 데이터 구조체 및 배열 (에러 방지를 위해 명시적 선언) ---
struct Stock {
  String name;
  String price;
  String change;
};

// 전역 변수들
Stock myStocks[5]; // 배열로 수정
String myNews[5];  // 배열로 수정
int infoIndex = 0;
unsigned long lastInfoUpdate = 0;

String weatherKR = "확인중";
float currentTemp = 0.0;
bool isWifiConnected = false;
unsigned long lastWeatherUpdate = 0;

// 얼굴/감정 변수
int centerX = 160, centerY = 120, eyeDistance = 70, eyeSize = 60;
float lookX = 0, lookY = 0;
unsigned long lastBlink = 0, lastExpressionChange = 0, touchTimer = 0,
              modeTimer = 0;
enum Emotion { NEUTRAL, SLEEPY, BORED, SURPRISED, HAPPY };
Emotion currentEmotion = NEUTRAL;
enum DisplayMode { FACE_MODE, CLOCK_MODE };
DisplayMode currentMode = FACE_MODE;

// 요일 계산기
int getDayOfWeek(int y, int m, int d) {
  if (m < 3) {
    m += 12;
    y--;
  }
  int res = (d + (13 * (m + 1) / 5) + y + (y / 4) - (y / 100) + (y / 400)) % 7;
  int korDay[] = {6, 0, 1, 2, 3, 4, 5};
  return korDay[res];
}

// JSON 로드 (에러 났던 부분 수정)
void loadLocalData() {
  if (!SPIFFS.begin(true))
    return;
  if (!SPIFFS.exists("/data.json"))
    return;

  File file = SPIFFS.open("/data.json", "r");
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, file);
  file.close();

  for (int i = 0; i < 5; i++) {
    // 안전하게 데이터 넣기 (as<String>() 사용)
    myStocks[i].name = doc["stocks"][i]["name"].as<String>();
    myStocks[i].price = doc["stocks"][i]["price"].as<String>();
    myStocks[i].change = doc["stocks"][i]["change"].as<String>();

    myNews[i] = doc["news"][i].as<String>();

    // 데이터가 비었을 때를 대비한 안전장치
    if (myStocks[i].name == "null")
      myStocks[i].name = "None";
    if (myNews[i] == "null")
      myNews[i] = "뉴스가 없습니다.";
  }
}

String translateWeather(String english) {
  if (english == "Clear")
    return "맑음";
  if (english == "Clouds")
    return "흐림";
  if (english == "Rain")
    return "비";
  if (english == "Snow")
    return "눈";
  return english;
}

void fetchWeather() {
  if (WiFi.status() != WL_CONNECTED)
    return;
  HTTPClient http;
  String url =
      "http://api.openweathermap.org/data/2.5/weather?q=" + String(city) +
      "&units=metric&appid=" + String(apiKey);
  const char *headerKeys[] = {"Date"};
  http.collectHeaders(headerKeys, 1);
  http.begin(url);
  if (http.GET() == 200) {
    String serverDate = http.header("Date");
    if (serverDate.length() > 0) {
      int h = serverDate.substring(17, 19).toInt();
      int m = serverDate.substring(20, 22).toInt();
      int s = serverDate.substring(23, 25).toInt();
      int d_ = serverDate.substring(5, 7).toInt();
      int y_ = serverDate.substring(12, 16).toInt();
      String monthStr = serverDate.substring(8, 11);
      int mon = 1;
      String months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                         "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
      for (int i = 0; i < 12; i++)
        if (monthStr == months[i])
          mon = i + 1;
      // UTC to KST (UTC+9) 변환 및 날짜 자동 계산
      struct tm t_tm;
      t_tm.tm_year = y_ - 1900;
      t_tm.tm_mon = mon - 1;
      t_tm.tm_mday = d_;
      t_tm.tm_hour = h + 9;
      t_tm.tm_min = m;
      t_tm.tm_sec = s;
      t_tm.tm_isdst = -1;
      
      time_t kst_time = mktime(&t_tm); // mktime이 날짜 넘김을 자동 처리함
      struct tm *kst_tm = localtime(&kst_time);
      
      M5.Rtc.setDateTime({{ (uint16_t)(kst_tm->tm_year + 1900), (int8_t)(kst_tm->tm_mon + 1), (int8_t)kst_tm->tm_mday}, 
                          { (int8_t)kst_tm->tm_hour, (int8_t)kst_tm->tm_min, (int8_t)kst_tm->tm_sec}});
    }
    String payload = http.getString();
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    if (doc.containsKey("main"))
      currentTemp = doc["main"]["temp"];
    int weatherIdx = payload.indexOf("\"weather\"");
    if (weatherIdx != -1) {
      int mainIdx = payload.indexOf("\"main\":\"", weatherIdx);
      if (mainIdx != -1) {
        int start = mainIdx + 8;
        int end = payload.indexOf("\"", start);
        weatherKR = translateWeather(payload.substring(start, end));
      }
    }
  }
  http.end();
}

void drawClock() {
  auto d = M5.Rtc.getDate();
  auto t = M5.Rtc.getTime();
  int realDay = getDayOfWeek(d.year, d.month, d.date);
  const char *daysKR[] = {"일요일", "월요일", "화요일", "수요일",
                          "목요일", "금요일", "토요일"};

  M5.Display.startWrite();
  M5.Display.fillScreen(BLACK);
  
  // 1. 상단 정보 바 (날짜, 요일, 날씨)
  M5.Display.fillRect(0, 0, 320, 32, M5.Display.color565(40, 40, 40));
  M5.Display.setFont(&fonts::efontKR_14);
  M5.Display.setTextColor(WHITE);
  char topStr[100];
  // 특수기호 ° 대신 C 사용 (글씨 깨짐 방지)
  sprintf(topStr, "%04d/%02d/%02d (%s)  %s %.1f C", d.year, d.month, d.date, daysKR[realDay], weatherKR.c_str(), currentTemp);
  M5.Display.drawCenterString(topStr, 160, 8);

  // 2. 중앙 시계 영역 (배율 제거로 가독성 향상)
  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  M5.Display.setTextSize(1.0); // 배율 제거하여 뚜렷하게
  M5.Display.setTextColor(0x07FF); 
  char timeStr[10];
  sprintf(timeStr, "%02d:%02d", t.hours, t.minutes);
  M5.Display.drawCenterString(timeStr, 160, 55);

  // 3. 하단 대형 정보 패널
  M5.Display.fillRect(8, 115, 304, 117, M5.Display.color565(15, 15, 40));
  M5.Display.drawRect(8, 115, 304, 117, 0x52AA); 

  int idx = infoIndex % 5;
  
  // 뉴스 섹션 타이틀 (더 큰 폰트 적용)
  M5.Display.setFont(&fonts::efontKR_24);
  M5.Display.setTextColor(0xFDA0); 
  M5.Display.setCursor(18, 122);
  M5.Display.print("NEWS"); 

  M5.Display.setFont(&fonts::efontKR_16);
  M5.Display.setTextColor(WHITE);
  M5.Display.setTextWrap(false); 
  M5.Display.setCursor(18, 152);
  M5.Display.print(myNews[idx]);
  M5.Display.setTextWrap(true);

  // 구분선
  M5.Display.drawFastHLine(15, 180, 290, 0x3186);

  // 주식 섹션 타이틀
  M5.Display.setFont(&fonts::efontKR_24);
  M5.Display.setTextColor(0x7FFF); 
  M5.Display.setCursor(18, 188);
  M5.Display.print("STOCK");

  M5.Display.setFont(&fonts::efontKR_16);
  M5.Display.setCursor(105, 194); // 타이틀 옆으로 배치하여 공간 확보
  M5.Display.setTextColor(WHITE);
  M5.Display.printf("%s: ", myStocks[idx].name.c_str());
  
  if (myStocks[idx].change.indexOf("+") != -1) M5.Display.setTextColor(RED);
  else if (myStocks[idx].change.indexOf("-") != -1) M5.Display.setTextColor(0x5DFF);
  else M5.Display.setTextColor(WHITE);
  
  M5.Display.printf("%s (%s%%)", myStocks[idx].price.c_str(), myStocks[idx].change.c_str());

  M5.Display.endWrite();
}

// --- 얼굴 그리기 및 나머지 루프 로직은 이전과 동일 (공간상 요약) ---
void drawLumiFace(float blink = 1.0) {
  M5.Display.startWrite();
  M5.Display.fillScreen(BLACK);
  for (int i = -1; i <= 1; i += 2) {
    int x = centerX + (i * eyeDistance) + (int)(lookX * 20);
    int y = centerY + (int)(lookY * 15);
    if (currentEmotion == HAPPY) {
      M5.Display.fillCircle(x, y + 10, eyeSize / 2, WHITE);
      M5.Display.fillCircle(x, y + 22, eyeSize / 2 + 2, BLACK);
    } else {
      M5.Display.fillEllipse(x, y, eyeSize / 2, (int)((eyeSize / 2) * blink),
                             WHITE);
      M5.Display.fillCircle(x - (i * 4), y, 6, BLACK);
    }
  }
  M5.Display.endWrite();
}

void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(100);
  WiFi.begin(ssid, password);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 10000)
    delay(500);
  isWifiConnected = (WiFi.status() == WL_CONNECTED);
  fetchWeather();
  loadLocalData();
  modeTimer = millis();
}

void loop() {
  M5.update();
  if (millis() - lastWeatherUpdate > 600000 && isWifiConnected) {
    fetchWeather();
    lastWeatherUpdate = millis();
  }
  if (millis() - modeTimer > 30000) { // 10초 -> 30초로 연장
    currentMode = (currentMode == FACE_MODE) ? CLOCK_MODE : FACE_MODE;
    if (currentMode == CLOCK_MODE)
      drawClock();
    else
      drawLumiFace(1.0);
    modeTimer = millis();
  }
  if (currentMode == CLOCK_MODE && millis() - lastInfoUpdate > 10000) { // 5초 -> 10초로 연장
    infoIndex++;
    drawClock();
    lastInfoUpdate = millis();
  }
  if (currentMode == FACE_MODE && millis() > lastBlink) {
    drawLumiFace(0.1);
    delay(150);
    drawLumiFace(1.0);
    lastBlink = millis() + random(5000, 10000); // 눈 깜빡임 간격도 조금 더 여유있게
  }
  yield();
}