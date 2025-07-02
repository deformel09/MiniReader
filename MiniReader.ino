#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <FS.h>

// Инициализация дисплея
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE, /* clock=*/ 4, /* data=*/ 5);

// Настройки WiFi
const char* ssid = "your_wifi";       // ИЗМЕНИТЕ НА СВОИ ДАННЫЕ
const char* password = "your_wifi_password"; // ИЗМЕНИТЕ НА СВОИ ДАННЫЕ

// Инициализация веб-сервера
ESP8266WebServer server(80);

// Пины кнопок
const int BUTTON1_PIN = 12;  // D6 - прокрутка вверх
const int BUTTON2_PIN = 13;  // D7 - прокрутка вниз 

// Переменные для текста
String currentText = "";
String currentFileName = "";
int currentLine = 0;
const int linesPerScreen = 6;  // Количество строк на экране для крупного шрифта
const int maxCharsPerLine = 42; // Максимальное количество символов в строке

// Функция для форматирования текста с переносом длинных строк
String formatText(String text) {
  String result = "";
  int pos = 0;
  
  while (pos < text.length()) {
    int endPos = text.indexOf('\n', pos);
    if (endPos == -1) {
      endPos = text.length();
    }
    
    String line = text.substring(pos, endPos);
    
    // Если строка длиннее максимальной длины, разбиваем ее
    while (line.length() > maxCharsPerLine) {
      // Ищем последний пробел в пределах максимальной длины
      int spacePos = line.lastIndexOf(' ', maxCharsPerLine);
      if (spacePos == -1 || spacePos == 0) {
        // Если пробел не найден, просто обрезаем по максимальной длине
        result += line.substring(0, maxCharsPerLine) + '\n';
        line = line.substring(maxCharsPerLine);
      } else {
        // Разбиваем по пробелу
        result += line.substring(0, spacePos) + '\n';
        line = line.substring(spacePos + 1);
      }
    }
    
    // Добавляем оставшуюся часть строки
    result += line;
    
    // Добавляем перенос строки, если это не конец текста
    if (endPos < text.length()) {
      result += '\n';
    }
    
    pos = endPos + 1;
  }
  
  return result;
}

// Функция для подсчета строк в форматированном тексте
int countFormattedLines(String text) {
  String formattedText = formatText(text);
  int count = 1;
  int pos = 0;
  
  while ((pos = formattedText.indexOf('\n', pos)) != -1) {
    count++;
    pos++;
  }
  
  return count;
}
void setup() {
  Serial.begin(115200);
  Serial.println("Starting...");
  
  // Настройка кнопок
  pinMode(BUTTON1_PIN, INPUT_PULLUP);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);
  
  // Инициализация I2C
  Wire.begin(5, 4); // SDA, SCL
  
  // Инициализация дисплея
  u8g2.begin();
  u8g2.enableUTF8Print();  // Включаем поддержку UTF-8
  
  u8g2.setFont(u8g2_font_5x8_t_cyrillic);  // 4x6 пикселей // Шрифт с поддержкой кириллицы
  
  // Отображение начального сообщения
  u8g2.clearBuffer();
  u8g2.setCursor(0, 13);
  u8g2.print("Инициализация...");
  u8g2.sendBuffer();
  
  // Инициализация файловой системы
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS initialization failed");
    u8g2.clearBuffer();
    u8g2.setCursor(0, 13);
    u8g2.print("Ошибка SPIFFS!");
    u8g2.sendBuffer();
    return;
  }
  
  // Подключение к WiFi
  WiFi.begin(ssid, password);
  
  u8g2.clearBuffer();
  u8g2.setCursor(0, 13);
  u8g2.print("Подключение WiFi");
  u8g2.sendBuffer();
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    u8g2.setCursor((attempts % 10) * 6, 26);
    u8g2.print(".");
    u8g2.sendBuffer();
    attempts++;
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection failed");
    u8g2.clearBuffer();
    u8g2.setCursor(0, 13);
    u8g2.print("Ошибка WiFi!");
    u8g2.sendBuffer();
    return;
  }
  
  Serial.println("");
  Serial.print("Connected to WiFi. IP address: ");
  Serial.println(WiFi.localIP());
  
  // Настройка маршрутов веб-сервера
  server.on("/", HTTP_GET, handleRoot);
  server.on("/upload", HTTP_POST, []() {
    server.send(200, "text/html", "<meta charset='utf-8'><h1>Файл загружен</h1><p><a href='/list'>Просмотр файлов</a></p>");
  }, handleFileUpload);
  server.on("/list", HTTP_GET, handleFileList);
  server.on("/view", HTTP_GET, handleFileView);
  server.onNotFound(handleNotFound);
  
  // Запуск сервера
  server.begin();
  Serial.println("HTTP server started");
  
  // Отображение IP-адреса на дисплее
  u8g2.clearBuffer();
  u8g2.setCursor(0, 13);
  u8g2.print("Сервер запущен!");
  u8g2.setCursor(0, 30);
  u8g2.print("IP адрес:");
  u8g2.setCursor(0, 45);
  u8g2.print(WiFi.localIP().toString().c_str());
  u8g2.sendBuffer();
}

