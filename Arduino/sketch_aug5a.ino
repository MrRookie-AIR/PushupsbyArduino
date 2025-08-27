#include <TM1637Display.h>

// Пины
#define CLK_PIN     9
#define DIO_PIN     8
#define TRIG_PIN   11
#define ECHO_PIN   10
#define BUZZER_PIN 12

// Константы
const int   AUTO_REPS         = 20;
const int   BEEP_FREQ_PUSHUPS = 20;
const long  RESTART_DELAY     = 500;
const long  START_DELAY_MS    = 5000;

// Дисплей
TM1637Display display(CLK_PIN, DIO_PIN);

// Состояние тренировки
bool          seriesActive     = false;
bool          startDelayActive = false;
unsigned long startDelayStart  = 0;
int           requiredReps     = AUTO_REPS;
int           repsDone         = 0;

// Обнаружение отжиманий
bool          downDetected     = false;
bool          upDetected       = false;
unsigned long downTime         = 0;
unsigned long lastRepTime      = 0;
unsigned long lastPenaltyCheck = 0;

// Таймеры
unsigned long seriesEnd        = 0;
unsigned long lastCommandTime  = 0;

// Текущая команда
int           currentUserId    = 0;
String        currentName      = "";

// ----------------------
// Сегменты для букв/цифр
uint8_t charMap(char c) {
  c = toupper(c);
  switch (c) {
    case '0': return display.encodeDigit(0);
    case '1': return display.encodeDigit(1);
    case '2': return display.encodeDigit(2);
    case '3': return display.encodeDigit(3);
    case '4': return display.encodeDigit(4);
    case '5': return display.encodeDigit(5);
    case '6': return display.encodeDigit(6);
    case '7': return display.encodeDigit(7);
    case '8': return display.encodeDigit(8);
    case '9': return display.encodeDigit(9);
    case 'A': return 0b01110111;
    case 'B': return 0b01111100;
    case 'C': return 0b00111001;
    case 'D': return 0b01011110;
    case 'E': return 0b01111001;
    case 'F': return 0b01110001;
    case 'H': return 0b01110110;
    case 'L': return 0b00111000;
    case 'O': return 0b00111111;
    case 'P': return 0b01110011;
    case 'S': return 0b01101101;
    case 'U': return 0b00111110;
    case 'Y': return 0b01101110;
    case '-': return 0b01000000;
    case ' ': return 0x00;
    default:  return 0x00;
  }
}

void displayName(String name) {
  name.toUpperCase();
  while (name.length() < 4) name += " ";
  uint8_t segs[4];
  for (int i = 0; i < 4; i++) segs[i] = charMap(name.charAt(i));
  display.setSegments(segs);
}

// ----------------------
// Звук
void beep(int times=1, int freq=2000, int dur=100, int pauseMs=100) {
  for (int i = 0; i < times; i++) {
    tone(BUZZER_PIN, freq, dur);
    delay(dur + pauseMs);
  }
}

// ----------------------
// Приём команды из Python
void readSerialCommand() {
  if (!Serial.available()) return;
  String msg = Serial.readStringUntil('\n');
  msg.trim();
  if (msg.length() == 0) return;

  int p1 = msg.indexOf('|');
  int p2 = msg.indexOf('|', p1 + 1);
  if (p1 < 1 || p2 < p1 + 2) {
    Serial.print("ERR|BAD_FORMAT|"); Serial.println(msg);
    return;
  }

  String sUser = msg.substring(0, p1);
  String sName = msg.substring(p1 + 1, p2);
  String sReps = msg.substring(p2 + 1);

  currentUserId   = sUser.toInt();
  currentName     = sName;
  requiredReps    = sReps.toInt();
  repsDone        = 0;
  downDetected    = upDetected = false;
  seriesActive    = false;
  startDelayActive = true;
  startDelayStart  = millis();

  displayName(currentName);
  display.showNumberDec(requiredReps);

  // Сигнализация
  beep(3, 1500, 100, 100);
  beep(1, 1000, 800, 0);

  lastCommandTime = millis();
  Serial.print("ACK|START|"); Serial.println(msg);
}

// ----------------------
void setup() {
  Serial.begin(9600);
  pinMode(TRIG_PIN,   OUTPUT);
  pinMode(ECHO_PIN,   INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  display.setBrightness(5);
  display.showNumberDec(requiredReps);

  lastRepTime      = millis();
  lastPenaltyCheck = millis();
  lastCommandTime  = millis();
}

// ----------------------
void loop() {
  readSerialCommand();
  unsigned long now = millis();

  // Ожидание начала движения
  if (startDelayActive) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long d = pulseIn(ECHO_PIN, HIGH, 30000);
    float dist = d * 0.034 / 2.0;

    if (dist > 0.5 && dist <= 4.0) {
      startDelayActive = false;
      seriesActive     = true;
      lastRepTime      = now;
      downDetected     = true;
      downTime         = now;
      beep(1, BEEP_FREQ_PUSHUPS);
      return;
    }

    if (now - startDelayStart >= START_DELAY_MS) {
      requiredReps++;
      display.showNumberDec(requiredReps);
      beep(1, 800, 150);
      startDelayStart = now;
    }

    delay(50);
    return;
  }

  // Авто-сериалки
  if (!seriesActive && now - seriesEnd > RESTART_DELAY) {
    if (now - lastCommandTime > 10000) {
      currentUserId = 0;
      currentName   = "AUTO";
      requiredReps  = AUTO_REPS;
      repsDone      = 0;
      seriesActive  = true;
      display.showNumberDec(requiredReps);
      Serial.println("🌀 AUTO_START");
    }
    delay(50);
    return;
  }

  // Основная логика серии
  if (seriesActive) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long d = pulseIn(ECHO_PIN, HIGH, 30000);
    float dist = d * 0.034 / 2.0;

    if (!downDetected && dist <= 4.0 && dist > 0.5) {
      downDetected = true;
      downTime     = now;
      lastRepTime  = now;
      beep(1, BEEP_FREQ_PUSHUPS);
    }

    if (downDetected && now - downTime > 10000) {
      downDetected = upDetected = false;
    }

    if (downDetected && !upDetected && dist > 28.0 && dist < 40.0) {
      upDetected = true;
      beep(1, BEEP_FREQ_PUSHUPS);
    }

    if (downDetected && upDetected) {
      repsDone++;
      requiredReps--;
      lastRepTime = now;
      display.showNumberDec(requiredReps);
      beep(1, BEEP_FREQ_PUSHUPS);
      downDetected = upDetected = false;

      if (requiredReps <= 0) {
        seriesActive = false;
        seriesEnd    = now;
        String out = String("{\"action\":\"DONE\",") +
                     "\"user_id\":" + currentUserId + "," +
                     "\"count\":"   + repsDone +
                     "}";
        Serial.println(out);
        beep(2, 1000, 200);
      }
    }

    if (seriesActive
        && repsDone > 0
        && requiredReps > 0
        && (now - lastRepTime) > 5000
        && (now - lastPenaltyCheck) > 5000)
    {
      requiredReps++;
      lastPenaltyCheck = now;
      display.showNumberDec(requiredReps);
      beep(1, 800, 150);
    }
  }

  delay(50);
}