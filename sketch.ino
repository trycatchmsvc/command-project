#include <Wire.h>
#include <U8g2lib.h>
#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>
#include <FS.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
ESP8266WebServer server(80);
WiFiClientSecure client;

// Настройки Telegram
String telegramToken = "";
UniversalTelegramBot *bot = nullptr;

// Переменные для меню
int currentFileIndex = 0;
bool viewingFile = false;
unsigned long buttonPressTime = 0;
const int buttonPin = D3;
const int longPressDuration = 300;

// Список файлов
String files[20];
int fileCount = 0;

void setup() {
  Serial.begin(115200);
  pinMode(buttonPin, INPUT_PULLUP);
  
  // Инициализация дисплея
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.setCursor(0, 12);
  u8g2.print("Starting...");
  u8g2.sendBuffer();

  // Инициализация файловой системы
  if (!SPIFFS.begin()) {
    u8g2.clearBuffer();
    u8g2.setCursor(0, 12);
    u8g2.print("FS Error");
    u8g2.setCursor(0, 28);
    u8g2.print("Formatting...");
    u8g2.sendBuffer();
    
    SPIFFS.format();
    if (!SPIFFS.begin()) {
      u8g2.clearBuffer();
      u8g2.setCursor(0, 12);
      u8g2.print("FS Failed");
      u8g2.setCursor(0, 28);
      u8g2.print("Restarting...");
      u8g2.sendBuffer();
      delay(2000);
      ESP.restart();
    }
  }

  updateFileList();
  startConfigMode();
}

void loop() {
  handleButton();

  if (WiFi.status() == WL_CONNECTED && bot != nullptr) {
    int numNewMessages = bot->getUpdates(bot->last_message_received + 1);
    if (numNewMessages) {
      handleNewMessages(numNewMessages);
      updateFileList();
    }
  }

  server.handleClient();
}

void handleButton() {
  static unsigned long lastPressTime = 0;
  static unsigned int clickCount = 0;
  static bool buttonPreviouslyPressed = false;

  bool buttonState = digitalRead(buttonPin) == LOW;

  if (buttonState && !buttonPreviouslyPressed) {
    unsigned long currentTime = millis();
    if (currentTime - lastPressTime > 400) {
      clickCount = 1;  // Новый клик
    } else {
      clickCount++;    // Второй клик
    }
    lastPressTime = currentTime;
    buttonPreviouslyPressed = true;
  }

  if (!buttonState && buttonPreviouslyPressed) {
    buttonPreviouslyPressed = false;
  }

  // Проверка на длинное нажатие
  if (buttonState && (millis() - lastPressTime > longPressDuration)) {
    if (viewingFile) {
      viewingFile = false;
      showFileList();
    }
    delay(300);
    clickCount = 0;
    buttonPreviouslyPressed = false;
  }

  // Обработка кликов
  if (clickCount > 0 && (millis() - lastPressTime) > 300 && !buttonState) {
    if (clickCount == 1) {
      // Одиночный клик – следующий файл
      if (!viewingFile && fileCount > 0) {
        currentFileIndex = (currentFileIndex + 1) % fileCount;
        showFileList();
      }
    } else if (clickCount == 2) {
      // Двойной клик – просмотр файла
      if (!viewingFile && fileCount > 0) {
        viewingFile = true;
        viewCurrentFile();
      }
    }
    clickCount = 0;
  }
}

void updateFileList() {
  fileCount = 0;
  Dir dir = SPIFFS.openDir("/");
  
  while (dir.next() && fileCount < 20) {
    if (dir.fileName() != "/config.txt") {
      files[fileCount++] = dir.fileName();
    }
  }
  
  if (fileCount == 0) {
    u8g2.clearBuffer();
    u8g2.setCursor(0, 12);
    u8g2.print("No files");
    u8g2.setCursor(0, 28);
    u8g2.print("Send via Telegram");
    u8g2.sendBuffer();
  } else if (!viewingFile) {
    showFileList();
  }
}

void showFileList() {
  if (fileCount == 0) {
    u8g2.clearBuffer();
    u8g2.setCursor(0, 12);
    u8g2.print("No files");
    u8g2.setCursor(0, 28);
    u8g2.print("Send via Telegram");
    u8g2.sendBuffer();
    return;
  }
  
  u8g2.clearBuffer();
  u8g2.setCursor(0, 12);
  u8g2.print("File " + String(currentFileIndex + 1) + "/" + String(fileCount));
  u8g2.setCursor(0, 28);
  
  String filename = files[currentFileIndex];
  if (filename.length() > 15) {
    filename = filename.substring(filename.length() - 15);
  }
  u8g2.print(filename);
  
  u8g2.setCursor(0, 44);
  u8g2.print("Hold to open");
  u8g2.sendBuffer();
}

