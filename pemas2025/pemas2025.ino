#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>

// Objek RTC dan LCD
RTC_DS3231 rtc;
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

// Pin relay (4 relay)
const int relayPinsOpen[4] = {0, 2, 11, 13};  // Relay untuk membuka valve
const int relayPinsClose[4] = {1, 3, 12, 10}; // Relay untuk menutup valve

// Pin Sensor Hujan
const int rainSensorPin = A1; // Sensor hujan tunggal
const int LIGHT_RAIN_THRESHOLD = 700; // Hujan ringan (nilai tinggi = sedikit basah)
const int HEAVY_RAIN_THRESHOLD = 400; // Hujan deras (nilai rendah = sangat basah)

// Deklarasi fungsi
void activateRelay(int relayIndex, bool isActive, bool isOpening = true);
void deactivateAllRelays();
bool isHeavyRain(); // Fungsi baru untuk cek hujan deras

// Array waktu delay khusus untuk setiap relay (dalam milidetik)
const int relayOpenDelays[4] = {25000, 30000, 30000, 30000}; // Contoh delay per relay

// Variabel global
int currentMenu = 0;
int relayTime = 1;  // Default 1 menit untuk relay buka
int startRelay = 1; // Relay pembuka pertama
int startHourMorning = 6;  // Jam mulai pagi
int startMinuteMorning = 0; // Menit mulai pagi
int startHourEvening = 16;  // Jam mulai sore
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

// Variabel untuk non-blocking display
unsigned long infoDisplayStartTime = 0;
bool showInfoDisplay = false;
int infoDisplayType = 0; // 1=durasi, 2=credits, 3=saved

// Variabel untuk setting waktu manual
unsigned long upButtonPressTime = 0;
bool upButtonPressed = false;
bool manualTimeMode = false;
bool manualTimeConfirm = false; // Mode konfirmasi setelah setting waktu
int manualHour = 0;
int manualMinute = 0;
bool editingHour = true;

// Variabel untuk tracking hujan dalam 2 jam terakhir
unsigned long lastHeavyRainTime = 0;
const unsigned long RAIN_SKIP_DURATION = 2UL * 60UL * 60UL * 1000UL; // 2 jam dalam milidetik

// Variabel untuk debugging dan mencegah spam
unsigned long lastSerialPrint = 0;
const unsigned long SERIAL_PRINT_INTERVAL = 2000; // Print setiap 2 detik

