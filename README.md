# Deauther_v2_by_unit3301
Deauther_v2_by_unit3301

Основа:
1) Аккумулятор  - Go Power LP603048
2) Контроллер заряда - TP4056
3) Платы ESP8266 D1 Mini v2 
4) Дисплей - OLED 1.3" 128x64, I2C
5) Кнопки 
6) Тумблер

Это вторая версия продвинутого деаутентификатора.
Принцип работы:
1) Main node - сканирование сетей.
2) Выбор сети.
3) Сканирование клиентов в сети.
4) Отправка данных через RX -TX на slave node.
5) Slave node - Прием данных, парсинг, деаутентификация. 
6) Main node- поднятие точки доступа, captive portal.
7) После подключения клиента и передачи пароля, - вывод на дисплей.
8) Отключение точки доступа. 
9) Сохранение пароля. 

Важные моменты:
1) Платы имеют общий Serial Monitor.
2) Сначала залить прошивку, потом паять TX - TX. Иначе прошивка не зальется. 
3) Функции деаутентификации работают с библиотеками:
https://raw.githubusercontent.com/SpacehuhnTech/arduino/main/package_spacehuhn_index.json
(File -> Preference -> Additional boards manager URLs:)

Captive Portal работает только для iphone. 
Для остального надо дорабатывать в фукции startFakeAP , при server.on("/hotspot-detect.html", handleRoot);
