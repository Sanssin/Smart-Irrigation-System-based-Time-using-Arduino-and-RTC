#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>

//ini program terbaru 2025

// Objek RTC dan LCD
RTC_DS3231 rtc;
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

// Pin relay (dikurangi menjadi 4)
const int relayPinsOpen[3] = {0, 2, 11};  // Relay untuk membuka valve
const int relayPinsClose[3] = {1, 3, 12}; // Relay untuk menutup valve

// Pin Sensor Hujan
const int rainSensorPin [3] = {A1, A2, A3};
const int HEAVY_RAIN_THRESHOLD = 500; // Ambang batas nilai analog untuk hujan deras

// Deklarasi fungsi
void activateRelay(int relayIndex, bool isActive, bool isOpening = true);
void deactivateAllRelays();
bool isHeavyRain(); // Fungsi baru untuk cek hujan deras

// Array waktu delay khusus untuk setiap relay (dalam milidetik)
const int relayOpenDelays[4] = {25000, 30000, 30000, 30000}; // Contoh delay per relay

// Variabel global
int currentMenu = 0;
int relayTime = 1;  // Default 1 menit untuk relay buka
int startRelay = 0; // Relay pembuka pertama
int startHourMorning = 0;  // Jam mulai pagi
int startMinuteMorning = 0; // Menit mulai pagi
int startHourEvening = 0;  // Jam mulai sore
int startMinuteEvening = 0; // Menit mulai sore
unsigned long lastRelayChange = 0;
int currentRelayIndex = -1; // Indeks relay yang sedang aktif
bool isRelayActive = false;
bool isRelayOpening = true;       // Status fase buka (true) atau tutup (false)
bool morningCycleCompleted = false; // Menandai apakah siklus PAGI selesai
bool eveningCycleCompleted = false; // Menandai apakah siklus SORE selesai
bool isMorningCycle = false;      // Penanda untuk tahu siklus mana yg aktif
const int closeTime = 15000;      // Durasi relay tutup (20 detik)

// Tombol LCD shield
const int btnRIGHT = 0;
const int btnUP = 1;
const int btnDOWN = 2;
const int btnLEFT = 3;
const int btnSELECT = 4;

// Fungsi membaca tombol
unsigned long lastButtonPressTime = 0; // Waktu terakhir tombol ditekan
const unsigned long debounceDelay = 1000; // Debounce dalam milidetik

// Variabel tampilan sementara
unsigned long tempDisplayStartTime = 0;
bool showTempDisplay = false;

int readButton() {
  int adc_key_in = analogRead(0);
  if (adc_key_in < 50)   return btnRIGHT;
  if (adc_key_in < 195)  return btnUP;
  if (adc_key_in < 380)  return btnDOWN;
  if (adc_key_in < 555)  return btnLEFT;
  if (adc_key_in < 790) {
    unsigned long now = millis();
    if (now - lastButtonPressTime > debounceDelay) {
      lastButtonPressTime = now;
      return btnSELECT;
    }
  }
  return -1; // Tidak ada tombol yang ditekan
}

// Fungsi menyimpan data ke EEPROM
void saveToEEPROM() {
  EEPROM.put(0, relayTime);
  EEPROM.put(20, startRelay);
  EEPROM.put(24, startHourMorning);
  EEPROM.put(28, startMinuteMorning);
  EEPROM.put(32, startHourEvening);
  EEPROM.put(36, startMinuteEvening);
}

// Fungsi membaca data dari EEPROM
void loadFromEEPROM() {
  EEPROM.get(0, relayTime);
  EEPROM.get(20, startRelay);
  EEPROM.get(24, startHourMorning);
  EEPROM.get(28, startMinuteMorning);
  EEPROM.get(32, startHourEvening);
  EEPROM.get(36, startMinuteEvening);
}

// Inisialisasi
void setup() {
  //Serial.begin(9600); // Mulai komunikasi serial
  lcd.begin(16, 2);
  if (!rtc.begin()) {
    lcd.print("RTC Error!");
    while (1);
  };

  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  pinMode(rainSensorPin, INPUT_PULLUP); // Inisialisasi pin sensor hujan

  for (int i = 0; i < 4; i++) {
    pinMode(relayPinsOpen[i], OUTPUT);
    pinMode(relayPinsClose[i], OUTPUT);
    digitalWrite(relayPinsOpen[i], HIGH); // Set relay to HIGH (inactive) for Low Level Trigger
    digitalWrite(relayPinsClose[i], HIGH); // Set relay to HIGH (inactive) for Low Level Trigger
  }

  loadFromEEPROM();
}