unsigned long lastButtonPress = 0;
int scrollSpeed = 1; // Начальная скорость прокрутки (строк за нажатие)
const int maxScrollSpeed = 5; // Максимальная скорость прокрутки
void loop() {
  server.handleClient();
  
  unsigned long currentTime = millis();
  
  // Обработка кнопок для прокрутки текста
  if (digitalRead(BUTTON1_PIN) == LOW && currentText.length() > 0) {
    // Прокрутка вверх
    if (currentLine > 0) {
      // Если кнопка удерживается, увеличиваем скорость прокрутки
      if (currentTime - lastButtonPress > 1000) { // После 1 секунды удержания
        scrollSpeed = min(maxScrollSpeed, scrollSpeed + 1);
      }
      
      currentLine = max(0, currentLine - scrollSpeed);
      displayText();
      lastButtonPress = currentTime;
      delay(100);
    }
  }
  else if (digitalRead(BUTTON2_PIN) == LOW && currentText.length() > 0) {
    // Прокрутка вниз
    int totalLines = countFormattedLines(currentText);
    if (currentLine < totalLines - linesPerScreen) {
      // Если кнопка удерживается, увеличиваем скорость прокрутки
      if (currentTime - lastButtonPress > 1000) { // После 1 секунды удержания
        scrollSpeed = min(maxScrollSpeed, scrollSpeed + 1);
      }
      
      currentLine = min(totalLines - linesPerScreen, currentLine + scrollSpeed);
      displayText();
      lastButtonPress = currentTime;
      delay(100);
    }
  }
  else {
    // Если кнопки не нажаты, сбрасываем скорость прокрутки
    scrollSpeed = 1;
  }
}
void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Файловый менеджер ESP8266</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 15px; line-height: 1.4; color: #333; }";
  html += "h1 { font-size: 1.8em; margin: 0 0 20px 0; color: #0066cc; }";
  html += ".card { background-color: #fff; border-radius: 8px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); padding: 20px; margin-bottom: 20px; }";
  html += ".menu { list-style-type: none; padding: 0; margin: 0; }";
  html += ".menu li { margin-bottom: 10px; }";
  html += ".menu a { color: #0066cc; text-decoration: none; display: block; padding: 12px 15px; background-color: #f5f5f5; border-radius: 6px; transition: all 0.2s ease; }";
  html += ".menu a:hover { background-color: #e0e0e0; transform: translateY(-2px); }";
  html += ".status { margin-top: 20px; font-size: 0.9em; color: #666; }";
  html += ".status p { margin: 5px 0; }";
  html += ".footer { margin-top: 30px; font-size: 0.8em; color: #888; text-align: center; }";
  html += "@media (max-width: 600px) { body { padding: 10px; } h1 { font-size: 1.5em; } .menu a { padding: 10px; } }";
  html += "</style></head><body>";
  
  html += "<div class='card'>";
  html += "<h1>Файловый менеджер ESP8266</h1>";
  html += "<ul class='menu'>";
  html += "<li><a href='/list'>Список файлов</a></li>";
  html += "<li><a href='/upload'>Загрузить файл</a></li>";
  html += "</ul>";
  html += "</div>";
  
  html += "<div class='card status'>";
  html += "<p><strong>Свободная память:</strong> " + formatFileSize(ESP.getFreeHeap()) + "</p>";
  
  FSInfo fs_info;
  SPIFFS.info(fs_info);
  html += "<p><strong>Всего памяти SPIFFS:</strong> " + formatFileSize(fs_info.totalBytes) + "</p>";
  html += "<p><strong>Использовано:</strong> " + formatFileSize(fs_info.usedBytes) + " (" + 
          String(fs_info.usedBytes * 100 / fs_info.totalBytes) + "%)</p>";
  html += "<p><strong>Свободно:</strong> " + formatFileSize(fs_info.totalBytes - fs_info.usedBytes) + "</p>";
  html += "<p><strong>IP адрес:</strong> " + WiFi.localIP().toString() + "</p>";
  html += "<p><strong>MAC адрес:</strong> " + WiFi.macAddress() + "</p>";
  html += "<p><strong>SSID:</strong> " + WiFi.SSID() + "</p>";
  html += "<p><strong>Уровень сигнала:</strong> " + String(WiFi.RSSI()) + " dBm</p>";
  html += "</div>";
  
  html += "<div class='footer'>";
  html += "Время работы: " + getUptime();
  html += "</div>";
  
  html += "</body></html>";
  server.send(200, "text/html", html);
}

