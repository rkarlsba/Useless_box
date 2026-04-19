/*
 * vim:ts=4:sw=4:sts=4:et:ai:si:fdm=marker
 *
 * Useless Box - Arduino
 *
 * A "useless machine" that turns itself off when switched on,
 * performing various entertaining animations each time.
 *
 * Play modes:
 *   ROUND_ROBIN        - Cycle through a fixed sequence, repeating forever
 *   CYCLE_THEN_RANDOM  - One full cycle through the sequence, then random
 *   ALL_RANDOM         - Always pick a random animation
 *
 * Relay support is runtime-configurable via the serial console and
 * persisted to EEPROM. When enabled, servo power is cut during setup
 * to prevent twitching, then restored once PWM signals are stable.
 */

#include <Servo.h>
#include <EEPROM.h>

// ──────────────────── Pin assignments ────────────────────

const int PIN_RELAY         = D0;
const int PIN_FINGER_SERVO  = D1;
const int PIN_DOOR_SERVO    = D2;
const int PIN_SWITCH        = D3;
const int PIN_LED           = D4;

const int INVERSE_LED       = true;

// ──────────────────── EEPROM layout ────────────────────

#define EEPROM_MAGIC        0xAB
#define EEPROM_ADDR_MAGIC   0
#define EEPROM_ADDR_MODE    1
#define EEPROM_ADDR_RELAY   2
#define EEPROM_SIZE         3

// ──────────────────── Servo positions ────────────────────

const int DOOR_CLOSED       = 80;
const int DOOR_OPEN         = 155;
const int FINGER_HIDDEN     = 0;
const int FINGER_EXTENDED   = 65;

// ──────────────────── Serial / console ────────────────────

const int SERIAL_SPEED      = 9600;
#define CON_BUF_SIZE        63          // max input line length

// ──────────────────── Play modes ────────────────────

enum PlayMode {
    ROUND_ROBIN,           // Cycle sequence in order, repeat forever
    CYCLE_THEN_RANDOM,     // One full cycle in order, then random picks
    ALL_RANDOM             // Always pick a random move
};

// ──────────────────── Globals ────────────────────

PlayMode playMode           = ROUND_ROBIN;
bool     useRelay           = false;

Servo doorServo;
Servo fingerServo;

int  sequenceIndex          = 0;
bool firstCycleComplete     = false;

char conBuf[CON_BUF_SIZE + 1];
int  conLen                 = 0;

// ──────────────────── Move declarations ────────────────────

void moveSimpleClose();
void moveHesitantClose();
void moveCrazyDoor();
void moveSlow();
void moveSerious();
void moveTrollClose();
void moveMatrix();
void moveSneak();

typedef void (*MoveFunction)();

const MoveFunction allMoves[] = {
    moveSimpleClose,    // 0
    moveHesitantClose,  // 1
    moveCrazyDoor,      // 2
    moveSlow,           // 3
    moveSerious,        // 4
    moveTrollClose,     // 5
    moveMatrix,         // 6
    moveSneak           // 7
};

const int NUM_MOVES      = sizeof(allMoves) / sizeof(allMoves[0]);

// Fixed round-robin sequence (preserves original ordering with repeats)
const int roundRobinSequence[] = { 0, 0, 1, 2, 3, 4, 5, 0, 6, 7 };
const int SEQUENCE_LENGTH      = sizeof(roundRobinSequence) / sizeof(roundRobinSequence[0]);

// ──────────────────── EEPROM ────────────────────

void loadSettings() {
    if (EEPROM.read(EEPROM_ADDR_MAGIC) == EEPROM_MAGIC) {
        uint8_t m = EEPROM.read(EEPROM_ADDR_MODE);
        if (m <= ALL_RANDOM) {
            playMode = (PlayMode)m;
        }
        useRelay = (EEPROM.read(EEPROM_ADDR_RELAY) == 1);
    }
}

void saveSettings() {
    EEPROM.write(EEPROM_ADDR_MAGIC, EEPROM_MAGIC);
    EEPROM.write(EEPROM_ADDR_MODE,  (uint8_t)playMode);
    EEPROM.write(EEPROM_ADDR_RELAY, useRelay ? 1 : 0);
    EEPROM.commit();
}