// Fungsi menampilkan menu
void displayMenu() {
  lcd.clear();
  DateTime now = rtc.now();

  if (showTempDisplay) {
    lcd.setCursor(1, 0);
    lcd.print("AKAN MENYIRAM");
    lcd.setCursor(3, 1);
    lcd.print("JAM ");
    lcd.print(startHourMorning);
    lcd.print(":");
    if (startMinuteMorning < 10) lcd.print("0");
    lcd.print(startMinuteMorning);

    if (millis() - tempDisplayStartTime > 2000) {
      showTempDisplay = false;
    }
    return;
  }

  switch (currentMenu) {
    case 0:
      lcd.setCursor(0, 0);
      lcd.print("WAKTU : ");
      lcd.print(now.hour(), DEC);
      lcd.print(":");
      lcd.print(now.minute(), DEC);
      lcd.print(":");
      lcd.print(now.second(), DEC);
      lcd.setCursor(0, 1);
      if (currentRelayIndex == -1) {
        lcd.print("PIPA TIDAK AKTIF");
      } else {
        lcd.print("PIPA ");
        lcd.print(currentRelayIndex + 1);
        lcd.print((isRelayOpening) ? " TERBUKA" : " MENUTUP");
      }
      break;
    case 1:
      lcd.setCursor(0, 0);
      lcd.print("DURASI SIRAM:");
      lcd.setCursor(4, 1);
      lcd.print(relayTime);
      lcd.print(" MENIT");
      break;
    case 2:
      lcd.setCursor(0, 0);
      lcd.print("MULAI SIRAM:");
      lcd.setCursor(2, 1);
      lcd.print("DARI PIPA ");
      lcd.print(startRelay + 1);
      break;
    case 3:
      lcd.setCursor(0, 0);
      lcd.print("MULAI PAGI:");
      lcd.setCursor(1, 1);
      lcd.print(startHourMorning);
      lcd.print(":");
      if (startMinuteMorning < 10) lcd.print("0");
      lcd.print(startMinuteMorning);
      break;
    case 4:
      lcd.setCursor(0, 0);
      lcd.print("MULAI SORE:");
      lcd.setCursor(1, 1);
      lcd.print(startHourEvening);
      lcd.print(":");
      if (startMinuteEvening < 10) lcd.print("0");
      lcd.print(startMinuteEvening);
      break;
  }
}

void handleMenuNavigation(int button) {
  if (currentMenu == 0) {
    if (button == btnSELECT) {
      currentMenu = (currentMenu + 1) % 4;
    }
    if (button == btnUP) {
      showTempDisplay = true;
      tempDisplayStartTime = millis();
    }
    if (button == btnRIGHT) {
      // Tampilkan durasi siram ketika tombol RIGHT ditekan
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("DURASI MENYIRAM");
      lcd.setCursor(1, 1);
      lcd.print("SELAMA ");
      lcd.print(relayTime);
      lcd.print(" MENIT");
      delay(2000); // Tampilkan durasi siram selama 2 detik
      currentMenu = 0; // Kembali ke menu awal
    }
    if (button == btnDOWN) {
      // Tampilkan "By hima einsten.com" selama 2 detik ketika tombol DOWN ditekan
      lcd.clear();
      lcd.setCursor(6, 0);
      lcd.print("By");
      lcd.setCursor(0, 1);
      lcd.print("HIMA EINSTEN.COM");
      delay(2000); // Tampilkan selama 2 detik
      currentMenu = 0; // Kembali ke menu awal
    }
  } else {
    if (button == btnUP) {
      if (currentMenu == 1) {
        if (relayTime < 60) relayTime++;
      }
      if (currentMenu == 3) startHourMorning = (startHourMorning + 1) % 24;
      if (currentMenu == 4) startHourEvening = (startHourEvening + 1) % 24;
    }
    if (button == btnDOWN) {
      if (currentMenu == 1) relayTime = max(1, relayTime - 1);
      if (currentMenu == 3) startHourMorning = (startHourMorning + 23) % 24;
      if (currentMenu == 4) startHourEvening = (startHourEvening + 23) % 24;
    }
    if (button == btnLEFT) {
      if (currentMenu == 2) startRelay = (startRelay + 3) % 3; // Disesuaikan untuk 4 relay
      if (currentMenu == 3) startMinuteMorning = (startMinuteMorning + 59) % 60;
      if (currentMenu == 4) startMinuteEvening = (startMinuteEvening + 59) % 60;
    }
    if (button == btnRIGHT) {
      if (currentMenu == 2) startRelay = (startRelay + 1) % 3; // Disesuaikan untuk 4 relay
      if (currentMenu == 3) startMinuteMorning = (startMinuteMorning + 1) % 60;
      if (currentMenu == 4) startMinuteEvening = (startMinuteEvening + 1) % 60;
    }
    if (button == btnSELECT) {
      if (currentMenu == 4) {
        saveToEEPROM();
        lcd.clear();
        lcd.setCursor(3, 0);
        lcd.print("PENGATURAN");
        lcd.setCursor(2, 1);
        lcd.print("DISIMPAN!!!!");
        delay(2000);
        currentMenu = 0;
      } else {
        currentMenu = (currentMenu + 1) % 5;
      }
    }
  }
}

