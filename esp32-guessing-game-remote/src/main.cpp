/*******************************************************************************
Remote node. Acts as a controller to play the game.

Made by Valérian Grégoire--Bégranger -- 2025
*******************************************************************************/

#include <Arduino.h>
#include <WiFi.h>
#include <esp_now.h>

// Remote MAC address: 30:C9:22:FF:81:D0
// Game Manager MAC address: 30:C9:22:FF:71:AC
uint8_t macAddress[6] = {0x30, 0xC9, 0x22, 0xFF, 0x71, 0xAC};

// Message buffer for retries
uint8_t lastSentMessage;


// State machine variables
enum class States
{
    ready,
    playing,
    guessed,
    correct,
    wrong,
    won
};
States state;

// FSM flags
volatile bool startSignal = false;
volatile bool rightGuess = false;
volatile bool wrongGuess = false;
volatile bool wonSignal = false;

// Command codes
const uint8_t CMD_GAME_START = 0x01;
const uint8_t CMD_GOOD_GUESS = 0x02;
const uint8_t CMD_WRONG_GUESS = 0x03;
const uint8_t CMD_GAME_WON = 0x04;

// Button handling
const uint8_t buttonsCount = 3;
const uint8_t buttonPins[buttonsCount] = {13, 14, 26};
volatile bool buttonPressed[buttonsCount] = {false, false, false};
uint32_t lastDebounceTime[buttonsCount] = {0, 0, 0};
const uint32_t debounceDelay = 20; // 20ms debounce time

// LED pins
const uint8_t redLed = 12;
const uint8_t greenLed = 4;

// Timer for LED states
uint32_t lastStateUpdate = 0;
uint32_t lastBreatheUpdate = 0;
uint32_t lastBlinkUpdate = 0;

// Ignore received commands flag
bool locked;

// Callback when data is sent
void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    Serial.print("Last Packet Send Status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Fail");
    
    uint8_t retries = 0;
    while (status != ESP_NOW_SEND_SUCCESS && retries++ < 5)
    {
        // Serial.printf("Resending... Attempt %d\n", retries);
        esp_err_t result = esp_now_send(mac_addr, &lastSentMessage, sizeof(lastSentMessage));
        delay(100);
    }
    
    if (retries == 5)
    {
        Serial.println("Failed to send after 5 attempts");
    }
}

// Callback to receive data
void onDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    if (!locked)
    {
        if (len != 1)
        {
            return; // Expecting single byte commands
        }
        uint8_t command = incomingData[0];
        switch (command)
        {
        case CMD_GAME_START:
            startSignal = true;
            break;
        case CMD_GOOD_GUESS:
            rightGuess = true;
            break;
        case CMD_WRONG_GUESS:
            wrongGuess = true;
            break;
        case CMD_GAME_WON:
            wonSignal = true;
            break;
        }
    }
}
// Button interrupt handlers
void IRAM_ATTR onButtonPress(int buttonIndex)
{
    uint32_t currentTime = millis();
    
    // Only take the first press into consideration
    if (currentTime - lastDebounceTime[buttonIndex] > debounceDelay)
    {
        buttonPressed[buttonIndex] = true;
        lastDebounceTime[buttonIndex] = currentTime;
    }
}

void IRAM_ATTR onButton1Press() { onButtonPress(0); }
void IRAM_ATTR onButton2Press() { onButtonPress(1); }
void IRAM_ATTR onButton3Press() { onButtonPress(2); }

