#include <TM1637Display.h>

// ===== Конфигурация =====
#define USE_DISPLAY       true
#define CLK_PIN           2
#define DIO_PIN           3
#define TRIG_PIN          4
#define ECHO_PIN          5
#define BUZZER_PIN        6
#define BUTTON_PIN        7

// Для удалённого режима
const unsigned long START_DELAY_MS = 5000;

TM1637Display display(CLK_PIN, DIO_PIN);

// ===== Состояния автомата =====
enum State { SELECT_REPS, SELECT_REST, TRAINING, RESTING, DONE };
State currentState = SELECT_REPS;

// ===== Локальные параметры тренировки =====
int totalReps           = 0;
int restBetweenReps     = 0;
const int restBetweenSets = 10;
const int setSize         = 20;

// ===== Локальные счётчики =====
int repsLeft            = 0;
int realPushupsDone     = 0;
int currentSetPushups   = 0;

// ===== Локальный таймер =====
unsigned long lastButtonPress   = 0;
unsigned long lastRepTime       = 0;
unsigned long restStartTime     = 0;
int          lastDisplayedSecond = -9999;

// ===== Детектор отжиманий =====
bool downDetected      = false;
bool upDetected        = false;
unsigned long downTime = 0;

// ===== Удалённый режим =====
bool remoteMode            = false;
bool seriesActive          = false;
bool startDelayActive      = false;
unsigned long startDelayStart;
unsigned long restTimeMs;
int           requiredReps;
unsigned long lastPenaltyCheck;
unsigned long lastActiveSend = 0;
int           repsDone;
int           currentUserId;

// ===== Кнопка локального режима =====
bool lastButtonState = HIGH;
bool isButtonJustPressed() {
  bool st = digitalRead(BUTTON_PIN);
  bool pressed = (lastButtonState == HIGH && st == LOW);
  lastButtonState = st;
  return pressed;
}

// ===== Звук =====
void beep(int times = 1, int freq = 2000, int dur = 100, int pauseMs = 100) {
  for (int i = 0; i < times; i++) {
    tone(BUZZER_PIN, freq, dur);
    delay(dur + pauseMs);
  }
}

// ===== Мигаем дисплеем =====
void blinkDisplay(int times, int value) {
  if (!USE_DISPLAY) return;
  for (int i = 0; i < times; i++) {
    display.clear();
    delay(200);
    display.showNumberDec(value, false);
    delay(200);
  }
}

// ===== Вывод состояния в Serial =====
void printStateName(State s) {
  switch (s) {
    case SELECT_REPS: Serial.print("SELECT_REPS"); break;
    case SELECT_REST: Serial.print("SELECT_REST"); break;
    case TRAINING:    Serial.print("TRAINING");    break;
    case RESTING:     Serial.print("RESTING");     break;
    case DONE:        Serial.print("DONE");        break;
  }
}

// ===== Чтение удалённой команды =====
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

  currentUserId    = msg.substring(0, p1).toInt();
  requiredReps     = msg.substring(p1 + 1, p2).toInt();
  restTimeMs       = (unsigned long)msg.substring(p2 + 1).toInt() * 1000UL;
  repsDone         = 0;
  seriesActive     = false;
  startDelayActive = true;
  startDelayStart  = millis();
  lastPenaltyCheck = millis();

  if (USE_DISPLAY) {
    display.clear();
    display.showNumberDec(requiredReps, false);
  }

  beep(3, 1500, 100, 100);
  beep(1, 1000, 800, 0);
  Serial.print("ACK|START|"); Serial.println(msg);

  remoteMode = true;
}