// ──────────────────── Helpers ────────────────────

// Forward declaration — processSerialByte is defined after console section
// but is needed inside consoleDelay which is used by sweepServo.
void processSerialByte(char c);

// Delay that keeps draining the serial RX buffer so the console stays
// responsive even while an animation is running. Characters captured by the
// UART interrupt are processed here between every servo step.
void consoleDelay(unsigned long ms) {
    unsigned long start = millis();
    while (millis() - start < ms) {
        while (Serial.available()) {
            processSerialByte((char)Serial.read());
        }
        yield();
    }
}

// Sweep a servo smoothly from one position to another.
void sweepServo(Servo &servo, int fromPos, int toPos, int step, int delayMs) {
    if (fromPos < toPos) {
        for (int pos = fromPos; pos < toPos; pos += step) {
            servo.write(pos);
            consoleDelay(delayMs);
        }
    } else {
        for (int pos = fromPos; pos >= toPos; pos -= step) {
            servo.write(pos);
            consoleDelay(delayMs);
        }
    }
}

// Blink the status LED a number of times. Active low.
void blinkLed(int count, int onMs, int offMs) {
    for (int i = 0; i < count; i++) {
        digitalWrite(PIN_LED, INVERSE_LED ? LOW : HIGH);
        consoleDelay(onMs);
        digitalWrite(PIN_LED, INVERSE_LED ? HIGH : LOW);
        if (i < count - 1) {
            consoleDelay(offMs);
        }
    }
}

// Pick the next move index based on the current play mode.
int pickNextMove() {
    switch (playMode) {
        case ROUND_ROBIN: {
            int idx = roundRobinSequence[sequenceIndex];
            sequenceIndex = (sequenceIndex + 1) % SEQUENCE_LENGTH;
            return idx;
        }
        case CYCLE_THEN_RANDOM:
            if (!firstCycleComplete) {
                int idx = roundRobinSequence[sequenceIndex++];
                if (sequenceIndex >= SEQUENCE_LENGTH) {
                    firstCycleComplete = true;
                }
                return idx;
            }
            return random(NUM_MOVES);
        case ALL_RANDOM:
            return random(NUM_MOVES);
    }
    return 0;
}

// ──────────────────── Console ────────────────────

void printPrompt() {
    Serial.print(F("> "));
}

void printHelp() {
    Serial.println();
    Serial.println(F("=== Useless Box ==="));
    Serial.print(F("  Play mode : "));
    switch (playMode) {
        case ROUND_ROBIN:       Serial.println(F("0 - round-robin"));       break;
        case CYCLE_THEN_RANDOM: Serial.println(F("1 - cycle-then-random")); break;
        case ALL_RANDOM:        Serial.println(F("2 - all-random"));        break;
    }
    Serial.print(F("  Relay     : "));
    Serial.println(useRelay ? F("on") : F("off"));
    Serial.println();
    Serial.println(F("Commands:"));
    Serial.println(F("  mode [0|1|2]   show or set play mode"));
    Serial.println(F("  relay [on|off] show or set relay"));
    Serial.println(F("  led            blink LED test"));
    Serial.println(F("  reboot         restart device"));
    Serial.println(F("  reset confirm  factory reset"));
    Serial.println(F("  help / ?       this help"));
    printPrompt();
}

// Case-insensitive string equality.
static bool strieq(const char *a, const char *b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
        a++; b++;
    }
    return (*a == '\0' && *b == '\0');
}