void viewCurrentFile() {
  if (fileCount == 0 || currentFileIndex >= fileCount) return;
  
  File file = SPIFFS.open(files[currentFileIndex], "r");
  if (!file) {
    u8g2.clearBuffer();
    u8g2.setCursor(0, 12);
    u8g2.print("Error");
    u8g2.setCursor(0, 28);
    u8g2.print("Cannot open file");
    u8g2.sendBuffer();
    return;
  }
  
  String content = file.readString();
  file.close();
  
  u8g2.clearBuffer();
  u8g2.setCursor(0, 12);
  u8g2.print("Viewing:");
  u8g2.setCursor(0, 28);
  
  String filename = files[currentFileIndex];
  if (filename.length() > 15) {
    filename = filename.substring(filename.length() - 15);
  }
  u8g2.print(filename);
  
  u8g2.setCursor(0, 44);
  u8g2.print(content.substring(0, 40));
  u8g2.sendBuffer();
}

void startConfigMode() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP8266_AP", "yourAPPassword");
  
  u8g2.clearBuffer();
  u8g2.setCursor(0, 12);
  u8g2.print("AP: ESP8266_AP");
  u8g2.setCursor(0, 28);
  u8g2.print("IP: ");
  u8g2.print(WiFi.softAPIP());
  u8g2.sendBuffer();

  server.on("/", HTTP_GET, []() {
    String html = "<html><body><h1>ESP8266 Configuration</h1>"
                 "<form action='/save' method='POST'>"
                 "Telegram Token: <input type='text' name='token' required><br>"
                 "<input type='submit' value='Save'></form></body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_POST, []() {
    if (server.hasArg("token")) {
      telegramToken = server.arg("token");
      
      File file = SPIFFS.open("/config.txt", "w");
      if (file) {
        file.println(telegramToken);
        file.close();
      }
      
      server.send(200, "text/html", "<html><body><h1>Token Saved</h1><p>Connecting...</p></body></html>");
      delay(1000);
      connectToWiFi();
    } else {
      server.send(400, "text/plain", "Token required");
    }
  });

  server.begin();
}

void connectToWiFi() {
  server.stop();
  WiFi.softAPdisconnect(true);
  
  u8g2.clearBuffer();
  u8g2.setCursor(0, 12);
  u8g2.print("Connecting WiFi...");
  u8g2.sendBuffer();

  WiFiManager wifiManager;
  wifiManager.resetSettings();
  wifiManager.setTimeout(180);
  
  if (!wifiManager.autoConnect("ESP8266_AP", "yourAPPassword")) {
    u8g2.clearBuffer();
    u8g2.setCursor(0, 12);
    u8g2.print("Failed to connect");
    u8g2.setCursor(0, 28);
    u8g2.print("Restarting...");
    u8g2.sendBuffer();
    delay(3000);
    ESP.restart();
  }

  u8g2.clearBuffer();
  u8g2.setCursor(0, 12);
  u8g2.print("Connected!");
  u8g2.setCursor(0, 28);
  u8g2.print("IP: ");
  u8g2.print(WiFi.localIP());
  u8g2.setCursor(0, 44);
  u8g2.print("Token: ");
  u8g2.print(telegramToken.substring(0, 5) + "...");
  u8g2.sendBuffer();
  
  if (telegramToken.length() > 0) {
    client.setInsecure();
    bot = new UniversalTelegramBot(telegramToken, client);
  }

  server.on("/", HTTP_GET, []() {
    String html = "<html><body><h1>ESP8266 Status</h1>"
                 "<p>WiFi: " + WiFi.SSID() + "</p>"
                 "<p>IP: " + WiFi.localIP().toString() + "</p>"
                 "<p>Token: " + telegramToken.substring(0, 5) + "...</p></body></html>";
    server.send(200, "text/html", html);
  });
  server.begin();
  updateFileList();
}

void handleNewMessages(int numNewMessages) {
  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = String(bot->messages[i].chat_id);
    String text = bot->messages[i].text;

    if (text.startsWith("/save ")) {
      String content = text.substring(6);
      if (content.length() > 1024) {
        bot->sendMessage(chat_id, "Message too long (max 1024)", "");
        continue;
      }
      
      String filename = "/note_" + String(millis()) + ".txt";
      File file = SPIFFS.open(filename, "w");
      if (file) {
        file.print(content);
        file.close();
        bot->sendMessage(chat_id, "Saved: " + filename, "");
        updateFileList();
      } else {
        bot->sendMessage(chat_id, "Save error", "");
      }
    }
    else if (text == "/list") {
      String fileList = "Files:\n";
      Dir dir = SPIFFS.openDir("/");
      while (dir.next()) {
        if (dir.fileName() != "/config.txt") {
          fileList += dir.fileName() + "\n";
        }
      }
      bot->sendMessage(chat_id, fileList, "");
    }
    else if (text.startsWith("/delete ")) {
      String filename = text.substring(8);
      if (!filename.startsWith("/")) {
        filename = "/" + filename;
      }
      if (SPIFFS.exists(filename) && filename != "/config.txt") {
        SPIFFS.remove(filename);
        bot->sendMessage(chat_id, "Deleted: " + filename, "");
        updateFileList();
      } else {
        bot->sendMessage(chat_id, "File not found", "");
      }
    }
    else {
      String help = "Commands:\n"
                    "/save [text] - Save note\n"
                    "/list - Show files\n"
                    "/delete [filename] - Delete file";
      bot->sendMessage(chat_id, help, "");
    }
  }
}