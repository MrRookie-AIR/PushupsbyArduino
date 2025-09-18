#include <TM1637Display.h>

// Пины
#define CLK_PIN      9
#define DIO_PIN      8
#define TRIG_PIN    11
#define ECHO_PIN    10
#define BUZZER_PIN  12

// Константы
const int   AUTO_REPS         = 20;
const int   BEEP_FREQ_PUSHUPS = 20;
const long  RESTART_DELAY     = 500;
const long  START_DELAY_MS    = 5000;

// Дисплей
TM1637Display display(CLK_PIN, DIO_PIN);

// Состояние тренировки
bool          seriesActive      = false;
bool          startDelayActive  = false;
unsigned long startDelayStart   = 0;
int           requiredReps      = AUTO_REPS;
int           initialReps       = AUTO_REPS;
int           repsDone          = 0;

// Обнаружение отжиманий
bool          downDetected      = false;
bool          upDetected        = false;
unsigned long downTime          = 0;
unsigned long lastRepTime       = 0;
unsigned long lastPenaltyCheck  = 0;

// Таймеры
unsigned long seriesEnd         = 0;
unsigned long lastCommandTime   = 0;

// Текущая команда
int           currentUserId     = 0;
unsigned long restTimeMs        = 5000;

// Звук
void beep(int times=1, int freq=2000, int dur=100, int pauseMs=100) {
  for (int i = 0; i < times; i++) {
    tone(BUZZER_PIN, freq, dur);
    delay(dur + pauseMs);
  }
}

// Чтение команды из Python
void readSerialCommand() {
  if (!Serial.available()) return;
  String msg = Serial.readStringUntil('\n');
  msg.trim();
  if (msg.length() == 0) return;

  int p1 = msg.indexOf('|');
  int p2 = msg.indexOf('|', p1 + 1);
  if (p1 < 0 || p2 < 0) {
    Serial.print("ERR|BAD_FORMAT|"); Serial.println(msg);
    return;
  }

  currentUserId = msg.substring(0, p1).toInt();
  requiredReps  = msg.substring(p1 + 1, p2).toInt();
  initialReps   = requiredReps;
  restTimeMs    = (unsigned long)msg.substring(p2 + 1).toInt() * 1000UL;

  repsDone         = 0;
  downDetected     = false;
  upDetected       = false;
  seriesActive     = false;
  startDelayActive = true;
  startDelayStart  = millis();

  display.showNumberDec(requiredReps, false);

  beep(3, 1500, 100, 100);
  beep(1, 1000, 800, 0);

  lastCommandTime = millis();
  Serial.println("ACK|START|" + msg);
}

void setup() {
  Serial.begin(9600);
  pinMode(TRIG_PIN,   OUTPUT);
  pinMode(ECHO_PIN,   INPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  display.setBrightness(5);
  display.showNumberDec(requiredReps, false);

  lastRepTime      = millis();
  lastPenaltyCheck = millis();
  lastCommandTime  = millis();
}

void loop() {
  readSerialCommand();
  unsigned long now = millis();

  // Пауза перед серией
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
      display.showNumberDec(requiredReps, false);
      beep(1, 800, 150);
      startDelayStart = now;
    }

    delay(50);
    return;
  }

  // Основная логика
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

    if (downDetected && now - downTime > 2000) {
      downDetected = upDetected = false;
    }

    if (downDetected && !upDetected && dist >= 20.0 && now - downTime > 500) {
      upDetected = true;
      beep(1, BEEP_FREQ_PUSHUPS);
    }

    if (downDetected && upDetected) {
      repsDone++;
      requiredReps--;
      lastRepTime = now;
      display.showNumberDec(requiredReps, false);
      beep(1, BEEP_FREQ_PUSHUPS);
      downDetected = upDetected = false;

      if (requiredReps <= 0) {
        seriesActive = false;
        seriesEnd    = now;
        String out = String("{\"action\":\"DONE\",")
                     + "\"user_id\":" + currentUserId + ","
                     + "\"count\":"   + repsDone
                     + "}";
        Serial.println(out);
        beep(2, 1000, 200);
      }
    }

    if (seriesActive
        && repsDone > 0
        && requiredReps > 0
        && (now - lastRepTime) > restTimeMs
        && (now - lastPenaltyCheck) > restTimeMs)
    {
      requiredReps++;
      lastPenaltyCheck = now;
      display.showNumberDec(requiredReps, false);
      beep(1, 800, 150);
    }
  }

  delay(50);
}
