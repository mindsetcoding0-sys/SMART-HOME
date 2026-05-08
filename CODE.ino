#include <SPI.h>
#include <MFRC522.h>
#include <Servo.h>

#define SS_PIN    10
#define RST_PIN   7

MFRC522 rfid(SS_PIN, RST_PIN);
byte allowedUID[4] = {0xC5, 0xF6, 0x19, 0x02};

#define SERVO_PIN   6

#define GREEN_LED   4
#define RED_LED     5
#define BUZZER_PIN  A5

#define YELLOW_LED  3
#define WHITE_LED   2

#define GAS_SENSOR_PIN A0
#define LDR_PIN     A2

#define TRIG_PIN    8
#define ECHO_PIN    9

Servo doorServo;

enum State { IDLE, UNLOCKED, FAIL_ACTION, LOCKED };
State systemState = IDLE;

int failCount = 0;

unsigned long actionStartTime = 0;

bool doorOpenLogged = false;
bool doorCloseLogged = false;

String getTimeStamp() {
  unsigned long t = millis() / 1000;

  int h = t / 3600;
  int m = (t % 3600) / 60;
  int s = t % 60;

  char buffer[20];
  sprintf(buffer, "[%02d:%02d:%02d]", h, m, s);

  return String(buffer);
}

void setup() {

  Serial.begin(9600);

  SPI.begin();
  rfid.PCD_Init();

  doorServo.attach(SERVO_PIN);
  doorServo.write(0);

  pinMode(GREEN_LED, OUTPUT);
  pinMode(RED_LED, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(YELLOW_LED, OUTPUT);
  pinMode(WHITE_LED, OUTPUT);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  digitalWrite(GREEN_LED, HIGH);
  digitalWrite(RED_LED, HIGH);
  digitalWrite(BUZZER_PIN, LOW);

  digitalWrite(YELLOW_LED, LOW);
  digitalWrite(WHITE_LED, LOW);

  Serial.println("SYSTEM READY");
}

void loop() {

  unsigned long currentMillis = millis();

  // ================= LDR =================

  int ldrValue = analogRead(LDR_PIN);

static bool lastLightState = false;

// DARK
if (ldrValue < 500) {

  digitalWrite(WHITE_LED, HIGH);

  // print once only
  if (!lastLightState) {

    Serial.print(getTimeStamp());
    Serial.println(" LIGHT EVENT | Condition: DARK | White LED ON");

    lastLightState = true;
  }
}

// LIGHT
else {

  digitalWrite(WHITE_LED, LOW);

  // print once only
  if (lastLightState) {

    Serial.print(getTimeStamp());
    Serial.println(" LIGHT EVENT | Condition: BRIGHT | White LED OFF");

    lastLightState = false;
  }
}

// // ================= GAS SENSOR =================

// int gasValue = analogRead(GAS_SENSOR_PIN);

// static bool gasDetected = false;

// // GAS DETECTED
// if (gasValue > 400) {

//   if (!gasDetected) {

//     Serial.print(getTimeStamp());
//     Serial.print(" GAS EVENT | Gas Detected | Sensor Value: ");
//     Serial.println(gasValue);

//     gasDetected = true;
//   }
// }

// // AIR NORMAL
// else {

//   if (gasDetected) {

//     Serial.print(getTimeStamp());
//     Serial.println(" GAS EVENT | Air Normal");

//     gasDetected = false;
//   }
// }

  // ================= ULTRASONIC =================

  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);

  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);

  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH, 20000);

  float distance = duration * 0.034 / 2.0;

  if (distance > 0 && distance <= 10) {

    digitalWrite(YELLOW_LED, HIGH);

    Serial.print("MOTION EVENT | Ultrasonic Sensor | Distance: ");
    Serial.print(distance);
    Serial.println("cm | Yellow LED ON");

  }
  else {

    digitalWrite(YELLOW_LED, LOW);

  }

  // ================= LOCKOUT =================

  static unsigned long lockStart = 0;
  static int lastPrintedSec = -1;

  if (systemState == LOCKED) {

    digitalWrite(RED_LED, LOW);

    if (lockStart == 0) {

      lockStart = currentMillis;
      lastPrintedSec = -1;

    }

    unsigned long elapsed = currentMillis - lockStart;

    int remaining = 120 - (elapsed / 1000);

    if (remaining <= 0) {

      failCount = 0;

      systemState = IDLE;

      lockStart = 0;

      digitalWrite(RED_LED, HIGH);

      Serial.println("3 ATTEMPTS RESET - YOU CAN TRY AGAIN");

      return;
    }

    // PRINT ONLY ONCE EACH SECOND
    if (remaining != lastPrintedSec) {

      lastPrintedSec = remaining;

      Serial.print("LOCKOUT TIMER RFID | ");
      Serial.print(remaining);
      Serial.println(" SEC LEFT");

    }

    return;
  }

  // ================= RFID =================

  if (systemState == IDLE) {

    digitalWrite(GREEN_LED, HIGH);
    digitalWrite(RED_LED, HIGH);

    if (rfid.PICC_IsNewCardPresent() &&
        rfid.PICC_ReadCardSerial()) {

      delay(50);

      bool access = true;

      for (byte i = 0; i < 4; i++) {

        if (rfid.uid.uidByte[i] != allowedUID[i]) {

          access = false;

        }
      }

      rfid.PICC_HaltA();

      actionStartTime = currentMillis;

      doorOpenLogged = false;
      doorCloseLogged = false;

      // ================= SUCCESS =================

      if (access) {

        Serial.print(getTimeStamp());
        Serial.println(" ACCESS GRANTED | RFID | Green LED ON");

        digitalWrite(GREEN_LED, LOW);
        digitalWrite(RED_LED, HIGH);

        doorServo.write(180);

        systemState = UNLOCKED;
      }

      // ================= FAIL =================

      else {

        failCount++;

        digitalWrite(GREEN_LED, HIGH);

        digitalWrite(RED_LED, LOW);

        // BUZZER 1 SEC
        digitalWrite(BUZZER_PIN, HIGH);
        delay(1000);
        digitalWrite(BUZZER_PIN, LOW);

        // FAIL 1
        if (failCount == 1) {

          Serial.println("FAIL 1/3 | RED LED 3 SEC + BUZZER 1 SEC");

          delay(2000);

          digitalWrite(RED_LED, HIGH);
        }

        // FAIL 2
        else if (failCount == 2) {

          Serial.println("FAIL 2/3 | RED LED 5 SEC + BUZZER 1 SEC");

          delay(4000);

          digitalWrite(RED_LED, HIGH);
        }

        // FAIL 3
        else if (failCount >= 3) {

          Serial.println("FAIL 3/3 | LOCKOUT 2 MIN START THE RFID");

          systemState = LOCKED;

          lockStart = 0;

          delay(100);
        }
      }
    }
  }

  // ================= DOOR =================

  if (systemState == UNLOCKED) {

    if (!doorOpenLogged) {

      Serial.print(getTimeStamp());
      Serial.println(" DOOR EVENT | UNLOCKED | MOTOR ON");

      doorOpenLogged = true;
    }

    if (currentMillis - actionStartTime >= 3000) {

      doorServo.write(0);

      digitalWrite(GREEN_LED, HIGH);

      Serial.print(getTimeStamp());
      Serial.println(" DOOR EVENT | LOCKED | MOTOR OFF");

      systemState = IDLE;
    }
  }
}
