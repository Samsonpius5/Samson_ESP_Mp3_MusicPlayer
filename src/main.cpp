
#include <Arduino.h>
#include <SPI.h>
#include <cstring>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>

// TFT pins
#define TFT_CS   27
#define TFT_DC   26
#define TFT_RST  25

// Touch controller
#define TOUCH_CS 33

// Shared SPI pins
#define SPI_SCK  18
#define SPI_MISO 19
#define SPI_MOSI 23

// Your touchscreen calibration
#define RAW_LEFT   421
#define RAW_RIGHT  3425
#define RAW_TOP    630
#define RAW_BOTTOM 3526

Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
XPT2046_Touchscreen touch(TOUCH_CS);

// Our simulated music files
const char* songs[] = {
    "Song 1.mp3",
    "Song 2.mp3",
    "Song 3.mp3",
    "Song 4.mp3"
};

// Calculate number of songs automatically
const int songCount = sizeof(songs) / sizeof(songs[0]);

int currentSong = 0;
bool isPlaying = false;
bool previousTouch = false;


void inspectSelectedSong() {

    const char* selected = songs[currentSong];

    Serial.print("Selected song: ");
    Serial.println(selected);

    Serial.print("First character: ");
    Serial.println(*selected);

    Serial.print("Second character: ");
    Serial.println(selected[1]);

    Serial.printf(
        "Memory address: %p\n",
        static_cast<const void*>(selected)
    );
}






// Button positions
const int BTN_Y = 174;
const int BTN_H = 48;

const int PREV_X = 10;
const int PREV_W = 85;

const int PLAY_X = 108;
const int PLAY_W = 104;

const int NEXT_X = 225;
const int NEXT_W = 85;

// Convert raw touch coordinates
int convertX(int rawX) {
    int x = map(rawX, RAW_LEFT, RAW_RIGHT, 25, 295);
    return constrain(x, 0, 319);
}

int convertY(int rawY) {
    int y = map(rawY, RAW_TOP, RAW_BOTTOM, 30, 210);
    return constrain(y, 0, 239);
}

// Check if a point is inside a button
bool insideButton(
    int x, int y,
    int buttonX, int buttonWidth
) {
    return (
        x >= buttonX &&
        x < buttonX + buttonWidth &&
        y >= BTN_Y &&
        y < BTN_Y + BTN_H
    );
}

// Draw a single button
void drawButton(
    int x,
    int width,
    const char* label,
    uint16_t color
) {
    tft.fillRoundRect(
        x, BTN_Y,
        width, BTN_H,
        8, color
    );

    tft.setTextColor(ILI9341_BLACK);
    tft.setTextSize(2);

    // Centre the button label
    int textWidth = static_cast<int>(strlen(label)) * 12;

    int textX = x + (width - textWidth) / 2;
    int textY = BTN_Y + (BTN_H - 16) / 2;

    tft.setCursor(textX, textY);
    tft.print(label);
}

// Draw all controls
void drawControls() {

    drawButton(
        PREV_X, PREV_W,
        "PREV", ILI9341_CYAN
    );

    uint16_t playColor = isPlaying
        ? ILI9341_WHITE
        : ILI9341_YELLOW;

    drawButton(
        PLAY_X, PLAY_W,
        isPlaying ? "PAUSE" : "PLAY",
        playColor
    );

    drawButton(
        NEXT_X, NEXT_W,
        "NEXT", ILI9341_CYAN
    );
}

// Display selected song
void drawSong() {

    tft.fillRect(
        10, 85, 300, 40,
        ILI9341_BLACK
    );

    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    tft.setCursor(95, 97);
    tft.print(songs[currentSong]);
}

// Display player state
void drawStatus() {

    tft.fillRect(
        10, 130, 300, 35,
        ILI9341_BLACK
    );

    tft.setTextSize(2);

    if (isPlaying) {
        tft.setTextColor(ILI9341_GREEN);
        tft.setCursor(82, 140);
        tft.print("DEMO: PLAYING");
    } else {
        tft.setTextColor(ILI9341_WHITE);
        tft.setCursor(88, 140);
        tft.print("DEMO: PAUSED");
    }
}



void printPlaybackStatus() {

    if (isPlaying) {
        Serial.println("Playback status: PLAYING");
    } else {
        Serial.println("Playback status: PAUSED");
    }
}


// Select next song

void nextSong() {

    currentSong++;

    if (currentSong >= songCount) {
        currentSong = 0;
    }

    Serial.print("Next song: ");
    Serial.println(songs[currentSong]);

    inspectSelectedSong();

    printPlaybackStatus();
}

// Select previous song

void previousSong() {

    currentSong--;

    if (currentSong < 0) {
        currentSong = songCount - 1;
    }

    Serial.print("Previous song: ");
    Serial.println(songs[currentSong]);

    printPlaybackStatus();
}


void setup() {

    Serial.begin(115200);

    pinMode(TFT_CS, OUTPUT);
    pinMode(TOUCH_CS, OUTPUT);

    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TOUCH_CS, HIGH);

    SPI.begin(
        SPI_SCK,
        SPI_MISO,
        SPI_MOSI
    );

    // Initialize TFT
    tft.begin();
    tft.setRotation(1);

    // Initialize touch controller
    if (!touch.begin()) {
        Serial.println("Touch initialization failed");

        while (true) {
            delay(1000);
        }
    }

    touch.setRotation(1);

    // Draw initial interface
    tft.fillScreen(ILI9341_BLACK);

    tft.setTextColor(ILI9341_CYAN);
    tft.setTextSize(2);

    tft.setCursor(64, 16);
    tft.print("ESP32 MP3 PLAYER");

    tft.drawFastHLine(
        15, 45, 290,
        ILI9341_WHITE
    );

    tft.setTextSize(1);
    tft.setCursor(106, 65);
    tft.print("SELECTED TRACK");

    drawSong();
    drawStatus();
    drawControls();

    Serial.println("Music player controls ready!");
}

void loop() {

    bool pressed = touch.touched();

    // React only when a new touch begins
    if (pressed && !previousTouch) {

    Serial.println("NEW TOUCH DETECTED");

    TS_Point p = touch.getPoint();



        int x = convertX(p.x);
        int y = convertY(p.y);

        Serial.print("X: ");
        Serial.print(x);
        Serial.print(" Y: ");
        Serial.println(y);

        if (insideButton(x, y, PREV_X, PREV_W)) {

            previousSong();

        } else if (insideButton(x, y, PLAY_X, PLAY_W)) {

            isPlaying = !isPlaying;

            printPlaybackStatus();

            Serial.println(
                isPlaying ? "PLAY" : "PAUSE"
            );

        } else if (insideButton(x, y, NEXT_X, NEXT_W)) {

            nextSong();
        }

        // Update the display
        drawSong();
        drawStatus();
        drawControls();
    }
    // Detect when the finger is released
    if (!pressed && previousTouch) {
        Serial.println("FINGER RELEASED!");
    }
    // Remember the current touch state
    previousTouch = pressed;

    delay(30);
}
