# Sterownik DMX – ESP32 + W5500 (Ethernet)

Panel WWW do sterowania oświetleniem DMX (lampy RGB i LED bary) ze sceną, chaserami i zapisem do pamięci Flash.
Komunikacja wyłącznie po kablu Ethernet (moduł W5500) – Wi-Fi jest całkowicie wyłączone.

## Sprzęt

- ESP32 (DevKit, `esp32dev`)
- Moduł Ethernet W5500 (SPI)
- Konwerter TTL → RS485 (np. Waveshare TTL TO RS485 (C) – sam przełącza kierunek, DE/RE niepotrzebne)
- Lampy DMX + kabel XLR

## Podłączenie

### W5500 (zasilanie **3.3 V**)

| W5500 | ESP32  |
|-------|--------|
| SCK   | GPIO18 |
| MISO  | GPIO19 |
| MOSI  | GPIO23 |
| CS    | GPIO5  |
| RST   | GPIO26 |
| GND   | GND    |

### DMX (RS485)

| Sygnał         | ESP32 / XLR       |
|----------------|-------------------|
| TX → wejście TTL modułu | GPIO17   |
| RX ← wyjście TTL modułu | GPIO16 (nieużywany, wymagany przez bibliotekę) |
| A+             | XLR pin 3         |
| B-             | XLR pin 2         |
| Masa RS485 (PE w Waveshare) | XLR pin 1 |

Jeśli lampy nie reagują mimo poprawnego kodu – najpierw spróbuj zamienić A i B.

## Kompilacja

Projekt PlatformIO (`platformio.ini`):

- platforma `espressif32@~6.3.2` (rdzeń Arduino ESP32 2.0.9)
- biblioteki z przypiętymi wersjami: `esp_dmx` 4.1.x, `Ethernet_Generic` 2.8.1, `EthernetWebServer` 2.4.1, `Functional-Vlpp` 1.0.2

Wgranie: `pio run -t upload`, podgląd logów: `pio device monitor` (115200 baud).

### Poprawka kompilacji Ethernet_Generic

W rdzeniu ESP32 2.x klasa `Server` ma czysto wirtualną metodę `begin(uint16_t port)`, której `EthernetServer` z Ethernet_Generic nie implementuje. Bez poprawki kompilacja kończy się błędem:

```
cannot declare field 'EthernetWebServer::_server' to be of abstract type 'EthernetServer'
```

Dlatego na górze `main.cpp` klasa bazowa `Server` jest podmieniana na własną (`EthServerBase`) tylko na czas dołączania bibliotek Ethernet. Biblioteki khoih-prog są zarchiwizowane, więc ten błąd nie zostanie naprawiony u źródła.

## Sieć

Domyślnie **statyczny adres `192.168.1.10`** (brama `192.168.1.1`, maska `255.255.255.0`).

- Komputer, z którego otwierasz panel, musi być w tej samej podsieci (`192.168.1.x`).
- Przy podłączeniu do routera z inną pulą adresów ustaw w `main.cpp`:
  ```cpp
  #define USE_DHCP true
  ```
  ESP najpierw spróbuje DHCP (8 s), a gdy się nie uda – przejdzie na statyczny adres.

Po starcie na Serial Monitorze pojawia się adres panelu, np. `Panel DMX: http://192.168.1.10`.

### Diagnostyka na Serial Monitorze

| Komunikat | Co sprawdzić |
|-----------|--------------|
| `BLAD: Nie znaleziono W5500!` | Przewody SCK/MISO/MOSI/CS, zasilanie 3.3 V, wspólna masa |
| `Brak linku - kabel Ethernet niepodlaczony?` | Kabel, port w routerze/switchu, diody na złączu RJ45 |
| `DHCP nie odpowiada - uzywam statycznego IP.` | Brak serwera DHCP – używany jest adres statyczny |

## Układ kanałów DMX

Panel zakłada poniższe tryby i adresy. **Ustawienia w menu lamp muszą się z tym zgadzać**, inaczej suwaki będą sterować niewłaściwymi kanałami.