// ===== SETUP =====
void setup() {
  pinMode(TRIG_PIN,    OUTPUT);
  pinMode(ECHO_PIN,    INPUT);
  pinMode(BUZZER_PIN,  OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (USE_DISPLAY) {
    display.setBrightness(5);
    display.clear();
    display.showNumberDec(0, false);
  }

  Serial.begin(9600);
  Serial.println("=== SYSTEM START ===");

  lastRepTime = millis();
}

// ===== LOOP =====
void loop() {
  unsigned long now = millis();

  // 1) Всегда читаем удалённые команды
  readSerialCommand();

  // 2) Удалённый режим: стартовая задержка и серия
  if (remoteMode) {
    // 2.1) Пауза перед стартом
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
        beep(1, 2000, 100);
      }
      else if (now - startDelayStart >= START_DELAY_MS) {
        requiredReps++;
        if (USE_DISPLAY) {
          display.clear();
          display.showNumberDec(requiredReps, false);
        }
        beep(1, 800, 150);
        startDelayStart = now;
      }

      delay(50);
      return;
    }

    // 2.2) Основная логика отжиманий
    if (seriesActive) {
      digitalWrite(TRIG_PIN, LOW);
      delayMicroseconds(2);
      digitalWrite(TRIG_PIN, HIGH);
      delayMicroseconds(10);
      digitalWrite(TRIG_PIN, LOW);

      long d = pulseIn(ECHO_PIN, HIGH, 30000);
      float dist = d * 0.034 / 2.0;
      // Отправка активности каждые 5 секунд
      if (now - lastActiveSend > 5000) {
        lastActiveSend = now;
        Serial.print("{\"action\":\"ACTIVE\",\"user_id\":");
        Serial.print(currentUserId);
        Serial.println("}");
      }

      if (!downDetected && dist > 0.5 && dist <= 4.0) {
        downDetected = true;
        downTime     = now;
        lastRepTime  = now;
        beep(1, 2000, 100);
      }
      if (downDetected && now - downTime > 2000) {
        downDetected = upDetected = false;
      }
      if (downDetected && !upDetected && dist >= 20.0 && now - downTime > 500) {
        upDetected = true;
        beep(1, 2000, 100);
      }
      if (downDetected && upDetected) {
        repsDone++;
        requiredReps--;
        lastRepTime = now;
        if (USE_DISPLAY) {
          display.clear();
          display.showNumberDec(requiredReps, false);
        }
        beep(1, 2000, 100);
        downDetected = upDetected = false;

        if (requiredReps <= 0) {
          seriesActive = false;
          remoteMode   = false;
          Serial.print("{\"action\":\"DONE\",\"user_id\":");
          Serial.print(currentUserId);
          Serial.print(",\"count\":");
          Serial.print(repsDone);
          Serial.println("}");
          beep(2, 1000, 200);
          return;
        }
      }
      if (seriesActive
          && repsDone > 0
          && requiredReps > 0
          && (now - lastRepTime) > restTimeMs
          && (now - lastPenaltyCheck) > restTimeMs) {
        requiredReps++;
        lastPenaltyCheck = now;
        if (USE_DISPLAY) {
          display.clear();
          display.showNumberDec(requiredReps, false);
        }
        beep(1, 800, 150);
      }
    }

    delay(50);
    return;
  }

  // ============ Локальный режим ============

  // 3) SELECT_REPS
  if (currentState == SELECT_REPS) {
    if (isButtonJustPressed()) {
      totalReps += 10;
      if (USE_DISPLAY) {
        display.clear();
        display.showNumberDec(totalReps, false);
      }
      lastButtonPress = now;
      Serial.print("Set totalReps -> "); Serial.println(totalReps);
    }
    else if (totalReps > 0 && now - lastButtonPress > 3000) {
      blinkDisplay(2, totalReps);
      currentState    = SELECT_REST;
      lastButtonPress = now;
      if (USE_DISPLAY) {
        display.clear();
        display.showNumberDec(0, false);
      }
      Serial.println("Switch to SELECT_REST");
    }
    return;
  }

  // 4) SELECT_REST
  if (currentState == SELECT_REST) {
    if (isButtonJustPressed()) {
      restBetweenReps++;
      if (USE_DISPLAY) {
        display.clear();
        display.showNumberDec(restBetweenReps, false);
      }
      lastButtonPress = now;
      Serial.print("Set restBetweenReps -> "); Serial.println(restBetweenReps);
    }
    else if (restBetweenReps > 0 && now - lastButtonPress > 3000) {
      blinkDisplay(3, restBetweenReps);
      currentState      = TRAINING;
      repsLeft          = totalReps;
      realPushupsDone   = 0;
      currentSetPushups = 0;
      lastRepTime       = now;
      if (USE_DISPLAY) {
        display.clear();
        display.showNumberDec(repsLeft, false);
      }
      Serial.println("Switch to TRAINING");
    }
    return;
  }

  // 5) TRAINING (локальный)
  if (currentState == TRAINING) {
    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    long duration = pulseIn(ECHO_PIN, HIGH, 30000);
    float dist    = duration * 0.034 / 2.0;

    if (!downDetected && dist > 0.5 && dist <= 4.0) {
      downDetected = true;
      downTime     = now;
      lastRepTime  = now;
      beep(1, 2000, 100);
    }
    if (downDetected && now - downTime > 2000) {
      downDetected = upDetected = false;
    }
    if (downDetected && !upDetected && dist >= 20.0 && now - downTime > 500) {
      upDetected = true;
      beep(1, 2000, 100);
    }
    if (downDetected && upDetected) {
      realPushupsDone++;
      currentSetPushups++;
      repsLeft--;
      if (USE_DISPLAY) {
        display.clear();
        display.showNumberDec(repsLeft, false);
      }
      beep(1, 2000, 100);
      downDetected = upDetected = false;
      lastRepTime = now;

      if (currentSetPushups >= setSize && repsLeft > 0) {
        restStartTime       = now;
        lastDisplayedSecond = -9999;
        currentState        = RESTING;
        Serial.println("ENTER LOCAL REST");
      }
      else if (repsLeft <= 0) {
        currentState = DONE;
        Serial.println("ENTER LOCAL DONE");
      }
    }
    if ((now - lastRepTime) > restBetweenReps * 1000UL && repsLeft > 0) {
      repsLeft++;
      if (USE_DISPLAY) {
        display.clear();
        display.showNumberDec(repsLeft, false);
      }
      beep(1, 800, 150);
      lastRepTime = now;
    }
    return;
  }

  // 6) RESTING (локальный)
  if (currentState == RESTING) {
    unsigned long elapsed = now - restStartTime;
    int secondsLeft = restBetweenSets - (elapsed / 1000);

    if (lastDisplayedSecond == -9999) {
      Serial.print("LOCAL REST START, secLeft=");
      Serial.println(secondsLeft);
    }
    if (secondsLeft != lastDisplayedSecond && secondsLeft >= 0) {
      if (USE_DISPLAY) {
        display.clear();
        display.showNumberDec(secondsLeft, false);
      }
      lastDisplayedSecond = secondsLeft;
      Serial.print("LOCAL RESTING: "); Serial.print(secondsLeft); Serial.println(" sec");
    }
    if (elapsed >= restBetweenSets * 1000UL) {
      currentSetPushups = 0;
      currentState      = TRAINING;
      if (USE_DISPLAY) {
        display.clear();
        display.showNumberDec(repsLeft, false);
      }
      Serial.println("LOCAL REST END");
    }
    return;
  }

  // 7) DONE (локальный)
  if (currentState == DONE) {
    // сегменты для «dOnE»
    const uint8_t segs_DOnE[4] = {
      0b1001111, // d
      0b0001110, // o
      0b0100001, // n
      0b0101111  // E
    };
    unsigned long t0 = millis();
    while (millis() - t0 < 3000) {
      if (USE_DISPLAY) display.setSegments(segs_DOnE, 4);
      delay(500);
      if (USE_DISPLAY) display.clear();
      delay(500);
    }
    // сброс
    totalReps         = 0;
    restBetweenReps   = 0;
    realPushupsDone   = 0;
    currentSetPushups = 0;
    currentState      = SELECT_REPS;
    if (USE_DISPLAY) {
      display.clear();
      display.showNumberDec(0, false);
    }
    Serial.println("RESET -> SELECT_REPS");
    return;
  }

  // В конце цикла небольшая задержка
  delay(20);
}