void processConsoleLine(const char *line) {
    // Skip leading spaces
    while (*line == ' ') line++;

    if (*line == '\0') {
        // Empty line: reprint status + prompt (handy after reconnect)
        printHelp();
        return;
    }

    // Split into command and argument at the first space
    char cmd[CON_BUF_SIZE + 1];
    char arg[CON_BUF_SIZE + 1];
    cmd[0] = arg[0] = '\0';

    const char *sp = strchr(line, ' ');
    if (sp) {
        size_t len = sp - line;
        strncpy(cmd, line, len);
        cmd[len] = '\0';
        sp++;
        while (*sp == ' ') sp++;    // skip extra spaces
        strncpy(arg, sp, CON_BUF_SIZE);
        arg[CON_BUF_SIZE] = '\0';
    } else {
        strncpy(cmd, line, CON_BUF_SIZE);
        cmd[CON_BUF_SIZE] = '\0';
    }

    // ── mode ──
    if (strieq(cmd, "mode")) {
        if (arg[0] == '\0') {
            Serial.print(F("Play mode: "));
            switch (playMode) {
                case ROUND_ROBIN:       Serial.println(F("0 - round-robin"));       break;
                case CYCLE_THEN_RANDOM: Serial.println(F("1 - cycle-then-random")); break;
                case ALL_RANDOM:        Serial.println(F("2 - all-random"));        break;
            }
        } else if (arg[0] >= '0' && arg[0] <= '2' && arg[1] == '\0') {
            playMode          = (PlayMode)(arg[0] - '0');
            sequenceIndex     = 0;
            firstCycleComplete = false;
            saveSettings();
            Serial.print(F("Saved. Play mode: "));
            switch (playMode) {
                case ROUND_ROBIN:       Serial.println(F("round-robin"));       break;
                case CYCLE_THEN_RANDOM: Serial.println(F("cycle-then-random")); break;
                case ALL_RANDOM:        Serial.println(F("all-random"));        break;
            }
        } else {
            Serial.println(F("Usage: mode [0|1|2]"));
        }
        printPrompt();
        return;
    }

    // ── relay ──
    if (strieq(cmd, "relay")) {
        if (arg[0] == '\0') {
            Serial.print(F("Relay: "));
            Serial.println(useRelay ? F("on") : F("off"));
        } else if (strieq(arg, "on")) {
            useRelay = true;
            saveSettings();
            digitalWrite(PIN_RELAY, HIGH);
            Serial.println(F("Relay on, saved."));
        } else if (strieq(arg, "off")) {
            useRelay = false;
            saveSettings();
            digitalWrite(PIN_RELAY, LOW);
            Serial.println(F("Relay off, saved."));
        } else {
            Serial.println(F("Usage: relay [on|off]"));
        }
        printPrompt();
        return;
    }

    // ── led ──
    if (strieq(cmd, "led")) {
        Serial.println(F("Testing LED..."));
        blinkLed(5, 100, 200);
        Serial.println(F("Done."));
        printPrompt();
        return;
    }

    // ── reboot ──
    if (strieq(cmd, "reboot") || strieq(cmd, "restart")) {
        Serial.println(F("Rebooting..."));
        consoleDelay(100);
        ESP.restart();
        return;
    }

    // ── reset ──
    if (strieq(cmd, "reset")) {
        if (strieq(arg, "confirm")) {
            EEPROM.write(EEPROM_ADDR_MAGIC, 0xFF);
            EEPROM.commit();
            Serial.println(F("Factory reset done. Rebooting..."));
            consoleDelay(100);
            ESP.restart();
        } else {
            Serial.println(F("Type 'reset confirm' to factory reset."));
            printPrompt();
        }
        return;
    }

    // ── help / ? ──
    if (strieq(cmd, "help") || strieq(cmd, "?")) {
        printHelp();
        return;
    }

    // ── unknown ──
    Serial.print(F("Unknown command '"));
    Serial.print(cmd);
    Serial.println(F("'. Type 'help' or '?'."));
    printPrompt();
}

// Called for every byte received from serial. Accumulates a line buffer,
// handles backspace/DEL, and dispatches to processConsoleLine on Enter.
void processSerialByte(char c) {
    if (c == '\r' || c == '\n') {
        Serial.println();
        conBuf[conLen] = '\0';
        processConsoleLine(conBuf);
        conLen = 0;
    } else if (c == 0x08 || c == 0x7F) {   // Backspace or DEL
        if (conLen > 0) {
            conLen--;
            Serial.print(F("\b \b"));
        }
    } else if (c >= 0x20 && conLen < CON_BUF_SIZE) {
        conBuf[conLen++] = c;
        Serial.print(c);                    // Local echo
    }
}

