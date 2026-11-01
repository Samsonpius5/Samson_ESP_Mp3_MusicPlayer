
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


// Calculate number of songs automatically.
const int songCount = sizeof(songs) / sizeof(songs[0]);

int currentSong = 0;
bool isPlaying = false;
bool previousTouch = false;
bool shuffleEnabled = false;
bool repeatEnabled = false;
bool favoriteSongs[songCount] = {};

// Screen management
enum class Screen {
    Splash,
    NowPlaying
};

Screen currentScreen = Screen::Splash;

// Splash screen timing
const unsigned long SPLASH_DURATION = 5000;
unsigned long splashStartTime = 0;

// Loading bar
const int BAR_X = 70;
const int BAR_Y = 180;
const int BAR_WIDTH = 180;
const int BAR_HEIGHT = 5;

int lastProgressWidth = 0;


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
const int BTN_Y = 184;
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

    uint16_t background = tft.color565(24, 16, 27);
    uint16_t white = ILI9341_WHITE;
    uint16_t orange = tft.color565(255, 135, 75);

    // Clear only the music controls area
    tft.fillRect(0, 181, 320, 59, background);

    // Active controls appear orange
    uint16_t shuffleColor = shuffleEnabled ? orange : white;
    uint16_t repeatColor = repeatEnabled ? orange : white;

    // 1. SHUFFLE ICON
    // Two crossing paths with arrowheads
    tft.drawLine(15, 199, 24, 199, shuffleColor);
    tft.drawLine(24, 199, 41, 215, shuffleColor);
    tft.drawLine(41, 215, 46, 215, shuffleColor);
    tft.drawLine(41, 210, 46, 215, shuffleColor);
    tft.drawLine(41, 220, 46, 215, shuffleColor);

    tft.drawLine(15, 215, 24, 215, shuffleColor);
    tft.drawLine(24, 215, 41, 199, shuffleColor);
    tft.drawLine(41, 199, 46, 199, shuffleColor);
    tft.drawLine(41, 194, 46, 199, shuffleColor);
    tft.drawLine(41, 204, 46, 199, shuffleColor);

    // 2. PREVIOUS SONG ICON
    tft.fillRect(76, 195, 3, 24, white);
    tft.fillTriangle(
        80, 207,
        101, 194,
        101, 220,
        white
    );

    // 3. CIRCULAR PLAY/PAUSE BUTTON
    tft.fillCircle(160, 207, 24, white);

    if (isPlaying) {

        // Pause icon: two vertical bars
        tft.fillRoundRect(
            151, 195, 6, 24, 2, background
        );

        tft.fillRoundRect(
            163, 195, 6, 24, 2, background
        );

    } else {

        // Play icon: right-facing triangle
        tft.fillTriangle(
            154, 194,
            154, 220,
            174, 207,
            background
        );
    }

    // 4. NEXT SONG ICON
    tft.fillTriangle(
        218, 194,
        218, 220,
        239, 207,
        white
    );

    tft.fillRect(240, 195, 3, 24, white);

    // 5. REPEAT ICON
    tft.drawFastHLine(280, 198, 18, repeatColor);
    tft.drawLine(298, 198, 304, 204, repeatColor);
    tft.drawLine(304, 204, 298, 210, repeatColor);

    tft.drawFastHLine(280, 216, 18, repeatColor);
    tft.drawLine(280, 216, 274, 210, repeatColor);
    tft.drawLine(274, 210, 280, 204, repeatColor);
}


// Display selected song
void drawSong() {

    // Same background as the player screen
    uint16_t background =
        tft.color565(24, 16, 27);

    // Clear the song title area
    tft.fillRect(
        12, 78,
        240, 32,
        background
    );

    // Set song title colour and size
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(3);

    // Position song title
    tft.setCursor(16, 81);

    // Display currently selected song
    tft.print(songs[currentSong]);
}

