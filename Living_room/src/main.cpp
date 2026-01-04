#include <Keypad.h>
#include <ESP32Servo.h>
#include <LiquidCrystal_I2C.h>
#include "DHT.h" // Thêm lại thư viện DHT

// --- PIN CONFIG ---
#define DHTPIN          13  // Chân dữ liệu DHT22
#define DHTTYPE         DHT22
#define POT_DUST        34  
#define LDR_PIN         35  
#define PIR_PIN         39  
#define SWITCH_MODE     18
#define BTN_HEATER      36 
#define BTN_TV          37
#define BTN_FAN         38
#define SERVO_DOOR      16
#define SERVO_CURTAIN   17

#define LED_LIGHT       2   
#define LO_SUOI         19  
#define LED_TV          23  
#define QUAT            4    

#define LED_AIR_WHITE   0
#define LED_AIR_GREEN   15
#define LED_AIR_YELLOW  2   
#define LED_AIR_RED     5   

// --- KHỞI TẠO ĐỐI TƯỢNG ---
DHT dht(DHTPIN, DHTTYPE);
Servo doorServo, curtain;
LiquidCrystal_I2C lcd(0x27, 16, 2);

const byte ROWS = 4, COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'}, {'4','5','6','B'},
  {'7','8','9','C'}, {'*','0','#','D'}
};
byte rowPins[ROWS] = {32, 33, 25, 26}; 
byte colPins[COLS] = {13, 12, 14, 27}; 
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// --- BIẾN ĐIỀU KHIỂN ---
String password = "1234";
String inputPass = "";
bool authenticated = false;
int heaterLevel = 0, fanLevel = 0;
bool tvStatus = false, manualMode = false;
bool lastBtnH = HIGH, lastBtnTV = HIGH, lastBtnF = HIGH;
float roomTemp = 0, roomHum = 0;

// --- FUNCTION PROTOTYPES ---
void handleAirFilter();
void forceOffInterior();
void handleKeypad();
void showLockScreen();
void processSmartHome();
void handleLight(bool hasPerson);
void handleCurtain();
void handleClimate(bool hasPerson);
void handleTV(bool hasPerson);
void updateLCD(bool hasPerson);

void setup() {
  Serial.begin(115200);
  dht.begin(); // Khởi động DHT

  pinMode(LO_SUOI, OUTPUT); pinMode(QUAT, OUTPUT); 
  pinMode(LED_TV, OUTPUT); pinMode(LED_LIGHT, OUTPUT);
  pinMode(LED_AIR_WHITE, OUTPUT); pinMode(LED_AIR_GREEN, OUTPUT);
  pinMode(LED_AIR_YELLOW, OUTPUT); pinMode(LED_AIR_RED, OUTPUT);

  digitalWrite(LED_TV, LOW); digitalWrite(LED_LIGHT, LOW);

  // Cấu hình PWM cho lò sưởi và quạt
  ledcSetup(0, 5000, 8); // Channel 0, 5kHz, 8-bit resolution
  ledcAttachPin(LO_SUOI, 0);
  ledcSetup(1, 5000, 8); // Channel 1, 5kHz, 8-bit resolution
  ledcAttachPin(QUAT, 1);
  ledcWrite(0, 0); ledcWrite(1, 0);

  pinMode(SWITCH_MODE, INPUT_PULLUP);
  pinMode(BTN_HEATER, INPUT_PULLUP);
  pinMode(BTN_TV, INPUT_PULLUP);
  pinMode(BTN_FAN, INPUT_PULLUP);
  pinMode(PIR_PIN, INPUT);

  ESP32PWM::allocateTimer(0);
  doorServo.attach(SERVO_DOOR, 500, 2400);
  curtain.attach(SERVO_CURTAIN, 500, 2400);
  
  doorServo.write(0);   
  curtain.write(170);   
  
  lcd.init();
  lcd.backlight();
}

void loop() {
  handleAirFilter();
  
  // Đọc nhiệt độ định kỳ
  roomTemp = dht.readTemperature();
  roomHum = dht.readHumidity();

  if (!authenticated) {
    forceOffInterior(); 
    handleKeypad();
    showLockScreen();
  } 
  else {
    processSmartHome();
    char key = keypad.getKey();
    if (key == 'D') { 
      authenticated = false;
      inputPass = "";
      doorServo.write(0);
      lcd.clear();
      forceOffInterior();
    }
  }
}