| Urządzenie | Adres | Tryb | Kanały |
|------------|-------|------|--------|
| Lampa Początkowa 1 | 1  | 7CH | Dimmer, R, G, B, (strobo/efekty) |
| Lampa Początkowa 2 | 8  | 7CH | Dimmer, R, G, B, (strobo/efekty) |
| LedBar 1 | 15 | 6CH | 2 segmenty × RGB |
| LedBar 2 | 21 | 6CH | 2 segmenty × RGB |
| LedBar 3 | 27 | 6CH | 2 segmenty × RGB |
| Lampa Końcowa 1 | 33 | 7CH | Dimmer, R, G, B, (strobo/efekty) |
| Lampa Końcowa 2 | 40 | 7CH | Dimmer, R, G, B, (strobo/efekty) |

Lampy z dimmerem nie zaświecą, dopóki suwak **DIM** jest na 0.

Układ zmienia się w tablicy `layoutConfig` w części HTML/JS w `main.cpp`.

## Funkcje panelu

- Suwaki R/G/B (i DIM) dla każdej lampy i segmentu, pole HEX, podgląd koloru – zmiany wysyłane na żywo
- **Główny Blackout** – wszystkie kanały na 0
- **Sceny** (do 16) – zapis aktualnego stanu, wczytanie, nadpisanie, zmiana nazwy, usuwanie
- **Chasery** (do 16, po 32 kroki) – sekwencja numerów scen, np. `0,1,2`, z globalnym czasem Fade i Hold (ms)
- Sceny i chasery są zapisywane w pamięci Flash (`Preferences`) i przetrwają restart

DMX jest wysyłany w sposób ciągły (~33 Hz) – większość lamp bez stałego sygnału robi blackout po ok. 1 s.

## API (HTTP GET)

| Endpoint | Opis |
|----------|------|
| `/` | Panel WWW |
| `/state` | Lista scen i chaserów (JSON) |
| `/getDmx` | Aktualne wartości 513 bajtów DMX (JSON) |
| `/setSingle?ch=&v=` | Ustawia kanał `ch` (1–512) na wartość `v` (0–255) |
| `/blackout` | Wszystkie kanały na 0 |
| `/saveScene` | Zapisuje bieżący stan jako nową scenę |
| `/updateScene?id=` | Nadpisuje scenę bieżącym stanem |
| `/renameScene?id=&name=` | Zmienia nazwę sceny |
| `/loadScene?id=` | Wczytuje scenę |
| `/deleteScene?id=` | Usuwa scenę (zatrzymuje chaser) |
| `/addChaser` | Dodaje pusty chaser |
| `/saveChaser?id=&seq=` | Zapisuje sekwencję, np. `seq=0,1,2` |
| `/playChaser?id=&f=&h=` | Odtwarza chaser z fade `f` i hold `h` (ms) |
| `/deleteChaser?id=` | Usuwa chaser |
| `/stop` | Zatrzymuje chaser |

## Zmiany względem wersji Wi-Fi

- Wi-Fi usunięte, zamiast `WebServer` używany jest `EthernetWebServer` (to samo API).
- Reset sprzętowy W5500 przy starcie, jawne piny SPI, oczekiwanie na link, czytelne komunikaty diagnostyczne.
- Opcjonalne DHCP z powrotem do statycznego IP i odnawianiem dzierżawy (`Ethernet.maintain()`).
- Poprawione błędy:
  - cudzysłów lub backslash w nazwie sceny psuł JSON i panel przestawał się ładować – nazwy są teraz escapowane (JSON i HTML),
  - ujemny lub nieistniejący numer sceny w chaserze oraz usunięcie sceny w trakcie odtwarzania mogły czytać pamięć poza tablicą – zły numer zamieniany jest na scenę 0, a usunięcie sceny zatrzymuje chaser,
  - ujemny czas Fade/Hold zamieniał się na ogromną liczbę i chaser „stawał”,
  - wartość kanału ograniczona do 0–255, liczba kroków chasera wczytana z Flash jest sprawdzana, sekwencja chasera kodowana w URL.