void setup()
{
    Serial.begin(115200);
    Serial.println("Running as remote node.");
    
    // WiFi setup
    WiFi.mode(WIFI_STA);
    Serial.print("Remote MAC Address: ");
    Serial.println(WiFi.macAddress());
    
    // ESP-NOW init
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("Error initializing ESP-NOW");
        ESP.restart();
    }
    esp_now_register_send_cb(onDataSent);
    esp_now_register_recv_cb(onDataRecv);
    
    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, macAddress, 6);
    peerInfo.channel = 1;
    peerInfo.encrypt = false;
    
    if (esp_now_add_peer(&peerInfo) != ESP_OK)
    {
        Serial.println("Failed to add peer");
        return;
    }

    // Initialize buttons with interrupts
    for (int i = 0; i < buttonsCount; ++i)
    {
        pinMode(buttonPins[i], INPUT_PULLUP);
    }
    attachInterrupt(buttonPins[0], onButton1Press, FALLING);
    attachInterrupt(buttonPins[1], onButton2Press, FALLING);
    attachInterrupt(buttonPins[2], onButton3Press, FALLING);

    // LED setup
    pinMode(redLed, OUTPUT);
    pinMode(greenLed, OUTPUT);

    // Initial state
    state = States::ready;
    Serial.println("Remote initialized; Waiting for the game to start.");
}

bool sendButtonPress(int buttonIndex)
{
    uint8_t buttonCode = buttonIndex + 1; // Send 1, 2, or 3 for button presses
    lastSentMessage = buttonCode;
    esp_err_t result = esp_now_send(macAddress, &buttonCode, sizeof(buttonCode));
    if (result == ESP_OK)
    {
        state = States::guessed;
        return true;
    }
    else
    {
        Serial.println("Failed to send button press.");
        return false;
    }
}

void breatheLeds()
{
    if (millis() - lastBreatheUpdate >= 20)
    {
        float r_intensity = (sin(millis() / 1000.0) * 127) + 128;
        float g_intensity = (cos(millis() / 1000.0) * 127) + 128;
        analogWrite(redLed, (int)r_intensity);
        analogWrite(greenLed, (int)g_intensity);
        lastBreatheUpdate = millis();
    }
}

void loop()
{
    switch (state)
    {
    case States::ready:
        locked = false;
        breatheLeds();
        if (startSignal)
        {
            Serial.println("The game starts !");
            startSignal = false;
            state = States::playing;
            lastStateUpdate = millis();
        }
        break;

    case States::playing:
        locked = false;
        for (int i = 0; i < buttonsCount; ++i)
        {
            if (buttonPressed[i])
            {
                buttonPressed[i] = false;
                bool sendSuccess = sendButtonPress(i);
                if (sendSuccess)
                {
                    Serial.print("Sent pressed signal for button ");
                    Serial.println(i);
                    state = States::guessed;
                    lastStateUpdate = millis();
                }
            }
        }
        break;

    case States::guessed:
        if (wonSignal)
        {
            wonSignal = false;
            Serial.println("Game won !");
            state = States::won;
            lastStateUpdate = millis();
            locked = true;
        }
        else if (rightGuess)
        {
            rightGuess = false;
            Serial.println("Right guess !");
            state = States::correct;
            lastStateUpdate = millis();
            locked = true;
        }
        else if (wrongGuess)
        {
            wrongGuess = false;
            Serial.println("Wrong guess !");
            state = States::wrong;
            lastStateUpdate = millis();
            locked = true;
        }
        break;

    case States::correct:
        digitalWrite(greenLed, HIGH);
        if (millis() - lastStateUpdate > 2000)
        {
            state = States::playing;
            lastStateUpdate = millis();
            digitalWrite(greenLed, LOW);
            locked = false;
        }
        break;
        
    case States::wrong:
        digitalWrite(redLed, HIGH);
        if (millis() - lastStateUpdate > 2000)
        {
            state = States::playing;
            lastStateUpdate = millis();
            digitalWrite(redLed, LOW);
            locked = false;
        }
        break;
    
    case States::won:
        digitalWrite(redLed, millis() % 2000 < 1000 ? HIGH : LOW);
        digitalWrite(greenLed, millis() % 2000 < 1000 ? HIGH : LOW);
        if (millis() - lastStateUpdate > 10000)
        {
            Serial.println("Waiting for a new game start signal.");
            state = States::ready;
            digitalWrite(greenLed, LOW);
            digitalWrite(redLed, LOW);
            locked = false;
        }
        break;
    }
}