void handleAirFilter() {
  int dust = analogRead(POT_DUST);
  bool w = (dust < 1000), g = (dust >= 1000 && dust < 2000);
  bool y = (dust >= 2000 && dust < 4000), r = (dust >= 4000);

  digitalWrite(LED_AIR_WHITE, w);
  digitalWrite(LED_AIR_GREEN, g);
  digitalWrite(LED_AIR_YELLOW, y);
  digitalWrite(LED_AIR_RED, r);
}

void forceOffInterior() {
  ledcWrite(0, 0); ledcWrite(1, 0); // Channel 0 = LO_SUOI, Channel 1 = QUAT
  digitalWrite(LED_TV, LOW); digitalWrite(LED_LIGHT, LOW);
}

void processSmartHome() {
  manualMode = (digitalRead(SWITCH_MODE) == LOW);
  bool hasPerson = digitalRead(PIR_PIN);
  
  handleLight(hasPerson);
  handleCurtain();
  handleClimate(hasPerson); 
  handleTV(hasPerson);      
  updateLCD(hasPerson);
}

void handleClimate(bool hasPerson) {
  if (!hasPerson) { ledcWrite(LO_SUOI, 0); ledcWrite(QUAT, 0); return; }
  
  if (manualMode) {
    // Điều khiển thủ công bằng nút nhấn
    if (digitalRead(BTN_HEATER) == LOW && lastBtnH == HIGH) {
      delay(50); heaterLevel = (heaterLevel + 1) % 4;
      ledcWrite(0, heaterLevel * 85); // Channel 0 = LO_SUOI
    }
    lastBtnH = digitalRead(BTN_HEATER);
    
    if (digitalRead(BTN_FAN) == LOW && lastBtnF == HIGH) {
      delay(50); fanLevel = (fanLevel + 1) % 4;
      ledcWrite(1, fanLevel * 85); // Channel 1 = QUAT
    }
    lastBtnF = digitalRead(BTN_FAN);
  } else {
    // CHẾ ĐỘ TỰ ĐỘNG DỰA TRÊN DHT
    if (isnan(roomTemp)) return; 
    if (roomTemp < 20) { ledcWrite(0, 255); ledcWrite(1, 0); } // Channel 0 = LO_SUOI, Channel 1 = QUAT
    else if (roomTemp > 28) { ledcWrite(0, 0); ledcWrite(1, 255); }
    else { ledcWrite(0, 0); ledcWrite(1, 0); }
  }
}

void updateLCD(bool hasPerson) {
  lcd.setCursor(0, 0);
  lcd.print("T:"); lcd.print((int)roomTemp); lcd.print("C ");
  lcd.print("H:"); lcd.print((int)roomHum); lcd.print("%");
  lcd.setCursor(0, 1);
  lcd.print(manualMode ? "MANUAL " : "AUTO   ");
  lcd.print(hasPerson ? "P:ON " : "P:OFF");
}

// ... (Các hàm handleKeypad, handleTV, handleLight, handleCurtain, showLockScreen giữ nguyên)
void handleKeypad() {
  char key = keypad.getKey();
  if (!key) return;
  if (key == '#') {
    if (inputPass == password) { authenticated = true; doorServo.write(90); lcd.clear(); }
    else { inputPass = ""; lcd.setCursor(0,1); lcd.print("SAI MAT KHAU!   "); delay(1000); }
  } else if (key == '*') inputPass = "";
  else if (inputPass.length() < 4) inputPass += key;
}

void handleTV(bool hasPerson) {
  if (manualMode) {
    bool btn = digitalRead(BTN_TV);
    if (btn == LOW && lastBtnTV == HIGH) { delay(50); tvStatus = !tvStatus; }
    lastBtnTV = btn;
  } else tvStatus = hasPerson;
  digitalWrite(LED_TV, tvStatus);
}

void handleLight(bool hasPerson) {
  digitalWrite(LED_LIGHT, (analogRead(LDR_PIN) > 2000 && hasPerson));
}

void handleCurtain() {
  int lux = analogRead(LDR_PIN);
  curtain.write((lux > 3500 || lux < 500) ? 170 : 10);
}

void showLockScreen() {
  lcd.setCursor(0, 0); lcd.print("CUA DANG KHOA ");
  lcd.setCursor(0, 1); lcd.print("PASS: ");
  for(int i=0; i<inputPass.length(); i++) lcd.print("*");
}