int readButton() {
  int adc_key_in = analogRead(0);
  unsigned long now = millis();
  
  if (adc_key_in < 50)   return btnRIGHT;
  if (adc_key_in < 195) {
    // Deteksi tombol UP untuk setting waktu manual
    if (!upButtonPressed) {
      upButtonPressed = true;
      upButtonPressTime = now;
    } else if (now - upButtonPressTime >= 5000 && currentMenu == 0) {
      // Masuk ke mode setting waktu manual setelah 5 detik
      DateTime currentTime = rtc.now();
      manualHour = currentTime.hour();
      manualMinute = currentTime.minute();
      manualTimeMode = true;
      editingHour = true;
      upButtonPressed = false;
      return -2; // Kode khusus untuk masuk manual time mode
    }
    return btnUP;
  }
  if (adc_key_in < 380) {
    upButtonPressed = false;
    return btnDOWN;
  }
  if (adc_key_in < 555) {
    upButtonPressed = false;
    return btnLEFT;
  }
  if (adc_key_in < 790) {
    upButtonPressed = false;
    if (now - lastButtonPressTime > debounceDelay) {
      lastButtonPressTime = now;
      return btnSELECT;
    }
  }
  
  // Reset upButtonPressed jika tidak ada tombol yang ditekan
  upButtonPressed = false;
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
  // Serial.begin(9600); // Nonaktifkan serial untuk mencegah interferensi relay
  lcd.begin(16, 2);

  // 1. Inisialisasi komunikasi dengan RTC
  if (!rtc.begin()) {
    lcd.print("RTC Error!");
    while (1);
  };

  // 2. LOGIKA CERDAS UNTUK MENANGANI 'lostPower'
  if (rtc.lostPower()) {
    // Jika ada laporan 'lostPower', kita periksa dulu waktunya
    DateTime now = rtc.now();
    
    // 2a. Cek apakah tahunnya tidak wajar (di bawah 2025)
    // Ini menandakan baterai RTC benar-benar mati dan waktu kacau.
    if (now.year() < 2025) {
      lcd.clear();
      lcd.print("Baterai RTC Habis");
      lcd.setCursor(0, 1);
      lcd.print("Waktu di-reset..");
      delay(2000);
      // Karena waktu sudah pasti salah, kita reset ke waktu compile
      // sebagai solusi darurat.
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    // 2b. Jika tahunnya wajar (else)
    // Ini berarti hanya ada gangguan di VCC, tapi waktu tetap aman berkat baterai.
    // Dalam kasus ini, kita TIDAK MELAKUKAN APA-APA dan biarkan program lanjut
    // dengan waktu yang benar dari RTC.
  }

  // 3. Inisialisasi pin sensor hujan dan relay
  pinMode(rainSensorPin, INPUT); 

  for (int i = 0; i < 3; i++) {
    pinMode(relayPinsOpen[i], OUTPUT);
    pinMode(relayPinsClose[i], OUTPUT);
    digitalWrite(relayPinsOpen[i], HIGH); 
    digitalWrite(relayPinsClose[i], HIGH);
  }

  // 4. Muat pengaturan jadwal dari EEPROM
  loadFromEEPROM();
}

// Fungsi menampilkan menu
void displayMenu() {
  lcd.clear();
  DateTime now = rtc.now();

  // Mode konfirmasi waktu manual
  if (manualTimeConfirm) {
    lcd.setCursor(0, 0);
    lcd.print("SIMPAN WAKTU?");
    lcd.setCursor(0, 1);
    lcd.print("UP=Ya DOWN=Tidak");
    return;
  }

  // Mode setting waktu manual
  if (manualTimeMode) {
    lcd.setCursor(0, 0);
    lcd.print("SETTING WAKTU:");
    lcd.setCursor(0, 1);
    
    if (editingHour) {
      lcd.print("JAM: [");
      if (manualHour < 10) lcd.print("0");
      lcd.print(manualHour);
      lcd.print("]");
    } else {
      lcd.print("JAM: ");
      if (manualHour < 10) lcd.print("0");
      lcd.print(manualHour);
    }
    
    lcd.print(" ");
    
    if (!editingHour) {
      lcd.print("MEN:[");
      if (manualMinute < 10) lcd.print("0");
      lcd.print(manualMinute);
      lcd.print("]");
    } else {
      lcd.print("MEN:");
      if (manualMinute < 10) lcd.print("0");
      lcd.print(manualMinute);
    }
    return;
  }

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

  if (showInfoDisplay) {
    if (infoDisplayType == 1) {
      lcd.setCursor(0, 0);
      lcd.print("DURASI MENYIRAM");
      lcd.setCursor(1, 1);
      lcd.print("SELAMA ");
      lcd.print(relayTime);
      lcd.print(" MENIT");
    } else if (infoDisplayType == 2) {
      lcd.setCursor(6, 0);
      lcd.print("By");
      lcd.setCursor(0, 1);
      lcd.print("HIMA EINSTEN.COM");
    } else if (infoDisplayType == 3) {
      // Info sensor hujan
      int rainValue = analogRead(rainSensorPin);
      lcd.setCursor(0, 0);
      lcd.print("SENSOR HUJAN:");
      lcd.setCursor(0, 1);
      lcd.print("VAL:");
      lcd.print(rainValue);
      if (rainValue < HEAVY_RAIN_THRESHOLD) {
        lcd.print(" DERAS");
      } else if (rainValue < LIGHT_RAIN_THRESHOLD) {
        lcd.print(" RINGAN");
      } else {
        lcd.print(" KERING");
      }
    }
    
    if (millis() - infoDisplayStartTime > 3000) { // Perpanjang waktu tampil untuk info sensor
      showInfoDisplay = false;
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
      
      // Prioritas tampilan: Hujan Deras > Hujan Ringan > Relay Aktif > Status Normal
      if (isHeavyRain()) {
        lcd.print("** HUJAN DERAS **");
      } else if (isLightRain()) {
        lcd.print("- HUJAN RINGAN -");
      } else if (hasRecentHeavyRain()) {
        lcd.print("HUJAN 2JAM LALU");
      } else if (currentRelayIndex == -1) {
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
  // Mode konfirmasi waktu manual
  if (manualTimeConfirm) {
    if (button == btnUP) {
      // Ya, simpan waktu ke RTC
      DateTime now = rtc.now();
      rtc.adjust(DateTime(now.year(), now.month(), now.day(), manualHour, manualMinute, 0));
      
      // Tampilkan konfirmasi
      lcd.clear();
      lcd.setCursor(2, 0);
      lcd.print("WAKTU DISET!");
      lcd.setCursor(1, 1);
      lcd.print("JAM ");
      if (manualHour < 10) lcd.print("0");
      lcd.print(manualHour);
      lcd.print(":");
      if (manualMinute < 10) lcd.print("0");
      lcd.print(manualMinute);
      delay(2000);
      
      manualTimeConfirm = false;
      manualTimeMode = false;
      currentMenu = 0;
    }
    if (button == btnDOWN) {
      // Tidak, batalkan setting waktu
      lcd.clear();
      lcd.setCursor(1, 0);
      lcd.print("SETTING DIBATAL");
      delay(2000);
      
      manualTimeConfirm = false;
      manualTimeMode = false;
      currentMenu = 0;
    }
    return;
  }

  // Mode setting waktu manual
  if (manualTimeMode) {
    if (button == btnUP) {
      if (editingHour) {
        manualHour = (manualHour + 1) % 24;
      } else {
        manualMinute = (manualMinute + 1) % 60;
      }
    }
    if (button == btnDOWN) {
      if (editingHour) {
        manualHour = (manualHour + 23) % 24;
      } else {
        manualMinute = (manualMinute + 59) % 60;
      }
    }
    if (button == btnRIGHT || button == btnLEFT) {
      editingHour = !editingHour; // Toggle antara jam dan menit
    }
    if (button == btnSELECT) {
      // Masuk ke mode konfirmasi
      manualTimeConfirm = true;
    }
    return;
  }

  if (currentMenu == 0) {
    if (button == btnSELECT) {
      currentMenu = (currentMenu + 1) % 5;
    }
    if (button == btnUP && !manualTimeMode) {
      showTempDisplay = true;
      tempDisplayStartTime = millis();
    }
    if (button == btnRIGHT) {
      showInfoDisplay = true;
      infoDisplayType = 1;
      infoDisplayStartTime = millis();
    }
    if (button == btnDOWN) {
      showInfoDisplay = true;
      infoDisplayType = 2;
      infoDisplayStartTime = millis();
    }
    if (button == btnLEFT) {
      // Tampilkan info sensor hujan
      showInfoDisplay = true;
      infoDisplayType = 3;
      infoDisplayStartTime = millis();
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
      if (currentMenu == 2) startRelay = (startRelay + 3) % 3; // 4 relay
      if (currentMenu == 3) startMinuteMorning = (startMinuteMorning + 59) % 60;
      if (currentMenu == 4) startMinuteEvening = (startMinuteEvening + 59) % 60;
    }
    if (button == btnRIGHT) {
      if (currentMenu == 2) startRelay = (startRelay + 1) % 3; // 4 relay
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
  if (currentMenu != 0 || manualTimeMode || manualTimeConfirm) return; // Hanya jalankan logika jika di menu utama dan tidak dalam mode setting waktu

  DateTime now = rtc.now();

  // 1. Reset semua status siklus setiap tengah malam
  if (now.hour() == 0 && now.minute() == 0) {
    morningCycleCompleted = false;
    eveningCycleCompleted = false;
    // Reset timer hujan juga setiap tengah malam
    lastHeavyRainTime = 0;
  }

  // 2. Hentikan siklus yang sedang berjalan jika tiba-tiba hujan deras
  static unsigned long rainStopDisplayTime = 0;
  static bool rainStopDisplayActive = false;
  
  if (isRelayActive && isHeavyRain() && !rainStopDisplayActive) {
    deactivateAllRelays();
    if (isMorningCycle) {
      morningCycleCompleted = true;
    } else {
      eveningCycleCompleted = true;
    }
    rainStopDisplayActive = true;
    rainStopDisplayTime = millis();
  }
  
  if (rainStopDisplayActive) {
    if (millis() - rainStopDisplayTime < 5000) {
      lcd.clear();
      lcd.print("HUJAN DERAS,");
      lcd.setCursor(0, 1);
      lcd.print("SIRAM DIHENTIKAN!");
      return;
    } else {
      rainStopDisplayActive = false;
    }
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
    } else if (hasRecentHeavyRain()) {
      lcd.clear();
      lcd.print("HUJAN 2 JAM LALU,");
      lcd.setCursor(0, 1);
      lcd.print("SIRAM DILEWATI");
      morningCycleCompleted = true; // Skip siklus pagi
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
    } else if (hasRecentHeavyRain()) {
      lcd.clear();
      lcd.print("HUJAN 2 JAM LALU,");
      lcd.setCursor(0, 1);
      lcd.print("SORE DILEWATI");
      eveningCycleCompleted = true; // Skip siklus sore
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
        currentRelayIndex = (currentRelayIndex + 1) % 4; // 4 relay total (0,1,2,3)
        
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
  bool heavyRain = rainValue < HEAVY_RAIN_THRESHOLD; // True jika nilai di bawah ambang batas hujan deras
  
  // Update waktu terakhir hujan deras
  if (heavyRain) {
    lastHeavyRainTime = millis();
  }
  
  return heavyRain;
}

// Fungsi cek hujan ringan
bool isLightRain() {
  int rainValue = analogRead(rainSensorPin);
  return rainValue < LIGHT_RAIN_THRESHOLD && rainValue >= HEAVY_RAIN_THRESHOLD;
}

// Fungsi untuk mengecek apakah ada hujan dalam 2 jam terakhir
bool hasRecentHeavyRain() {
  return (millis() - lastHeavyRainTime) < RAIN_SKIP_DURATION;
}

// Fungsi untuk menonaktifkan semua relay
void deactivateAllRelays() {
  for (int i = 0; i < 4; i++) { // 4 relay sesuai dengan array relayPinsOpen/Close
    activateRelay(i, false, true);  // Matikan relay buka
    activateRelay(i, false, false); // Matikan relay tutup
  }
  
  // Reset status relay
  currentRelayIndex = -1;  // Tidak ada relay yang aktif
  isRelayOpening = true;   // Default ke fase buka untuk siklus berikutnya
  isRelayActive = false;   // Tidak ada relay yang aktif
}

void activateRelay(int relayIndex, bool isActive, bool isOpening = true) {
  if (relayIndex < 0 || relayIndex >= 4) return; // 4 relay sesuai dengan array
  
  // Tentukan pin berdasarkan fase buka/tutup
  int pin = isOpening ? relayPinsOpen[relayIndex] : relayPinsClose[relayIndex];
  
  // Aktifkan pin dengan LOW saat isActive true (Low Level Trigger), atau HIGH saat isActive false
  digitalWrite(pin, isActive ? LOW : HIGH);
}

// Fungsi utama loop
void loop() {
  // Nonaktifkan Serial Print untuk mencegah interferensi relay
  /*
  if (millis() - lastSerialPrint > SERIAL_PRINT_INTERVAL) {
    int rainValue = analogRead(rainSensorPin);
    Serial.print("Nilai Sensor Hujan: ");
    Serial.print(rainValue);
    Serial.print(" | Threshold: ");
    Serial.print(HEAVY_RAIN_THRESHOLD);
    Serial.print(" | Status: ");
    Serial.print(rainValue < HEAVY_RAIN_THRESHOLD ? "HUJAN" : "KERING");
    Serial.print(" | Relay Active: ");
    Serial.print(isRelayActive ? "YA" : "TIDAK");
    if (isRelayActive) {
      Serial.print(" | Current Relay: ");
      Serial.print(currentRelayIndex);
      Serial.print(" | Phase: ");
      Serial.print(isRelayOpening ? "BUKA" : "TUTUP");
    }
    Serial.println();
    lastSerialPrint = millis();
  }
  */

  int button = readButton();
  
  // Handle masuk ke manual time mode
  if (button == -2) {
    // Sudah ditangani di readButton(), tidak perlu aksi tambahan
    button = -1; // Reset button untuk mencegah aksi lain
  }
  
  handleMenuNavigation(button);
  
  // Hanya jalankan relay logic jika tidak dalam manual time mode atau konfirmasi
  if (!manualTimeMode && !manualTimeConfirm) {
    handleRelayLogic();
  }
  
  displayMenu();
  delay(200);
}