// Display player state
void drawStatus() {

    // Player background colour
    uint16_t background =
        tft.color565(24, 16, 27);

    // Muted text colour
    uint16_t mutedText =
        tft.color565(180, 170, 185);

    // Orange indicator
    uint16_t orange =
        tft.color565(255, 135, 75);

    // Clear only the status area
    tft.fillRect(
        12, 109, 296, 23,
        background
    );

    // Draw the orange dot
    tft.fillCircle(
        20, 120, 4,
        orange
    );

    // Display playback status
    tft.setTextColor(mutedText);
    tft.setTextSize(2);
    tft.setCursor(32, 112);

    if (isPlaying) {
        tft.print("DEMO - PLAY");
    } else {
        tft.print("DEMO - PAUSE");
    }
}


void drawProgressBar() {

    uint16_t background = tft.color565(24, 16, 27);
    uint16_t purple = tft.color565(155, 70, 220);
    uint16_t orange = tft.color565(255, 135, 75);

    // Clear the progress bar area
    tft.fillRect(12, 136, 296, 44, background);

    // Time labels
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    tft.setCursor(16, 139);
    tft.print("1:12");

    tft.setCursor(256, 139);
    tft.print("3:20");

    // Progress bar dimensions
    const int progressX = 16;
    const int progressY = 167;
    const int progressWidth = 288;
    const int progressHeight = 6;

    // Simulated progress: approximately 36%
    const int playedWidth = 104;

    // Full progress track
    tft.fillRoundRect(
        progressX, progressY,
        progressWidth, progressHeight,
        3, orange
    );

    // Played portion
    tft.fillRoundRect(
        progressX, progressY,
        playedWidth, progressHeight,
        3, purple
    );

    // Circular progress indicator
    tft.fillCircle(
        progressX + playedWidth,
        progressY + 3,
        7, orange
    );

    tft.drawCircle(
        progressX + playedWidth,
        progressY + 3,
        7, ILI9341_WHITE
    );
}


void drawFavouriteHeart() {

    uint16_t background = tft.color565(24, 16, 27);
    uint16_t orange = tft.color565(255, 135, 75);

    // Clear the heart area
    tft.fillRect(266, 78, 44, 31, background);

    if (favoriteSongs[currentSong]) {

        // Filled heart for a favourite song
        tft.fillCircle(281, 89, 7, orange);
        tft.fillCircle(295, 89, 7, orange);

        tft.fillTriangle(
            274, 91,
            302, 91,
            288, 106,
            orange
        );

    } else {

        // Outline heart
        uint16_t white = ILI9341_WHITE;

        tft.drawLine(288, 85, 284, 81, white);
        tft.drawLine(284, 81, 278, 81, white);
        tft.drawLine(278, 81, 273, 86, white);
        tft.drawLine(273, 86, 273, 93, white);
        tft.drawLine(273, 93, 288, 107, white);
        tft.drawLine(288, 107, 303, 93, white);
        tft.drawLine(303, 93, 303, 86, white);
        tft.drawLine(303, 86, 298, 81, white);
        tft.drawLine(298, 81, 292, 81, white);
        tft.drawLine(292, 81, 288, 85, white);
    }
}


void drawLibraryButton() {

    uint16_t white = ILI9341_WHITE;

    // Four small circles forming a grid
    tft.drawCircle(284, 18, 4, white);
    tft.drawCircle(300, 18, 4, white);

    tft.drawCircle(284, 34, 4, white);
    tft.drawCircle(300, 34, 4, white);
}


void drawPlayerScreen() {

    // Main background colour
    uint16_t background =
        tft.color565(24, 16, 27);

    // Secondary text colour
    uint16_t mutedText =
        tft.color565(180, 170, 185);

    // Clear the screen
    tft.fillScreen(background);

    // Main heading
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);

    tft.setCursor(16, 16);
    tft.print("ESP32 MUSIC PLAYER");
    drawLibraryButton();

    // Section label
    tft.setTextColor(mutedText);
    tft.setTextSize(1);

    tft.setCursor(16, 65);
    tft.print("NOW PLAYING");

    // Existing functionality
    drawSong();
    drawStatus();
    drawProgressBar();
    drawControls();
    drawFavouriteHeart();
}