// ──────────────────── Setup & Loop ────────────────────

void setup() {
    Serial.begin(SERIAL_SPEED);
    EEPROM.begin(EEPROM_SIZE);
    loadSettings();

    pinMode(PIN_SWITCH, INPUT);
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);    // Active low: start off

    pinMode(PIN_RELAY, OUTPUT);
    digitalWrite(PIN_RELAY, LOW);   // Servo power off during init

    doorServo.attach(PIN_DOOR_SERVO);
    fingerServo.attach(PIN_FINGER_SERVO);

    doorServo.write(DOOR_CLOSED);
    fingerServo.write(FINGER_HIDDEN);

    blinkLed(3, 200, 100);

    if (useRelay) {
        digitalWrite(PIN_RELAY, HIGH);  // Enable servo power now that PWM is stable
    }

    randomSeed(analogRead(A0));
    printHelp();
}

void loop() {
    while (Serial.available()) {
        processSerialByte((char)Serial.read());
    }

    if (digitalRead(PIN_SWITCH) != HIGH) {
        return;
    }

    int moveIndex = pickNextMove();
    allMoves[moveIndex]();
}

// ──────────────────── Move animations ────────────────────

// Quick open, flick switch, quick close.
void moveSimpleClose() {
    sweepServo(doorServo,   DOOR_CLOSED, DOOR_OPEN,         3, 15);
    sweepServo(fingerServo, FINGER_HIDDEN, FINGER_EXTENDED, 4, 15);
    sweepServo(fingerServo, FINGER_EXTENDED, FINGER_HIDDEN, 4, 15);
    sweepServo(doorServo,   DOOR_OPEN, DOOR_CLOSED,         3, 15);
}

// Open door and pause, finger peeks and pauses, then flicks and hides.
void moveHesitantClose() {
    sweepServo(doorServo, DOOR_CLOSED, DOOR_OPEN, 3, 15);
    consoleDelay(800);

    sweepServo(fingerServo, FINGER_HIDDEN, 30, 4, 15);
    consoleDelay(1000);

    sweepServo(fingerServo, 30, FINGER_EXTENDED,            4, 15);
    sweepServo(fingerServo, FINGER_EXTENDED, FINGER_HIDDEN, 5, 15);
    sweepServo(doorServo,   DOOR_OPEN, DOOR_CLOSED,         3, 15);
}

// Door peeks and slams shut several times, then bursts open and flicks.
void moveCrazyDoor() {
    // Peek and slam
    sweepServo(doorServo, DOOR_CLOSED, 125,  3, 15);
    sweepServo(doorServo, 125, DOOR_CLOSED,  5, 15);

    // Peek again, hard slam
    sweepServo(doorServo, DOOR_CLOSED, 120,  3, 15);
    sweepServo(doorServo, 120, DOOR_CLOSED, 15, 15);
    consoleDelay(700);

    // Wider peek, hold, then close
    sweepServo(doorServo, DOOR_CLOSED, 135, 3, 15);
    consoleDelay(700);
    sweepServo(doorServo, 135, DOOR_CLOSED, 5, 15);

    // Burst open, flick, slam shut
    sweepServo(doorServo,   DOOR_CLOSED, DOOR_OPEN,         8, 15);
    sweepServo(fingerServo, FINGER_HIDDEN, FINGER_EXTENDED, 3, 15);
    sweepServo(fingerServo, FINGER_EXTENDED, FINGER_HIDDEN, 3, 15);
    sweepServo(doorServo,   DOOR_OPEN, DOOR_CLOSED,        15, 15);
}

// Everything moves very slowly, then the door snaps shut at the end.
void moveSlow() {
    sweepServo(doorServo,   DOOR_CLOSED, DOOR_OPEN, 1, 30);
    sweepServo(fingerServo, FINGER_HIDDEN, 60,       1, 30);
    sweepServo(fingerServo, 60, FINGER_HIDDEN,       1, 30);

    // Close door slowly at first, then snap shut
    sweepServo(doorServo, DOOR_OPEN, 125,   1, 30);
    consoleDelay(100);
    sweepServo(doorServo, 125, DOOR_CLOSED, 4, 15);
}

