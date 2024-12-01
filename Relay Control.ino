#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>

// Objek RTC dan LCD
RTC_DS3231 rtc;
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

// Pin relay
const int relayPinsOpen[5] = {0, 2, 11, 13, A2};  // Relay untuk membuka valve
const int relayPinsClose[5] = {1, 3, 12, A1, A3}; // Relay untuk menutup valve

// Deklarasi fungsi
void activateRelay(int relayIndex, bool isActive, bool isOpening = true);

// Array waktu delay khusus untuk setiap relay (dalam milidetik)
const int relayOpenDelays[5] = {25000, 30000, 30000, 30000, 30000}; // Contoh delay per relay

// Variabel global
int currentMenu = 0;
int relayTime = 1;  // Default 1 menit untuk relay buka
int startRelay = 0; // Relay pembuka pertama
int startHour = 0;  // Jam mulai
int startMinute = 0; // Menit mulai
unsigned long lastRelayChange = 0;
int currentRelayIndex = -1; // Indeks relay yang sedang aktif
bool isRelayActive = false;
bool isRelayOpening = true;       // Status fase buka (true) atau tutup (false)
bool cycleCompleted = false; // Menandai apakah siklus selesai
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
  EEPROM.put(24, startHour);
  EEPROM.put(28, startMinute);
}

// Fungsi membaca data dari EEPROM
void loadFromEEPROM() {
  EEPROM.get(0, relayTime);
  EEPROM.get(20, startRelay);
  EEPROM.get(24, startHour);
  EEPROM.get(28, startMinute);
}

// Inisialisasi
void setup() {
  lcd.begin(16, 2);
  if (!rtc.begin()) {
    lcd.print("RTC Error!");
    while (1);
  };

  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  for (int i = 0; i < 5; i++) {
    pinMode(relayPinsOpen[i], OUTPUT);
    pinMode(relayPinsClose[i], OUTPUT);
    digitalWrite(relayPinsOpen[i], LOW);
    digitalWrite(relayPinsClose[i], LOW);
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
    lcd.print(startHour);
    lcd.print(":");
    if (startMinute < 10) lcd.print("0");
    lcd.print(startMinute);

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
      lcd.print("MULAI SIRAM:");
      lcd.setCursor(1, 1);
      lcd.print("DARI JAM ");
      lcd.print(startHour);
      lcd.print(":");
      lcd.print(startMinute);
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
    }else {
    if (button == btnUP) {
      if (currentMenu == 1) {
        if (relayTime < 60) relayTime++;
      }
      if (currentMenu == 3) startHour = (startHour + 1) % 24;
    }
    if (button == btnDOWN) {
      if (currentMenu == 1) relayTime = max(1, relayTime - 1);
      if (currentMenu == 3) startHour = (startHour + 23) % 24;
    }
    if (button == btnLEFT) {
      if (currentMenu == 2) startRelay = (startRelay + 4) % 5;
      if (currentMenu == 3) startMinute = (startMinute + 59) % 60;
    }
    if (button == btnRIGHT) {
      if (currentMenu == 2) startRelay = (startRelay + 1) % 5;
      if (currentMenu == 3) startMinute = (startMinute + 1) % 60;
    }
    if (button == btnSELECT) {
      if (currentMenu == 3) {
        saveToEEPROM();
        lcd.clear();
        lcd.setCursor(3, 0);
        lcd.print("PENGATURAN");
        lcd.setCursor(2, 1);
        lcd.print("DISIMPAN!!!!");
        delay(2000);
        currentMenu = 0;
      } else {
        currentMenu = (currentMenu + 1) % 4;
      }
    }
  }
}

void handleRelayLogic() {
  if (currentMenu == 0) {
    DateTime now = rtc.now();

    // Cek apakah waktu mulai telah tiba dan siklus belum selesai
    if (!isRelayActive && now.hour() == startHour && now.minute() == startMinute && !cycleCompleted) {
      isRelayActive = true;
      currentRelayIndex = startRelay; // Mulai dari relay pembuka yang ditentukan
      isRelayOpening = true;          // Awali dengan fase buka
      lastRelayChange = millis();
      // Aktifkan relay buka pertama kali
      activateRelay(currentRelayIndex, true); 
    }

    if (isRelayActive) {
      unsigned long nowMillis = millis();

      if (isRelayOpening) {
        // Logika untuk fase buka
        if (nowMillis - lastRelayChange >= relayTime * 60000) {
          // Fase buka selesai, beralih ke fase tutup
          activateRelay(currentRelayIndex, false); // Matikan relay buka
          lastRelayChange = millis();
          isRelayOpening = false; // Beralih ke fase tutup
          activateRelay(currentRelayIndex, true, false); // Aktifkan relay tutup
        }
      } else {
        // Logika untuk fase tutup
        if (nowMillis - lastRelayChange >= closeTime) {
          // Tutup selesai, lanjutkan ke relay berikutnya
          activateRelay(currentRelayIndex, false, false); // Matikan relay tutup
          
          // Pindah ke relay berikutnya dalam urutan 2-3-4-5-1
          currentRelayIndex = (currentRelayIndex + 1) % 5;
          
          // Jika sudah selesai semua relay (siklus selesai), matikan semua relay
          if (currentRelayIndex == startRelay) {
            cycleCompleted = true; // Selesai semua siklus
            isRelayActive = false; // Matikan seluruh relay
            deactivateAllRelays(); // Menonaktifkan semua relay untuk reset tampilan
          } else {
            // Lanjutkan ke relay berikutnya
            lastRelayChange = millis();
            isRelayOpening = true;  // Beralih kembali ke fase buka
            activateRelay(currentRelayIndex, true); // Aktifkan relay buka untuk relay berikutnya
          }
        }
      }
    }
  }
}

// Fungsi untuk menonaktifkan semua relay
void deactivateAllRelays() {
  for (int i = 0; i < 5; i++) {
    activateRelay(i, false, false); // Matikan setiap relay
}
  
  // Reset status relay
  currentRelayIndex = -1;  // Tidak ada relay yang aktif
  isRelayOpening = true;   // Default ke fase buka untuk siklus berikutnya
  isRelayActive = false;   // Tidak ada relay yang aktif
  cycleCompleted = false;  // Siklus selesai, siap untuk memulai lagi
  
  lcd.clear();
  currentMenu = 0;         // Kembali ke menu awal
  displayMenu();           // Perbarui tampilan ke menu awal
}

void activateRelay(int relayIndex, bool isActive, bool isOpening = true) {
  if (relayIndex < 0 || relayIndex >= 5) return;
  
  // Tentukan pin berdasarkan fase buka/tutup
  int pin = isOpening ? relayPinsOpen[relayIndex] : relayPinsClose[relayIndex];
  
  // Aktifkan pin dengan HIGH saat isActive true, atau LOW saat isActive false
  digitalWrite(pin, isActive ? HIGH : LOW);
}

// Fungsi utama loop
void loop() {
  int button = readButton();
  handleMenuNavigation(button);
  handleRelayLogic();
  displayMenu();
  delay(200);
}