void drawSplashScreen() {

    tft.fillScreen(ILI9341_WHITE);

    uint16_t orange = tft.color565(255, 135, 75);
    uint16_t purple = tft.color565(160, 105, 225);
    uint16_t gray = tft.color565(215, 215, 215);

    // Simple headphone icon
    tft.drawCircle(160, 60, 10, orange);

    // Hide the lower half of the circle
    tft.fillRect(148, 60, 24, 12, ILI9341_WHITE);

    // Headphone earpieces
    tft.fillRoundRect(147, 58, 5, 12, 2, orange);
    tft.fillRoundRect(168, 58, 5, 12, 2, orange);

    // Player name
    tft.setTextSize(2);
    tft.setTextColor(orange, ILI9341_WHITE);

    tft.setCursor(112, 92);
    tft.print("SAMSON'S");

    tft.setCursor(100, 118);
    tft.print("MP3 PLAYER");

    // Loading message
    tft.setTextSize(1);
    tft.setTextColor(purple, ILI9341_WHITE);

    tft.setCursor(73, 155);
    tft.print("Initializing music library...");

    // Loading bar background
    tft.fillRoundRect(
        BAR_X,
        BAR_Y,
        BAR_WIDTH,
        BAR_HEIGHT,
        3,
        gray
    );

    // Footer
    tft.setTextColor(ILI9341_DARKGREY, ILI9341_WHITE);

    tft.setCursor(109, 215);
    tft.print("POWERED BY ESP32");

    lastProgressWidth = 0;
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

void updateSplashScreen() {

    unsigned long elapsed = millis() - splashStartTime;

    // Calculate progress, maximum 100%
    unsigned long progressTime = elapsed;

    if (progressTime > SPLASH_DURATION) {
        progressTime = SPLASH_DURATION;
    }

    int progressWidth =
        (progressTime * BAR_WIDTH) / SPLASH_DURATION;

    // Draw only the new part of the bar
    if (progressWidth > lastProgressWidth) {

        uint16_t purple = tft.color565(180, 85, 200);

        tft.fillRect(
            BAR_X + lastProgressWidth,
            BAR_Y,
            progressWidth - lastProgressWidth,
            BAR_HEIGHT,
            purple
        );

        lastProgressWidth = progressWidth;
    }

    // Switch screens after five seconds
    if (elapsed >= SPLASH_DURATION) {

        currentScreen = Screen::NowPlaying;

        isPlaying = false;

        drawPlayerScreen();

        Serial.println("Startup complete!");
        Serial.println("Now Playing screen loaded.");
    }
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

    // Show startup screen
drawSplashScreen();

// Start the five-second timer
splashStartTime = millis();

Serial.println("Splash screen started!");
}

void loop() {

    if (currentScreen == Screen::Splash) {

    updateSplashScreen();

    return;
}

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


        // Respond to touches in the control row
        if (x >= 266 && x < 310 &&
    y >= 78 && y < 110) {

    favoriteSongs[currentSong] =
        !favoriteSongs[currentSong];

    drawFavouriteHeart();

    Serial.println(
        favoriteSongs[currentSong]
            ? "ADDED TO FAVOURITES"
            : "REMOVED FROM FAVOURITES"
    );

} else if (y >= 182 && y < 240) {

            if (x < 60) {

                // Shuffle
                shuffleEnabled = !shuffleEnabled;

                Serial.println(
                    shuffleEnabled ? "SHUFFLE ON" : "SHUFFLE OFF"
                );

            } else if (x < 126) {

                // Previous song
                previousSong();

            } else if (x < 194) {

                // Play or pause
                isPlaying = !isPlaying;

                printPlaybackStatus();

            } else if (x < 260) {

                // Next song
                nextSong();

            } else {

                // Repeat
                repeatEnabled = !repeatEnabled;

                Serial.println(
                    repeatEnabled ? "REPEAT ON" : "REPEAT OFF"
                );
            }

            // Update only the relevant UI elements
            drawSong();
            drawStatus();
            drawControls();
            drawFavouriteHeart();
        }
    }
    // Detect when the finger is released
      if (!pressed && previousTouch) {
        Serial.println("FINGER RELEASED!");
    }

    // Remember the current touch state
    previousTouch = pressed;

    delay(30);
}
