/*******************************************************************************
Game Manager node. Controls the game logic and difficulty.

Made by Valérian Grégoire--Bégranger -- 2025
*******************************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// Game Manager MAC address: 30:C9:22:FF:71:AC
// Remote MAC address: 30:C9:22:FF:81:D0
uint8_t remoteMacAddress[6] = {0x30, 0xC9, 0x22, 0xFF, 0x81, 0xD0};

// Game states
enum class States
{
    idle,
    countdown,
    playing,
    game_over
};
States state;

// Command codes
const uint8_t CMD_GAME_START = 0x01;
const uint8_t CMD_GOOD_GUESS = 0x02;
const uint8_t CMD_WRONG_GUESS = 0x03;
const uint8_t CMD_GAME_WON = 0x04;

// LED and button pins
const uint8_t ledPins[4] = {17, 25, 4, 12};
const uint8_t buttonPin = 13;

// Difficulty level (0-15)
volatile uint8_t difficulty = 0;
volatile bool difficultyLocked = false;
volatile bool longPressDetected = false;
volatile bool buttonPressed = false;
volatile uint32_t pressDuration = 0;

// Timing variables
uint32_t buttonPressStart = 0;
const uint32_t longPressDuration = 2000; // 2 seconds

// Random sequence variables
const uint8_t maxSequenceLength = 15;
uint8_t sequence[maxSequenceLength];
uint8_t currentStep = 0;

// ESP-NOW callback for data sent
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    Serial.print("Packet Send Status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
}

// Display difficulty using binary representation on LEDs
void displayDifficulty()
{
    // Serial.print("Displaying difficulty: ");
    // Serial.println(difficulty);
    for (int i = 0; i < 4; ++i)
    {
        digitalWrite(ledPins[i], (difficulty >> i) & 1 ? HIGH : LOW);
    }
}

// Generate a random sequence of numbers (1-3)
void generateSequence()
{
    Serial.println("Generating random sequence");
    for (int i = 0; i <= difficulty; ++i)
    {
        sequence[i] = random(1, 4);
    }
    currentStep = 0;
}

// Send game start command
void sendGameStart()
{
    Serial.println("Sending game start command");
    esp_now_send(remoteMacAddress, &CMD_GAME_START, sizeof(CMD_GAME_START));
}

// Process received data from remote node
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    if (state != States::playing || len != 1)
        return;

    uint8_t guess = incomingData[0];
    Serial.print("Received guess: ");
    Serial.println(guess);
    if (guess == sequence[currentStep])
    {
        currentStep++;
        if (currentStep > difficulty)
        {
            esp_now_send(remoteMacAddress, &CMD_GAME_WON, sizeof(CMD_GAME_WON));
            state = States::game_over;
        }
        else
        {
            esp_now_send(remoteMacAddress, &CMD_GOOD_GUESS, sizeof(CMD_GOOD_GUESS));
        }
    }
    else
    {
        esp_now_send(remoteMacAddress, &CMD_WRONG_GUESS, sizeof(CMD_WRONG_GUESS));
        currentStep = 0;
    }
}

// Interrupt Service Routine for button press
void IRAM_ATTR onButtonPress()
{
    if (digitalRead(buttonPin) == LOW)
    {
        buttonPressed = true;
        buttonPressStart = millis();
    }
    else
    {
        pressDuration = millis() - buttonPressStart;
        buttonPressed = false;
    }
}

void setup()
{
    Serial.begin(115200);
    WiFi.mode(WIFI_STA);
    Serial.print("Game manager MAC Address: ");
    Serial.println(WiFi.macAddress());

    // Initialize LEDs and button
    for (int i = 0; i < 4; ++i)
    {
        pinMode(ledPins[i], OUTPUT);
        digitalWrite(ledPins[i], LOW);
    }
    pinMode(buttonPin, INPUT_PULLUP);
    attachInterrupt(buttonPin, onButtonPress, CHANGE);

    // ESP-NOW setup
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        return;
    }
    esp_now_register_send_cb(onDataSent);

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, remoteMacAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        Serial.println("Failed to add peer");
        return;
    }

    esp_now_register_recv_cb(onDataRecv);

    // Initial state
    state = States::idle;
    displayDifficulty();
}

void loop()
{
    if (!buttonPressed && pressDuration > 0)
    {
        if (pressDuration >= longPressDuration)
        {
            difficultyLocked = true;
            longPressDetected = true;
            Serial.println("Long press detected, difficulty locked");
        }
        else if (!difficultyLocked)
        {
            difficulty = (difficulty + 1) % 16;
            Serial.print("Button short press, new difficulty: ");
            Serial.println(difficulty);
            displayDifficulty();
        }
        pressDuration = 0; // Reset duration after processing
    }

    switch (state)
    {
    case States::idle:
        if (longPressDetected)
        {
            generateSequence();
            sendGameStart();
            state = States::countdown;
            longPressDetected = false;
        }
        break;

    case States::countdown:
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 4; ++j)
            {
                digitalWrite(ledPins[j], HIGH);
            }
            delay(200);
            for (int j = 0; j < 4; ++j)
            {
                digitalWrite(ledPins[j], LOW);
            }
            delay(200);
        }
        state = States::playing;
        break;

    case States::playing:
        displayDifficulty();
        break;

    case States::game_over:
        delay(3000);
        state = States::idle;
        difficultyLocked = false;
        displayDifficulty();
        break;
    }
}