// Вспомогательная функция для получения времени работы
String getUptime() {
  unsigned long currentMillis = millis();
  unsigned long seconds = currentMillis / 1000;
  unsigned long minutes = seconds / 60;
  unsigned long hours = minutes / 60;
  unsigned long days = hours / 24;
  
  return String(days) + " д " + String(hours % 24) + " ч " + String(minutes % 60) + " м " + String(seconds % 60) + " с";
}

void handleFileUpload() {
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    if (!filename.startsWith("/")) {
      filename = "/" + filename;
    }
    Serial.print("Uploading: "); Serial.println(filename);
    
    // Открываем файл для записи
    File file = SPIFFS.open(filename, "w");
    if (!file) {
      Serial.println("Failed to open file for writing");
      return;
    }
  } 
  else if (upload.status == UPLOAD_FILE_WRITE) {
    File file = SPIFFS.open("/" + upload.filename, "a");
    if (file) {
      file.write(upload.buf, upload.currentSize);
      file.close();
    }
  } 
  else if (upload.status == UPLOAD_FILE_END) {
    Serial.print("Upload complete: ");
    Serial.print(upload.totalSize);
    Serial.println(" bytes");
    
    u8g2.clearBuffer();
    u8g2.setCursor(0, 13);
    u8g2.print("Файл загружен:");
    u8g2.setCursor(0, 30);
    u8g2.print(upload.filename.c_str());
    u8g2.setCursor(0, 45);
    u8g2.print(String(upload.totalSize).c_str());
    u8g2.print(" байт");
    u8g2.sendBuffer();
  }
}

void handleFileList() {
  String path = "/";
  Dir dir = SPIFFS.openDir(path);
  
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Список файлов</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 15px; line-height: 1.4; }";
  html += "h1 { font-size: 1.5em; margin: 0 0 15px 0; }";
  html += "ul { list-style-type: none; padding: 0; margin: 0; }";
  html += "li { margin-bottom: 8px; border-bottom: 1px solid #eee; padding-bottom: 8px; }";
  html += "a { color: #0066cc; text-decoration: none; display: block; padding: 8px; }";
  html += "a:hover { background-color: #f0f0f0; }";
  html += ".file-size { color: #666; font-size: 0.8em; margin-left: 10px; }";
  html += ".back-button { display: inline-block; margin-top: 15px; padding: 8px 15px; background-color: #f0f0f0; border-radius: 4px; }";
  html += ".back-button:hover { background-color: #e0e0e0; }";
  html += "@media (max-width: 600px) { body { padding: 10px; } h1 { font-size: 1.2em; } }";
  html += "</style></head><body>";
  html += "<h1>Список файлов</h1>";
  html += "<ul>";
  
  while (dir.next()) {
    File entry = dir.openFile("r");
    String fileName = entry.name();
    size_t fileSize = entry.size();
    
    html += "<li><a href='/view?file=" + fileName + "'>" + fileName.substring(1) + 
            "<span class='file-size'>(" + formatFileSize(fileSize) + ")</span></a></li>";
    
    entry.close();
  }
  
  html += "</ul>";
  html += "<a href='/' class='back-button'>На главную</a>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

// Вспомогательная функция для форматирования размера файла
String formatFileSize(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + " Б";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0, 1) + " КБ";
  } else {
    return String(bytes / 1024.0 / 1024.0, 1) + " МБ";
  }
}

