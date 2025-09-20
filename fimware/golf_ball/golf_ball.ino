long adc = 0;
int loop_count = 0;

// pins
const uint8_t VIB_PIN = 0;
const uint8_t LED_PIN = 2;
const uint8_t MOSFET_PIN = 3;
const uint8_t ADC_PIN = 4;

// state machine
enum State { ACTIVE,
             SLEEP };
State state = ACTIVE;

// battery-shutdown flag
bool batterySleep = false;

// timing
unsigned long lastMovementMs = 0;
unsigned long lastBlinkMs = 0;  // for the 15 s heartbeat

// vibration change detection
int lastVibState = LOW;

// wake-from-sleep counters
int wakeCount = 0;
unsigned long wakeStartMs = 0;

void setup() {
  delay(100);
  pinMode(VIB_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(MOSFET_PIN, OUTPUT);
  pinMode(ADC_PIN, INPUT);

  digitalWriteFast(LED_PIN, LOW);
  digitalWriteFast(MOSFET_PIN, LOW);

  lastVibState = digitalRead(VIB_PIN);
  lastMovementMs = millis();  // assume movement at power-up
  lastBlinkMs = millis();     // start heartbeat timer now

  // —— Power-up blink 10× fast ——
  for (uint8_t i = 0; i < 10; i++) {
    digitalWriteFast(LED_PIN, HIGH);
    delay(100);
    digitalWriteFast(LED_PIN, LOW);
    delay(100);
  }
}

void loop() {
  unsigned long now = millis();

  // ——— Movement / wake logic ———
  int currVib = digitalRead(VIB_PIN);
  bool movementDetected = (currVib != lastVibState);
  lastVibState = currVib;

  if (movementDetected) {
    lastMovementMs = now;

    if (state == SLEEP && !batterySleep) {
      if (wakeCount == 0) {
        wakeStartMs = now;
        wakeCount = 1;
      } else {
        if (now - wakeStartMs <= 3000) {
          wakeCount++;
        } else {
          wakeStartMs = now;
          wakeCount = 1;
        }
      }

      if (wakeCount >= 10) {
        // —— Wake-up blink 10× fast ——
        for (uint8_t j = 0; j < 10; j++) {
          digitalWriteFast(LED_PIN, HIGH);
          delay(100);
          digitalWriteFast(LED_PIN, LOW);
          delay(100);
        }
        state = ACTIVE;
        wakeCount = 0;
        // reset heartbeat timer on wake:
        lastBlinkMs = now;
      }
    }
  }

  // ——— ADC averaging & battery logic ———
  adc += analogRead(ADC_PIN);
  if (loop_count++ >= 1000) {
    adc /= 1000;

    if (adc < 614) {
      // critical low: go to sleep until voltage recovers
      batterySleep = true;
      state = SLEEP;
      digitalWriteFast(MOSFET_PIN, LOW);  // ensure MOSFET off

      // flash LED 20× fast to indicate “shut off” state:
      for (uint8_t j = 0; j < 20; j++) {
        digitalWriteFast(LED_PIN, HIGH);
        delay(100);
        digitalWriteFast(LED_PIN, LOW);
        delay(100);
      }
    } else if (batterySleep && adc >= 716) {
      // battery recovered: clear shutdown
      batterySleep = false;
      state = ACTIVE;
      digitalWriteFast(LED_PIN, LOW);
      lastBlinkMs = now;  // restart heartbeat timer
    } else if (!batterySleep) {
      // normal battery-warning indication (<716): LED ON constantly
      if (adc < 716) {
        digitalWriteFast(LED_PIN, HIGH);
      } else {
        digitalWriteFast(LED_PIN, LOW);
      }
    }

    adc = 0;
    loop_count = 0;
  }

  // ——— ACTIVE state: MOSFET pulses, idle-timeout → sleep, heartbeat blink ———
  if (state == ACTIVE) {
    // 1) Do your original 8×12 µs on/off MOSFET pulses
    for (uint8_t i = 0; i < 8; i++) {
      digitalWriteFast(MOSFET_PIN, HIGH);
      delayMicroseconds(12);
      digitalWriteFast(MOSFET_PIN, LOW);
      delayMicroseconds(12);
    }

    // 2) If no movement for ≥ 1 min → blink LED 10× fast & sleep
    if (now - lastMovementMs >= 60000) {
      for (uint8_t i = 0; i < 10; i++) {
        digitalWriteFast(LED_PIN, HIGH);
        delay(100);
        digitalWriteFast(LED_PIN, LOW);
        delay(100);
      }
      digitalWriteFast(MOSFET_PIN, LOW);
      state = SLEEP;
      wakeCount = 0;  // reset wake counter
    } else {
      // 3) While still ACTIVE (and not forced off by battery), do a single quick blink every 15 s
      //    Only blink if the LED is currently LOW (i.e. not in battery-warning ON state)
      //    We do not use delay(100) here to allow IR pulses to continue
      if (now - lastBlinkMs >= 15000) {
        if (digitalRead(LED_PIN) == LOW) {
          digitalWriteFast(LED_PIN, HIGH);
        } else if (now - lastBlinkMs >= 15100) {
          digitalWriteFast(LED_PIN, LOW);
          lastBlinkMs = now;
        }
      }
    }
  } else {
    // SLEEP state: ensure MOSFET off
    digitalWriteFast(MOSFET_PIN, LOW);
  }

  // maintain your original pacing
  delayMicroseconds(550);
}