void handleRelayLogic() {
  if (currentMenu != 0) return; // Hanya jalankan logika jika di menu utama

  DateTime now = rtc.now();

  // 1. Reset semua status siklus setiap tengah malam
  if (now.hour() == 0 && now.minute() == 0) {
    morningCycleCompleted = false;
    eveningCycleCompleted = false;
  }

  // 2. Hentikan siklus yang sedang berjalan jika tiba-tiba hujan deras
  if (isRelayActive && isHeavyRain()) {
    lcd.clear();
    lcd.print("HUJAN DERAS,");
    lcd.setCursor(0, 1);
    lcd.print("SIRAM DIHENTIKAN!");
    deactivateAllRelays();
    if (isMorningCycle) {
      morningCycleCompleted = true; // Tandai siklus pagi selesai untuk hari ini
    } else {
      eveningCycleCompleted = true; // Tandai siklus sore selesai untuk hari ini
    }
    delay(5000);
    return;
  }

  // 3. Logika untuk memulai siklus PAGI
  if (now.hour() == startHourMorning && now.minute() == startMinuteMorning && !morningCycleCompleted) {
    if (isHeavyRain()) {
      lcd.clear();
      lcd.print("PAGI HUJAN DERAS,");
      lcd.setCursor(0, 1);
      lcd.print("SIRAM DIBATALKAN");
      morningCycleCompleted = true; // Batalkan hanya siklus pagi
      delay(5000);
      return;
    } else {
      isRelayActive = true;
      isMorningCycle = true; // Set penanda bahwa ini siklus pagi
      currentRelayIndex = startRelay;
      isRelayOpening = true;
      lastRelayChange = millis();
      activateRelay(currentRelayIndex, true);
    }
  }

  // 4. Logika untuk memulai siklus SORE
  if (now.hour() == startHourEvening && now.minute() == startMinuteEvening && !eveningCycleCompleted) {
    if (isHeavyRain()) {
      lcd.clear();
      lcd.print("SORE HUJAN DERAS,");
      lcd.setCursor(0, 1);
      lcd.print("SIRAM DIBATALKAN");
      eveningCycleCompleted = true; // Batalkan hanya siklus sore
      delay(5000);
      return;
    } else {
      isRelayActive = true;
      isMorningCycle = false; // Set penanda bahwa ini siklus sore
      currentRelayIndex = startRelay;
      isRelayOpening = true;
      lastRelayChange = millis();
      activateRelay(currentRelayIndex, true);
    }
  }

  // 5. Logika utama saat relay aktif (proses penyiraman)
  if (isRelayActive) {
    unsigned long nowMillis = millis();

    if (isRelayOpening) {
      if (nowMillis - lastRelayChange >= (unsigned long)relayTime * 60000) {
        activateRelay(currentRelayIndex, false);
        lastRelayChange = millis();
        isRelayOpening = false;
        activateRelay(currentRelayIndex, true, false);
      }
    } else {
      if (nowMillis - lastRelayChange >= closeTime) {
        activateRelay(currentRelayIndex, false, false);
        currentRelayIndex = (currentRelayIndex + 1) % 3;
        
        if (currentRelayIndex == startRelay) {
          // Siklus selesai, tandai sesuai siklusnya (pagi/sore)
          if (isMorningCycle) {
            morningCycleCompleted = true;
          } else {
            eveningCycleCompleted = true;
          }
          isRelayActive = false;
          deactivateAllRelays();
        } else {
          lastRelayChange = millis();
          isRelayOpening = true;
          activateRelay(currentRelayIndex, true);
        }
      }
    }
  }
}

// Fungsi cek hujan deras berdasarkan nilai analog
bool isHeavyRain() {
  int rainValue = analogRead(rainSensorPin);
  return rainValue < HEAVY_RAIN_THRESHOLD; // True jika nilai di bawah ambang batas
}

// Fungsi untuk menonaktifkan semua relay
void deactivateAllRelays() {
  for (int i = 0; i < 3; i++) { // Disesuaikan untuk 4 relay
    activateRelay(i, false, true);  // Matikan relay buka
    activateRelay(i, false, false); // Matikan relay tutup
  }
  
  // Reset status relay
  currentRelayIndex = -1;  // Tidak ada relay yang aktif
  isRelayOpening = true;   // Default ke fase buka untuk siklus berikutnya
  isRelayActive = false;   // Tidak ada relay yang aktif
  
  lcd.clear();
  currentMenu = 0;         // Kembali ke menu awal
  displayMenu();           // Perbarui tampilan ke menu awal
}

void activateRelay(int relayIndex, bool isActive, bool isOpening = true) {
  if (relayIndex < 0 || relayIndex >= 3) return; // Disesuaikan untuk 4 relay
  
  // Tentukan pin berdasarkan fase buka/tutup
  int pin = isOpening ? relayPinsOpen[relayIndex] : relayPinsClose[relayIndex];
  
  // Aktifkan pin dengan LOW saat isActive true (Low Level Trigger), atau HIGH saat isActive false
  digitalWrite(pin, isActive ? LOW : HIGH);
}

// Fungsi utama loop
void loop() {
  // Cetak nilai sensor hujan ke Serial Monitor untuk debugging
  //int rainValue = analogRead(rainSensorPin);
  //Serial.print("Nilai Sensor Hujan: ");
  //Serial.println(rainValue);

  int button = readButton();
  handleMenuNavigation(button);
  handleRelayLogic();
  displayMenu();
  delay(200);
}