void handleFileView() {
  String fileName = server.arg("file");
  if (fileName == "") {
    server.send(400, "text/html", "<meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>Отсутствует параметр file");
    return;
  }
  if (!SPIFFS.exists(fileName)) {
    server.send(404, "text/html", "<meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>Файл не найден");
    return;
  }
  File file = SPIFFS.open(fileName, "r");
  if (!file) {
    server.send(500, "text/html", "<meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>Ошибка открытия файла");
    return;
  }
  // Чтение файла в строку
  currentText = "";
  while (file.available()) {
    currentText += (char)file.read();
  }
  file.close();
  // Сохраняем имя файла для отображения
  currentFileName = fileName.substring(1); // Убираем начальный слеш
  // Отображение на дисплее
  currentLine = 0;
  displayText();
  // Отправка содержимого файла в браузер
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  html += "<title>Просмотр: " + fileName + "</title>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 0; padding: 10px; line-height: 1.4; }";
  html += "h1 { font-size: 1.5em; margin: 0 0 15px 0; word-break: break-word; }";
  html += "pre { white-space: pre-wrap; word-wrap: break-word; background-color: #f5f5f5; padding: 10px; border-radius: 5px; overflow-x: auto; font-size: 0.9em; }";
  html += "a { color: #0066cc; text-decoration: none; display: inline-block; margin-top: 15px; padding: 8px 15px; background-color: #f0f0f0; border-radius: 4px; }";
  html += "a:hover { background-color: #e0e0e0; }";
  html += "@media (max-width: 600px) { body { padding: 8px; } h1 { font-size: 1.2em; } pre { font-size: 0.8em; } }";
  html += "</style></head><body>";
  html += "<h1>Файл: " + fileName + "</h1>";
  html += "<pre>" + currentText + "</pre>";
  html += "<a href='/list'>Назад к списку файлов</a>";
  html += "</body></html>";
  server.send(200, "text/html", html);
}

void handleNotFound() {
  String message = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  message += "<title>Страница не найдена</title>";
  message += "<style>";
  message += "body { font-family: Arial, sans-serif; margin: 0; padding: 15px; line-height: 1.4; }";
  message += "h1 { font-size: 1.5em; margin: 0 0 15px 0; }";
  message += "p { margin: 8px 0; word-break: break-word; }";
  message += "a { color: #0066cc; text-decoration: none; display: inline-block; margin-top: 15px; padding: 8px 15px; background-color: #f0f0f0; border-radius: 4px; }";
  message += "a:hover { background-color: #e0e0e0; }";
  message += "@media (max-width: 600px) { body { padding: 10px; } h1 { font-size: 1.2em; } }";
  message += "</style></head><body>";
  message += "<h1>Страница не найдена</h1>";
  message += "<p>URI: " + server.uri() + "</p>";
  message += "<p>Метод: " + String((server.method() == HTTP_GET) ? "GET" : "POST") + "</p>";
  message += "<p>Аргументы: " + String(server.args()) + "</p>";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += "<p>" + server.argName(i) + ": " + server.arg(i) + "</p>";
  }
  message += "<a href='/'>На главную</a>";
  message += "</body></html>";
  server.send(404, "text/html", message);
}
void displayText() {
  u8g2.clearBuffer();
  
  // Рисуем рамку и разделительную линию для улучшения читаемости
  u8g2.drawFrame(0, 0, 128, 64);
  u8g2.drawHLine(0, 16, 128);
  
  // Заголовок с именем файла (обрезаем, если слишком длинное)
  String shortFileName = currentFileName;
  if (shortFileName.length() > maxCharsPerLine) {
    shortFileName = shortFileName.substring(0, maxCharsPerLine - 3) + "...";
  }
  
  u8g2.setCursor(4, 13); // Немного отступаем от края
  u8g2.print("Файл: ");
  u8g2.print(shortFileName.c_str());
  
  // Форматируем текст с переносом длинных строк
  String formattedText = formatText(currentText);
  
  // Разбиваем текст на строки
  int startPos = 0;
  int endPos = 0;
  int displayedLines = 0;
  
  // Пропускаем строки до текущей позиции прокрутки
  for (int i = 0; i < currentLine; i++) {
    endPos = formattedText.indexOf('\n', startPos);
    if (endPos == -1) {
      break;
    }
    startPos = endPos + 1;
  }
  
  // Отображаем строки
  while (displayedLines < linesPerScreen && startPos < formattedText.length()) {
    endPos = formattedText.indexOf('\n', startPos);
    if (endPos == -1) {
      endPos = formattedText.length();
    }
    
    String line = formattedText.substring(startPos, endPos);
    u8g2.setCursor(4, 25 + displayedLines * 8); // Увеличиваем расстояние между строками и отступаем от края
    u8g2.print(line.c_str());
    
    startPos = endPos + 1;
    displayedLines++;
  }
  
  // // Отображаем только номера страниц без полосы прокрутки
  // int totalLines = countFormattedLines(currentText);
  // if (totalLines > linesPerScreen) {
  //   // Показываем номер текущей страницы и общее количество страниц
  //   int totalPages = (totalLines + linesPerScreen - 1) / linesPerScreen;
  //   int currentPage = (currentLine / linesPerScreen) + 1;
    
  //   u8g2.setCursor(4, 75);
  //   // u8g2.print(currentPage);
  //   // u8g2.print("/");
  //   // u8g2.print(totalPages);
  // }
  
  u8g2.sendBuffer();
}