// Opens, extends finger slowly, nudges door threateningly, then flicks.
void moveSerious() {
    sweepServo(doorServo,   DOOR_CLOSED, DOOR_OPEN,         3, 15);
    sweepServo(fingerServo, FINGER_HIDDEN, FINGER_EXTENDED, 1, 15);
    consoleDelay(800);

    // Door nudges back and forth (threatening)
    sweepServo(doorServo, DOOR_OPEN, 130, 3, 15);
    sweepServo(doorServo, 130, DOOR_OPEN, 3, 15);
    sweepServo(doorServo, DOOR_OPEN, 130, 3, 15);
    sweepServo(doorServo, 130, DOOR_OPEN, 3, 15);

    // Finger snaps partway back, pauses, then flicks
    fingerServo.write(30);
    consoleDelay(1000);
    sweepServo(fingerServo, 30, FINGER_EXTENDED,            4, 15);
    sweepServo(fingerServo, FINGER_EXTENDED, FINGER_HIDDEN, 4, 15);

    // Door closes slowly from partway (matches original behavior)
    sweepServo(doorServo, 120, DOOR_CLOSED, 1, 15);
}

// Flicks the switch, then teases by nudging the door before finally closing.
void moveTrollClose() {
    sweepServo(doorServo,   DOOR_CLOSED, DOOR_OPEN,         3, 15);
    sweepServo(fingerServo, FINGER_HIDDEN, FINGER_EXTENDED, 4, 15);

    // Fake closing, pause, reopen
    sweepServo(doorServo, DOOR_OPEN, 130, 3, 15);
    consoleDelay(2000);
    sweepServo(doorServo, 130, DOOR_OPEN, 3, 15);

    // Small tease
    sweepServo(doorServo, DOOR_OPEN, 140, 3, 15);
    sweepServo(doorServo, 140, DOOR_OPEN, 3, 15);
    consoleDelay(500);

    sweepServo(fingerServo, FINGER_EXTENDED, FINGER_HIDDEN, 4, 15);
    sweepServo(doorServo,   DOOR_OPEN, DOOR_CLOSED,         3, 15);
}

// Finger extends fast then slows down dramatically near the switch.
void moveMatrix() {
    sweepServo(doorServo,   DOOR_CLOSED, DOOR_OPEN,  3, 15);
    sweepServo(fingerServo, FINGER_HIDDEN, 40,        4, 15);
    sweepServo(fingerServo, 40, FINGER_EXTENDED,      1, 30);  // Slow-motion finish
    consoleDelay(300);
    sweepServo(fingerServo, FINGER_EXTENDED, FINGER_HIDDEN, 4, 10);  // Quick retract
    sweepServo(doorServo,   DOOR_OPEN, DOOR_CLOSED,   3, 15);
}

// Sneaks the door open slowly, finger hesitates and wiggles, then flicks.
void moveSneak() {
    sweepServo(doorServo, DOOR_CLOSED, 130, 1, 30);
    consoleDelay(2000);

    sweepServo(fingerServo, FINGER_HIDDEN, 30, 1, 30);
    consoleDelay(500);

    sweepServo(doorServo, 130, DOOR_OPEN, 4, 15);
    consoleDelay(100);

    sweepServo(fingerServo, 30, 40, 4, 15);
    consoleDelay(500);

    // Wiggle finger nervously
    sweepServo(fingerServo, 40, 20, 4, 15);
    consoleDelay(100);
    sweepServo(fingerServo, 20, 40, 4, 15);
    consoleDelay(100);
    sweepServo(fingerServo, 40, 20, 4, 15);
    consoleDelay(100);

    // Commit: extend and retract
    sweepServo(fingerServo, 20, FINGER_EXTENDED,            4, 15);
    sweepServo(fingerServo, FINGER_EXTENDED, FINGER_HIDDEN, 4, 15);
    sweepServo(doorServo,   DOOR_OPEN, DOOR_CLOSED,         3, 15);
}
