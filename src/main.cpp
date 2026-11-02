
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <cstring>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <ESP_I2S.h>
#include <math.h>
#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

// TFT pins
#define TFT_CS   27
#define TFT_DC   26
#define TFT_RST  25

// Touch controller
#define TOUCH_CS 33

// sd card module
#define SD_CS   32
#define SD_SCK  14
#define SD_MISO 16
#define SD_MOSI 13
// NS4168 amplifier connections
#define I2S_BCLK 21
#define I2S_LRC  22
#define I2S_DOUT 17

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

SPIClass sdSPI(HSPI);
Preferences songStats;
bool songStatsReady = false;
AudioFileSourceSD* audioFile = nullptr;
AudioGeneratorMP3* mp3Decoder = nullptr;
AudioOutputI2S* audioOutput = nullptr;
I2SClass speakerI2S;

// Our simulated music files


const int MAX_SONGS = 20;
const int MAX_FILENAME_LENGTH = 128;
const int MAX_PATH_LENGTH = 192;

// Storage for filenames read from the SD card
char songNameStorage[MAX_SONGS][MAX_FILENAME_LENGTH] = {};

// Full file paths for future audio playback
char songPaths[MAX_SONGS][MAX_PATH_LENGTH] = {};

// Pointers to each stored filename
const char* songs[MAX_SONGS] = {};

// Duration is unknown until we read audio metadata
uint16_t songDurations[MAX_SONGS] = {};

// Actual number of MP3 files discovered
int songCount = 0;


int currentSong = 0;
bool isPlaying = false;
bool isPaused = false;
bool previousTouch = false;
bool shuffleEnabled = false;
bool repeatEnabled = false;
bool favoriteSongs[MAX_SONGS] = {};
uint32_t playCounts[MAX_SONGS] = {};

// Screen management
enum class Screen {
    Splash,
    NowPlaying,
    Library,
    SongList,
    FavouritesList,
    MostPlayedList
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

const int VISIBLE_SONG_ROWS = 5;

const int SONG_LIST_TOP = 78;
const int SONG_ROW_HEIGHT = 26;
const int SONG_ROW_STEP = 29;

// Index of the first visible song
int firstVisibleSong = 0;

// Stores which songs belong to the current list
int visibleSongIndices[MAX_SONGS];

// Number of songs in the current list
int visibleSongCount = 0;

// Song List touch gesture tracking
int listTouchStartX = 0;
int listTouchStartY = 0;
int lastScrollY = 0;

bool listTouchTracking = false;
bool listWasSwiped = false;

const int SWIPE_THRESHOLD = 18;


int lastProgressWidth = 0;
// Finger tracking for music seeking
bool seekDragging = false;
int seekDragX = 16;

void saveFavoriteSong(int songIndex);
void saveSongPlayCount(int songIndex);
uint32_t elapsedPlaybackMs = 0;
uint32_t playbackStartedAtMs = 0;
uint32_t lastProgressUpdateMs = 0;
// Allows the touch functions to stop audio playback
void stopMp3Playback();
void startMp3Playback(uint32_t startAtMs, bool startPaused);
void recordDemoPlay();


void inspectSelectedSong() {

    if (songCount == 0) {
    return;
}

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
    if (songCount == 0) {
    tft.print("NO MP3 FILES");
    return;
    }

    char displayTitle[14];
    strncpy(displayTitle, songs[currentSong], 13);
    displayTitle[13] = '\0';

    tft.print(displayTitle);
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
        tft.print("PLAYING");
    } else if (isPaused) {
        tft.print("PAUSED");
    } else {
        tft.print("STOPPED");
    }
}

uint32_t getPlaybackElapsedMs() {

    if (isPlaying) {
        return elapsedPlaybackMs +
               (millis() - playbackStartedAtMs);
    }

    return elapsedPlaybackMs;
}



void drawProgressBar(bool fullRedraw = false) {

    uint16_t background = tft.color565(24, 16, 27);
    uint16_t purple = tft.color565(155, 70, 220);
    uint16_t orange = tft.color565(255, 135, 75);

    const int progressX = 16;
    const int progressY = 167;
    const int progressWidth = 288;
    const int progressHeight = 6;

    // Remember what we drew last time
    static int lastWidth = -1;
    static int lastSong = -1;

    uint32_t elapsedSeconds =
        getPlaybackElapsedMs() / 1000;

    uint32_t totalSeconds = 0;

    if (songCount > 0) {
        totalSeconds = songDurations[currentSong];
    }

    if (totalSeconds > 0 &&
        elapsedSeconds > totalSeconds) {
        elapsedSeconds = totalSeconds;
    }

    int playedWidth = 0;

    if (totalSeconds > 0) {
        playedWidth =
            (elapsedSeconds * progressWidth) /
            totalSeconds;
    }

    // Full redraw only when necessary
    bool resetBar =
        fullRedraw ||
        lastWidth < 0 ||
        currentSong != lastSong ||
        playedWidth < lastWidth;

    if (resetBar) {

        // Clear only for a new screen or reset
        tft.fillRect(
            12, 136, 296, 44, background
        );

        tft.fillRoundRect(
            progressX, progressY,
            progressWidth, progressHeight,
            3, orange
        );

        if (playedWidth > 0) {
            tft.fillRect(
                progressX, progressY,
                playedWidth, progressHeight,
                purple
            );
        }

    } else if (playedWidth != lastWidth) {

        // Clear only the old and new indicator area
        int oldX = progressX + lastWidth;
        int newX = progressX + playedWidth;

        int dirtyLeft = min(oldX, newX) - 8;
        int dirtyRight = max(oldX, newX) + 8;

        tft.fillRect(
            dirtyLeft,
            progressY - 8,
            dirtyRight - dirtyLeft + 1,
            22,
            background
        );

        // Restore the track inside this small area
        int trackLeft = max(dirtyLeft, progressX);

        int trackRight = min(
            dirtyRight,
            progressX + progressWidth - 1
        );

        if (trackLeft <= trackRight) {

            tft.fillRect(
                trackLeft, progressY,
                trackRight - trackLeft + 1,
                progressHeight,
                orange
            );

            int filledRight = min(
                trackRight,
                progressX + playedWidth - 1
            );

            if (filledRight >= trackLeft) {
                tft.fillRect(
                    trackLeft, progressY,
                    filledRight - trackLeft + 1,
                    progressHeight,
                    purple
                );
            }
        }
    }

    // Draw text directly over its old background.
    // No full-area clearing every second.
    tft.setTextColor(
        ILI9341_WHITE, background
    );

    tft.setTextSize(2);

    char elapsedText[18];

    snprintf(
        elapsedText,
        sizeof(elapsedText),
        "%lu:%02lu",
        (unsigned long)(elapsedSeconds / 60),
        (unsigned long)(elapsedSeconds % 60)
    );

    tft.setCursor(16, 139);
    tft.print(elapsedText);

    char durationText[18];

    if (totalSeconds == 0) {
        snprintf(
            durationText,
            sizeof(durationText),
            "--:--"
        );
    } else {
        snprintf(
            durationText,
            sizeof(durationText),
            "%lu:%02lu",
            (unsigned long)(totalSeconds / 60),
            (unsigned long)(totalSeconds % 60)
        );
    }

    tft.setCursor(250, 139);
    tft.print(durationText);

    // Draw the indicator at the new position
    tft.fillCircle(
        progressX + playedWidth,
        progressY + 3,
        7,
        orange
    );

    tft.drawCircle(
        progressX + playedWidth,
        progressY + 3,
        7,
        ILI9341_WHITE
    );

    // Save the position for the next update
    lastWidth = playedWidth;
    lastSong = currentSong;
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
    drawProgressBar(true);
    drawControls();
    drawFavouriteHeart();
}


void drawLibraryScreen() {

    uint16_t background = tft.color565(24, 16, 27);
    uint16_t cardColor = tft.color565(36, 30, 43);
    uint16_t borderColor = tft.color565(68, 57, 76);
    uint16_t mutedText = tft.color565(180, 170, 185);

    // Clear screen
    tft.fillScreen(background);

    // Back arrow
    tft.drawLine(30, 24, 14, 24, ILI9341_WHITE);
    tft.drawLine(14, 24, 22, 16, ILI9341_WHITE);
    tft.drawLine(14, 24, 22, 32, ILI9341_WHITE);

    // Header
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(50, 20);
    tft.print("ESP32 MUSIC PLAYER");

    // Page title
    tft.setTextSize(2);
    tft.setCursor(16, 57);
    tft.print("MY LIBRARY");

    // All Songs card
    tft.fillRoundRect(16, 84, 288, 78, 8, cardColor);
    tft.drawRoundRect(16, 84, 288, 78, 8, borderColor);

    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(2);
    tft.setCursor(34, 108);
    tft.print("All Songs");

    tft.setTextColor(mutedText);
    tft.setTextSize(1);
    tft.setCursor(34, 138);
    tft.print("Browse your music");

    // Favourites card
    tft.fillRoundRect(16, 172, 140, 52, 8, cardColor);
    tft.drawRoundRect(16, 172, 140, 52, 8, borderColor);

    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(28, 188);
    tft.print("Favourites");

    tft.setTextColor(mutedText);
    tft.setCursor(28, 206);
    tft.print("Saved Songs");

    // Most Played card
    tft.fillRoundRect(164, 172, 140, 52, 8, cardColor);
    tft.drawRoundRect(164, 172, 140, 52, 8, borderColor);

    tft.setTextColor(ILI9341_WHITE);
    tft.setCursor(176, 188);
    tft.print("Most Played");

    tft.setTextColor(mutedText);
    tft.setCursor(176, 206);
    tft.print("Top Tracks");
}



void prepareMusicList(Screen listType) {

    visibleSongCount = 0;

    for (int i = 0; i < songCount; i++) {

        bool includeSong = false;

        if (listType == Screen::SongList) {
            includeSong = true;
        }
        else if (listType == Screen::FavouritesList) {
            includeSong = favoriteSongs[i];
        }
        else if (listType == Screen::MostPlayedList) {
            includeSong = playCounts[i] > 0;
        }

        if (includeSong) {
            visibleSongIndices[visibleSongCount] = i;
            visibleSongCount++;
        }
    }

    // Sort Most Played by number of plays
    if (listType == Screen::MostPlayedList) {

        for (int i = 0; i < visibleSongCount; i++) {
            for (int j = i + 1; j < visibleSongCount; j++) {

                int first = visibleSongIndices[i];
                int second = visibleSongIndices[j];

                if (playCounts[second] > playCounts[first]) {

                    int temp = visibleSongIndices[i];
                    visibleSongIndices[i] = visibleSongIndices[j];
                    visibleSongIndices[j] = temp;
                }
            }
        }
    }

    firstVisibleSong = 0;
}



void drawMusicListScreen() {

    uint16_t background = tft.color565(24, 16, 27);
    uint16_t cardColor = tft.color565(36, 30, 43);
    uint16_t borderColor = tft.color565(68, 57, 76);
    uint16_t mutedText = tft.color565(180, 170, 185);
    uint16_t purple = tft.color565(165, 85, 210);

    tft.fillScreen(background);

    // Back arrow
    tft.drawLine(30, 24, 14, 24, ILI9341_WHITE);
    tft.drawLine(14, 24, 22, 16, ILI9341_WHITE);
    tft.drawLine(14, 24, 22, 32, ILI9341_WHITE);

    // Header
    tft.setTextColor(ILI9341_WHITE);
    tft.setTextSize(1);
    tft.setCursor(50, 20);
    tft.print("ESP32 MUSIC PLAYER");

    // Page title
    tft.setTextSize(2);
    tft.setCursor(16, 51);
    if (currentScreen == Screen::FavouritesList) {
    tft.print("FAVOURITES");
    }
    else if (currentScreen == Screen::MostPlayedList) {
    tft.print("MOST PLAYED");
    }
    else {
        tft.print("SONG LIST");
    }

    // DJ shortcut to Now Playing
    uint16_t orange = tft.color565(255, 135, 75);

    tft.drawCircle(290, 24, 11, orange);
    tft.drawCircle(290, 24, 6, orange);
    tft.fillCircle(290, 24, 2, orange);

    

    // How many rows can we draw?
    int visibleCount = visibleSongCount - firstVisibleSong;

    if (visibleCount > VISIBLE_SONG_ROWS) {
        visibleCount = VISIBLE_SONG_ROWS;
    }

    if (visibleSongCount == 0) {
    tft.setTextColor(mutedText);
    tft.setTextSize(1);
    tft.setCursor(50, 115);

    if (currentScreen == Screen::FavouritesList) {
        tft.print("No favourite songs yet");
    }
    else if (currentScreen == Screen::MostPlayedList) {
        tft.print("No plays recorded yet");
    }
    else if (currentScreen == Screen::SongList) {
    tft.print("No MP3 files found");
    }
}

    for (int row = 0; row < visibleCount; row++) {

        // Actual index in our song array
        int index = visibleSongIndices[firstVisibleSong + row];

        int rowY = SONG_LIST_TOP + row * SONG_ROW_STEP;

        // Song row
        tft.fillRoundRect(
            16, rowY, 284, SONG_ROW_HEIGHT,
            6, cardColor
        );

        tft.drawRoundRect(
            16, rowY, 284, SONG_ROW_HEIGHT,
            6, borderColor
        );

        // Six-dot grip icon
        for (int dotRow = 0; dotRow < 3; dotRow++) {
            for (int dotCol = 0; dotCol < 2; dotCol++) {

                tft.fillCircle(
                    26 + dotCol * 6,
                    rowY + 7 + dotRow * 6,
                    1,
                    mutedText
                );
            }
        }

        // Song title
        tft.setTextColor(ILI9341_WHITE);
        tft.setTextSize(2);
        tft.setCursor(44, rowY + 5);
        // Keep long filenames inside the row
        char displayName[18];

        strncpy(displayName, songs[index], 17);
        displayName[17] = '\0';

        tft.print(displayName);

        // Song duration

    // Right-aligned duration area
        tft.setTextColor(mutedText);
        tft.setTextSize(1);
        tft.setCursor(265, rowY + 9);

        if (songDurations[index] == 0) {

            tft.print("--:--");

        } else {

            unsigned int minutes = songDurations[index] / 60;
            unsigned int seconds = songDurations[index] % 60;

            tft.print(minutes);
            tft.print(":");

            if (seconds < 10) {
            tft.print("0");
            }

            tft.print(seconds);
        }
    }


    // Scroll indicator
    const int trackY = SONG_LIST_TOP;
    const int trackHeight = 142;

    tft.fillRoundRect(
        307, trackY, 3, trackHeight,
        1, borderColor
    );

    int thumbHeight = trackHeight;

    if (visibleSongCount > VISIBLE_SONG_ROWS) {
    thumbHeight =
        trackHeight * VISIBLE_SONG_ROWS / visibleSongCount;
    }

    int maxOffset = visibleSongCount - VISIBLE_SONG_ROWS;


    int thumbY = trackY;

    if (maxOffset > 0) {
        thumbY +=
            firstVisibleSong * (trackHeight - thumbHeight)
            / maxOffset;
    }

    tft.fillRoundRect(
        306, thumbY, 5, thumbHeight,
        2, purple
    );
}


void handleMusicListTouch(bool pressed) {

    if (pressed) {

        TS_Point p = touch.getPoint();

        int x = convertX(p.x);
        int y = convertY(p.y);

        // A new finger touch begins
        if (!previousTouch) {

            listTouchStartX = x;
            listTouchStartY = y;
            lastScrollY = y;

            listTouchTracking = true;
            listWasSwiped = false;
        }

        // Finger is moving across the list
        else if (listTouchTracking &&
                 listTouchStartY >= SONG_LIST_TOP) {

            int movement = y - lastScrollY;

            // Distinguish swiping from tapping
            if (abs(y - listTouchStartY) >= SWIPE_THRESHOLD) {
                listWasSwiped = true;
            }

            int maxScroll = visibleSongCount - VISIBLE_SONG_ROWS;

            if (maxScroll < 0) {
                maxScroll = 0;
            }

            // Swipe UP: show later songs
            if (movement <= -SWIPE_THRESHOLD) {

                if (firstVisibleSong < maxScroll) {
                    firstVisibleSong++;
                    drawMusicListScreen();
                }

                lastScrollY = y;
            }

            // Swipe DOWN: show earlier songs
            else if (movement >= SWIPE_THRESHOLD) {

                if (firstVisibleSong > 0) {
                    firstVisibleSong--;
                    drawMusicListScreen();
                }

                lastScrollY = y;
            }
        }
    }

    // Finger released
    else if (previousTouch && listTouchTracking) {

        if (!listWasSwiped) {

            int x = listTouchStartX;
            int y = listTouchStartY;

            // DJ shortcut: Now Playing
            if (x >= 270 && y <= 55) {

                currentScreen = Screen::NowPlaying;
                drawPlayerScreen();
            }

            // Back: My Library
            else if (x <= 55 && y <= 55) {

                currentScreen = Screen::Library;
                drawLibraryScreen();
            }

            // A song row was tapped
            else if (x >= 16 && x < 300 &&
                     y >= SONG_LIST_TOP &&
                     y < SONG_LIST_TOP +
                         VISIBLE_SONG_ROWS * SONG_ROW_STEP) {

                int row =
                    (y - SONG_LIST_TOP) / SONG_ROW_STEP;

                int offset =
                    (y - SONG_LIST_TOP) % SONG_ROW_STEP;

                
            int position = firstVisibleSong + row;

            if (
                row < VISIBLE_SONG_ROWS &&
                offset < SONG_ROW_HEIGHT &&
                position < visibleSongCount
            ) {

                // Stop any previously playing or paused song
                stopMp3Playback();

                // Select the song the user touched
                currentSong = visibleSongIndices[position];

                Serial.print("Selected song: ");
                Serial.println(songs[currentSong]);

                // Open the Now Playing screen
                currentScreen = Screen::NowPlaying;
                drawPlayerScreen();

                // Start the selected song immediately
                startMp3Playback(0, false);

                // Record a successful playback
                if (isPlaying) {
                    recordDemoPlay();
                }

                // Update the screen to show PLAYING
                drawStatus();
                drawControls();
                drawProgressBar(true);

                }
            }
        }

        listTouchTracking = false;
        listWasSwiped = false;
        }
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


void stopMp3Playback() {

    if (mp3Decoder != nullptr) {

        if (mp3Decoder->isRunning()) {
            mp3Decoder->stop();
        }

        delete mp3Decoder;
        mp3Decoder = nullptr;
    }

    if (audioFile != nullptr) {
        audioFile->close();
        delete audioFile;
        audioFile = nullptr;
    }

    if (audioOutput != nullptr) {
        audioOutput->stop();
        delete audioOutput;
        audioOutput = nullptr;
    }

    isPlaying = false;
    isPaused = false;
    elapsedPlaybackMs = 0;
    playbackStartedAtMs = 0;
}


void startMp3Playback(
    uint32_t startAtMs = 0,
    bool startPaused = false
    ) {

    if (songCount == 0 ||
        currentSong < 0 ||
        currentSong >= songCount) {

        Serial.println("No valid song selected");
        return;
    }

    // Stop any previously opened song
    stopMp3Playback();

    Serial.print("Opening MP3: ");
    Serial.println(songPaths[currentSong]);

    audioFile =
        new AudioFileSourceSD(songPaths[currentSong]);

    if (audioFile == nullptr ||
        !audioFile->isOpen()) {

        Serial.println("Cannot open selected MP3");
        stopMp3Playback();
        return;
    }

    
    // Seek to an approximate position in the MP3 file
    if (startAtMs > 0 && songDurations[currentSong] > 0) {

        uint32_t totalMs =
            (uint32_t)songDurations[currentSong] * 1000UL;

        // Avoid starting beyond the end of the file
        if (startAtMs >= totalMs) {
            startAtMs = totalMs > 2000
                ? totalMs - 2000
                : 0;
        }

        uint32_t fileSize = audioFile->getSize();

        if (startAtMs > 0 && fileSize > 4096) {

            uint32_t dataStart = 0;
            uint8_t id3[10];

            // Account for an ID3 metadata tag at the start
            if (audioFile->seek(0, SEEK_SET) &&
                audioFile->read(id3, 10) == 10 &&
                id3[0] == 'I' &&
                id3[1] == 'D' &&
                id3[2] == '3') {

                dataStart =
                    10 +
                    ((uint32_t)(id3[6] & 0x7F) << 21) +
                    ((uint32_t)(id3[7] & 0x7F) << 14) +
                    ((uint32_t)(id3[8] & 0x7F) << 7) +
                    (uint32_t)(id3[9] & 0x7F);

                if (id3[3] == 4 && (id3[5] & 0x10)) {
                    dataStart += 10;
                }
            }

            if (dataStart < fileSize - 4096) {

                uint32_t audioBytes = fileSize - dataStart;

                uint32_t offset =
                    dataStart +
                    (uint32_t)(
                        ((uint64_t)audioBytes * startAtMs)
                        / totalMs
                    );

                // Begin a little earlier so the MP3 decoder
                // can synchronize with nearby audio frames
                if (offset > dataStart + 2048) {
                    offset -= 2048;
                } else {
                    offset = dataStart;
                }

                if (offset > fileSize - 2048) {
                    offset = fileSize - 2048;
                }

                if (!audioFile->seek(
                        (int32_t)offset, SEEK_SET)) {

                    Serial.println("MP3 seek FAILED");
                    stopMp3Playback();
                    return;
                }

                Serial.printf(
                    "MP3 seek: %lu ms, byte %lu\n",
                    (unsigned long)startAtMs,
                    (unsigned long)offset
                );

            } else {

                startAtMs = 0;
                audioFile->seek(0, SEEK_SET);
            }

        } else {

            startAtMs = 0;
            audioFile->seek(0, SEEK_SET);
        }
    }


    audioOutput = new AudioOutputI2S();

    if (audioOutput == nullptr) {
        Serial.println("Cannot create audio output");
        stopMp3Playback();
        return;
    }

    // Use the same pins as our working tone test
    audioOutput->SetPinout(
        I2S_BCLK,
        I2S_LRC,
        I2S_DOUT
    );

    // Low volume for the first music test
    audioOutput->SetGain(0.50f);

    // Mix stereo audio for our single speaker
    audioOutput->SetOutputModeMono(true);

    mp3Decoder = new AudioGeneratorMP3();

    if (mp3Decoder == nullptr ||
        !mp3Decoder->begin(audioFile, audioOutput)) {

        Serial.println("MP3 decoder failed");
        stopMp3Playback();
        return;
    }

        isPlaying = !startPaused;
        isPaused = startPaused;

        elapsedPlaybackMs = startAtMs;
        playbackStartedAtMs = millis();
        lastProgressUpdateMs = millis();

        // Keep the output silent if seeking while paused
        if (startPaused && audioOutput != nullptr) {
            audioOutput->flush();
        }

    Serial.println("REAL MP3 PLAYBACK STARTED!");
}


void seekToProgressX(int touchX) {

    if (songCount == 0 ||
        currentSong < 0 ||
        currentSong >= songCount ||
        songDurations[currentSong] == 0) {

        return;
    }

    const int barLeft = 16;
    const int barRight = 304;
    const int barWidth = barRight - barLeft;

    // Keep touch position within the progress bar
    touchX = constrain(touchX, barLeft, barRight);

    uint32_t totalMs =
        (uint32_t)songDurations[currentSong] * 1000UL;

    // Convert horizontal position into song time
    uint32_t targetMs =
        (uint32_t)(
            ((uint64_t)(touchX - barLeft) * totalMs)
            / barWidth
        );

    // Leave some audio after the requested position
    if (targetMs >= totalMs) {
        targetMs = totalMs > 2000
            ? totalMs - 2000
            : 0;
    }

    // Remember whether the player was paused
    bool remainPaused = isPaused;

    Serial.printf(
        "Seek requested: %lu seconds\n",
        (unsigned long)(targetMs / 1000)
    );

    // Restart the decoder near the requested time
    startMp3Playback(targetMs, remainPaused);

    // Refresh the player screen
    if (currentScreen == Screen::NowPlaying) {
        drawStatus();
        drawProgressBar(true);
        drawControls();
    }
}


void printPlaybackStatus() {

    if (isPlaying) {
        Serial.println("Playback status: PLAYING");
    } else if (isPaused) {
        Serial.println("Playback status: PAUSED");
    } else {
        Serial.println("Playback status: STOPPED");
    }
}

void recordDemoPlay() {
    if (songCount == 0) {
    return;
}
    playCounts[currentSong]++;
    saveSongPlayCount(currentSong);

    Serial.print("Demo play count: ");
    Serial.print(songs[currentSong]);
    Serial.print(" = ");
    Serial.println(playCounts[currentSong]);
}

// Select next song
    
void nextSong() {

    if (songCount == 0) {
        return;
    }

    // Remember whether music was playing
    bool wasPlaying = isPlaying;

    // Stop the current song
    stopMp3Playback();

    if (shuffleEnabled && songCount > 1) {

        // Select a random song different from
        // the currently selected song
        int randomSong = random(songCount - 1);

        if (randomSong >= currentSong) {
            randomSong++;
        }

        currentSong = randomSong;

        Serial.println("SHUFFLE: Random song selected");

    } else {

        // Normal sequential next song
        currentSong++;

        if (currentSong >= songCount) {
            currentSong = 0;
        }
    }

    Serial.print("Next song: ");
    Serial.println(songs[currentSong]);

    // Keep music playing if it was playing before
    if (wasPlaying) {

        startMp3Playback();

        if (isPlaying) {
            recordDemoPlay();
        }
    }

    printPlaybackStatus();
}




// Select previous song

void previousSong() {

    if (songCount == 0) {
        return;
    }

    // Remember whether music was playing
    bool wasPlaying = isPlaying;

    // Stop the current track
    stopMp3Playback();

    // Select the previous song
    currentSong--;

    if (currentSong < 0) {
        currentSong = songCount - 1;
    }

    Serial.print("Previous song: ");
    Serial.println(songs[currentSong]);

    // Automatically play if music was playing
    if (wasPlaying) {
        startMp3Playback();

        if (isPlaying) {
            recordDemoPlay();
        }
    }

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


uint16_t readMp3Duration(const char* path) {

    File mp3 = SD.open(path, FILE_READ);

    if (!mp3) {
        Serial.println("Cannot open MP3 for duration");
        return 0;
    }

    uint32_t fileSize = mp3.size();
    uint32_t position = 0;

    // Check for an ID3 tag at the beginning.
    // ID3 contains information such as title and artist.
    uint8_t tag[10];

    if (fileSize >= 10 &&
        mp3.read(tag, 10) == 10 &&
        tag[0] == 'I' &&
        tag[1] == 'D' &&
        tag[2] == '3') {

        uint32_t tagSize =
            ((uint32_t)tag[6] << 21) |
            ((uint32_t)tag[7] << 14) |
            ((uint32_t)tag[8] << 7) |
            tag[9];

        position = 10 + tagSize;

        // ID3v2.4 may also contain a footer
        if (tag[3] == 4 && (tag[5] & 0x10)) {
            position += 10;
        }
    }

    // Bitrates in kilobits per second
    const uint16_t mpeg1Bitrates[16] = {
        0, 32, 40, 48, 56, 64, 80, 96,
        112, 128, 160, 192, 224, 256, 320, 0
    };

    const uint16_t mpeg2Bitrates[16] = {
        0, 8, 16, 24, 32, 40, 48, 56,
        64, 80, 96, 112, 128, 144, 160, 0
    };

    const uint32_t sampleRates[3] = {
        44100, 48000, 32000
    };

    uint64_t totalMicroseconds = 0;
    uint32_t frameCount = 0;

    // Read MP3 frame headers
    while (position + 4 <= fileSize) {

        uint8_t header[4];

        if (!mp3.seek(position) ||
            mp3.read(header, 4) != 4) {
            break;
        }

        uint8_t version = (header[1] >> 3) & 3;
        uint8_t layer = (header[1] >> 1) & 3;
        uint8_t bitrateIndex = header[2] >> 4;
        uint8_t sampleRateIndex = (header[2] >> 2) & 3;

        // Ignore invalid or unsupported frame headers
        if (header[0] != 0xFF ||
            (header[1] & 0xE0) != 0xE0 ||
            version == 1 ||
            layer != 1 ||
            bitrateIndex == 0 ||
            bitrateIndex == 15 ||
            sampleRateIndex == 3) {

            position++;
            continue;
        }

        uint32_t sampleRate =
            sampleRates[sampleRateIndex];

        if (version == 2) {
            sampleRate /= 2;
        }
        else if (version == 0) {
            sampleRate /= 4;
        }

        uint16_t bitrate =
            (version == 3)
            ? mpeg1Bitrates[bitrateIndex]
            : mpeg2Bitrates[bitrateIndex];

        uint32_t frameSize =
            ((version == 3 ? 144000UL : 72000UL)
             * bitrate) / sampleRate;

        frameSize += (header[2] >> 1) & 1;

        if (frameSize < 4 ||
            frameSize > fileSize - position) {

            position++;
            continue;
        }

        uint16_t samplesPerFrame =
            (version == 3) ? 1152 : 576;

        totalMicroseconds +=
            (uint64_t)samplesPerFrame *
            1000000ULL / sampleRate;

        frameCount++;
        position += frameSize;

        // Allow the ESP32 to service other tasks
        if (frameCount % 500 == 0) {
            yield();
        }
    }

    mp3.close();

    if (frameCount == 0) {
        return 0;
    }

    uint64_t seconds =
        (totalMicroseconds + 500000ULL) / 1000000ULL;

    if (seconds == 0) {
        seconds = 1;
    }

    if (seconds > 65535) {
        seconds = 65535;
    }

    return (uint16_t)seconds;
}


uint32_t makeSongCacheHash(
    const char* path,
    uint32_t fileSize
) {
    uint32_t hash = 2166136261UL;

    // Include every character in the file path
    while (*path) {
        hash ^= (uint8_t)*path++;
        hash *= 16777619UL;
    }

    // Include the file size
    for (int i = 0; i < 4; i++) {
        hash ^= (uint8_t)(fileSize >> (i * 8));
        hash *= 16777619UL;
    }

    return hash;
}


void makeSongStatsKey(
    int songIndex,
    char prefix,
    char key[10]
) {
    // Use the filename/path as a stable identity.
    // File size is zero because we don't want
    // play history to depend on the file size.
    uint32_t hash = makeSongCacheHash(
        songPaths[songIndex],
        0
    );

    snprintf(
        key,
        10,
        "%c%08lX",
        prefix,
        (unsigned long)hash
    );
}


void loadSongStats() {

    songStatsReady = songStats.begin(
        "songstats",
        false
    );

    if (!songStatsReady) {
        Serial.println("Song NVS initialization FAILED");
        return;
    }

    for (int i = 0; i < songCount; i++) {

        char key[10];

        // Restore the favourite flag
        makeSongStatsKey(i, 'f', key);

        favoriteSongs[i] =
            songStats.getBool(key, false);

        // Restore the number of plays
        makeSongStatsKey(i, 'p', key);

        playCounts[i] =
            songStats.getUInt(key, 0);
    }

    Serial.println("Song favourites and play counts restored!");
}


void saveFavoriteSong(int songIndex) {

    if (!songStatsReady ||
        songIndex < 0 ||
        songIndex >= songCount) {
        return;
    }

    char key[10];
    makeSongStatsKey(songIndex, 'f', key);

    songStats.putBool(
        key,
        favoriteSongs[songIndex]
    );

    Serial.println("Favourite saved to NVS");
}


void saveSongPlayCount(int songIndex) {

    if (!songStatsReady ||
        songIndex < 0 ||
        songIndex >= songCount) {
        return;
    }

    char key[10];
    makeSongStatsKey(songIndex, 'p', key);

    songStats.putUInt(
        key,
        playCounts[songIndex]
    );

    Serial.println("Play count saved to NVS");
}



void testSDCard() {

    Serial.println();
    Serial.println("Checking SD card...");

    songCount = 0;
    currentSong = 0;

    // Initialize SD card using HSPI
    if (!SD.begin(SD_CS, sdSPI, 4000000)) {
        Serial.println("SD initialization FAILED");
        return;
    }

    Serial.println("SD initialization SUCCESS!");

    File root = SD.open("/");

    if (!root || !root.isDirectory()) {
        Serial.println("Cannot open root directory");
        return;
    }

    Serial.println("Searching for MP3 files...");
    Preferences durationCache;

    bool cacheReady =
        durationCache.begin("songtime", false);

    if (!cacheReady) {
        Serial.println("Duration cache unavailable");
    }

    File entry = root.openNextFile();

    while (entry) {

        if (!entry.isDirectory()) {

            const char* filename = entry.name();
            const char* path = entry.path();

            String lowerName = filename;
            lowerName.toLowerCase();

            if (lowerName.endsWith(".mp3")) {

                if (songCount >= MAX_SONGS) {

                    Serial.println("Library capacity reached");

                } else if (
                    strlen(filename) >= MAX_FILENAME_LENGTH ||
                    strlen(path) >= MAX_PATH_LENGTH
                ) {

                    Serial.print("Skipped MP3: ");
                    Serial.println(filename);

                    Serial.print("Filename length: ");
                    Serial.println(strlen(filename));

                    Serial.print("Path length: ");
                    Serial.println(strlen(path));

                } else {

                    // Store the actual filename
                    strcpy(
                        songNameStorage[songCount],
                        filename
                    );

                    // Store its full SD card path
                    strcpy(
                        songPaths[songCount],
                        path
                    );

                    songs[songCount] =
                        songNameStorage[songCount];
            

                    
                // Create a short cache key from the path and file size
                uint32_t hash = makeSongCacheHash(
                    songPaths[songCount],
                    (uint32_t)entry.size()
                );

                char cacheKey[10];

                snprintf(
                    cacheKey,
                    sizeof(cacheKey),
                    "d%08lX",
                    (unsigned long)hash
                );

                // Check whether we already know this song's duration
                uint16_t savedDuration = cacheReady
                    ? durationCache.getUShort(cacheKey, 0)
                    : 0;

                if (savedDuration > 0) {

                    // Fast path: reuse the previously saved duration
                    songDurations[songCount] = savedDuration;

                    Serial.print("CACHE HIT: ");
                    Serial.println(songs[songCount]);

                } else {

                    // Slow path: calculate duration only when necessary
                    Serial.print("CACHE MISS - Reading: ");
                    Serial.println(songs[songCount]);

                    songDurations[songCount] =
                        readMp3Duration(songPaths[songCount]);

                    // Save a successfully calculated duration
                    if (cacheReady && songDurations[songCount] > 0) {
                        durationCache.putUShort(
                            cacheKey,
                            songDurations[songCount]
                        );
                    }
                }

                // Display the resulting duration
                Serial.print("Duration: ");
                Serial.print(songDurations[songCount] / 60);
                Serial.print(":");

                if (songDurations[songCount] % 60 < 10) {
                    Serial.print("0");
                }

                Serial.println(songDurations[songCount] % 60);



                    Serial.print("MP3 found: ");
                    Serial.println(songs[songCount]);

                    songCount++;
                }

            } // End MP3 extension check

        } // End file check

        entry.close();
        entry = root.openNextFile();

    } // End directory scanning loop

    root.close();

    if (cacheReady) {
    durationCache.end();
    }

    Serial.print("Total MP3 songs loaded: ");
    Serial.println(songCount);

    Serial.println("SD scan completed");

} // End testSDCard()


void testSpeakerTone() {

    Serial.println("Starting I2S speaker test...");

    // Configure the three I2S signal pins
    speakerI2S.setPins(
        I2S_BCLK,
        I2S_LRC,
        I2S_DOUT
    );

    // 16 kHz, 16-bit stereo audio
    if (!speakerI2S.begin(
        I2S_MODE_STD,
        16000,
        I2S_DATA_BIT_WIDTH_16BIT,
        I2S_SLOT_MODE_STEREO
    )) {
        Serial.println("I2S initialization FAILED");
        return;
    }

    const int sampleRate = 16000;
    const int framesPerBlock = 128;

    int16_t samples[framesPerBlock * 2];

    uint32_t sampleNumber = 0;
    bool writeOK = true;

    // Generate approximately one second of
    // low-volume 440 Hz audio
    for (int block = 0; block < 125; block++) {

        for (int i = 0; i < framesPerBlock; i++) {

            float phase =
                2.0f * PI * 440.0f *
                sampleNumber / sampleRate;

            int16_t value =
                (int16_t)(2000.0f * sinf(phase));

            // Same signal in both channels
            samples[i * 2] = value;
            samples[i * 2 + 1] = value;

            sampleNumber++;
        }

        size_t written = speakerI2S.write(
            (const uint8_t*)samples,
            sizeof(samples)
        );

        if (written != sizeof(samples)) {
            writeOK = false;
            break;
        }
    }

    speakerI2S.end();

    if (writeOK) {
        Serial.println("I2S test data sent");
    } else {
        Serial.println("I2S write FAILED");
    }
}


void setup() {

    Serial.begin(115200);

    pinMode(TFT_CS, OUTPUT);
    pinMode(TOUCH_CS, OUTPUT);
    pinMode(SD_CS, OUTPUT);       // NEW

    digitalWrite(TFT_CS, HIGH);
    digitalWrite(TOUCH_CS, HIGH);
    digitalWrite(SD_CS, HIGH);    // NEW

    SPI.begin(
        SPI_SCK,
        SPI_MISO,
        SPI_MOSI
    );

    sdSPI.begin(
    SD_SCK,
    SD_MISO,
    SD_MOSI,
    SD_CS
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

   testSDCard();

// Restore saved favourites and play history
loadSongStats();

// Show startup screen
drawSplashScreen();

// Start the five-second timer
splashStartTime = millis();

Serial.println("Splash screen started!");
}


void loop() {

        
    
// Keep the MP3 decoder supplied with audio data
if (mp3Decoder != nullptr &&
    isPlaying &&
    !isPaused) {

    if (!mp3Decoder->loop()) {

        Serial.println("Song finished!");

        if (repeatEnabled) {

            // Repeat the same song from the beginning
            Serial.println("REPEAT: Restarting current song");

            stopMp3Playback();
            startMp3Playback();

            if (isPlaying) {
                recordDemoPlay();
            }

        } else if (!shuffleEnabled &&
                   currentSong == songCount - 1) {

            // Last song finished and Repeat is OFF
            Serial.println("Playlist finished");

            stopMp3Playback();

        } else {

            // Play the next sequential or random song
            nextSong();
        }

        // Refresh the player display
        if (currentScreen == Screen::NowPlaying) {

            drawSong();
            drawStatus();
            drawProgressBar(true);
            drawControls();
            drawFavouriteHeart();
        }
    }
}



        // Update progress every second while music plays
    if (currentScreen == Screen::NowPlaying &&
        isPlaying &&
        millis() - lastProgressUpdateMs >= 1000) {

        lastProgressUpdateMs = millis();

        drawProgressBar();
    }

    // 1. Handle startup splash screen
    if (currentScreen == Screen::Splash) {
        updateSplashScreen();
        return;
    }

    bool pressed = touch.touched();

    // 2. Song List needs continuous touch tracking
    // so that swiping can be detected.
    if (
    currentScreen == Screen::SongList ||
    currentScreen == Screen::FavouritesList ||
    currentScreen == Screen::MostPlayedList
) {
    handleMusicListTouch(pressed);

    previousTouch = pressed;

    delay(isPlaying ? 1 : 30);
    return;
}


// Touch-to-seek on the Now Playing progress bar
if (currentScreen == Screen::NowPlaying) {

    // Detect the beginning of a touch on the bar
    if (pressed && !previousTouch) {

        TS_Point p = touch.getPoint();

        int x = convertX(p.x);
        int y = convertY(p.y);

        // Expanded touch area around the timeline
        if (x >= 12 && x <= 308 &&
            y >= 155 && y <= 180 &&
            songCount > 0 &&
            songDurations[currentSong] > 0) {

            seekDragging = true;
            seekDragX = constrain(x, 16, 304);

            previousTouch = true;
            delay(1);
            return;
        }
    }

    // Track movement until the finger is released
    if (seekDragging) {

        if (pressed) {

            TS_Point p = touch.getPoint();

            seekDragX = constrain(
                convertX(p.x),
                16,
                304
            );

        } else {

            // Finger released: seek once
            seekDragging = false;

            seekToProgressX(seekDragX);
        }

        previousTouch = pressed;
        delay(1);
        return;
    }
}


    // 3. Handle a new touch on other screens
    if (pressed && !previousTouch) {

        Serial.println("NEW TOUCH DETECTED");

        TS_Point p = touch.getPoint();

        int x = convertX(p.x);
        int y = convertY(p.y);

        Serial.print("X: ");
        Serial.print(x);
        Serial.print(" Y: ");
        Serial.println(y);

        // MY LIBRARY SCREEN
        
    if (currentScreen == Screen::Library) {

        // Back to Now Playing
        if (x <= 55 && y <= 55) {

            currentScreen = Screen::NowPlaying;
            drawPlayerScreen();
        }

        // All Songs card
        else if (
            x >= 16 && x < 304 &&
            y >= 84 && y < 162
        ) {
            currentScreen = Screen::SongList;

            prepareMusicList(currentScreen);
            drawMusicListScreen();

            Serial.println("All Songs opened");
        }

        // Favourites card
        else if (
            x >= 16 && x < 156 &&
            y >= 172 && y < 224
        ) {
            currentScreen = Screen::FavouritesList;

            prepareMusicList(currentScreen);
            drawMusicListScreen();

            Serial.println("Favourites opened");
        }

        // Most Played card
        else if (
            x >= 164 && x < 304 &&
            y >= 172 && y < 224
        ) {
            currentScreen = Screen::MostPlayedList;

            prepareMusicList(currentScreen);
            drawMusicListScreen();

            Serial.println("Most Played opened");
        }

        previousTouch = true;
        return;
}


        // NOW PLAYING SCREEN
        if (currentScreen == Screen::NowPlaying) {

            // Library button
            if (x >= 260 && y <= 55) {

                currentScreen = Screen::Library;
                drawLibraryScreen();

                Serial.println("Library opened");

                previousTouch = true;
                return;
            }

            // Favourite heart
            if (
                x >= 266 && x < 310 &&
                y >= 78 && y < 110
            ) {

            favoriteSongs[currentSong] =
            !favoriteSongs[currentSong];

        // Save the change to ESP32 flash
        saveFavoriteSong(currentSong);

        drawFavouriteHeart();

                Serial.println(
                    favoriteSongs[currentSong]
                    ? "ADDED TO FAVOURITES"
                    : "REMOVED FROM FAVOURITES"
                );
            }

            // Music controls
            else if (y >= 182 && y < 240) {

                if (x < 60) {

                    shuffleEnabled = !shuffleEnabled;

                    Serial.println(
                        shuffleEnabled
                        ? "SHUFFLE ON"
                        : "SHUFFLE OFF"
                    );

                } else if (x < 126) {

                    previousSong();

                } else if (x < 194) {

            
                
            if (isPlaying) {

                // Save elapsed time before pausing
                elapsedPlaybackMs +=
                    millis() - playbackStartedAtMs;

                isPlaying = false;
                isPaused = true;

                // Replace buffered audio with silence
                if (audioOutput != nullptr) {
                    audioOutput->flush();
                }

                Serial.println("MP3 PAUSED");

            } else if (isPaused) {


                    // Resume from the preserved decoder position
                    if (mp3Decoder != nullptr &&
                        mp3Decoder->isRunning()) {

                        isPaused = false;
                        playbackStartedAtMs = millis();
                        isPlaying = true;

                        Serial.println("MP3 RESUMED");

                    } else {

                        // Recovery if decoder is no longer available
                        isPaused = false;
                        startMp3Playback();
                    }

                } else {

                    // Start a new song
                    startMp3Playback();

                    if (isPlaying) {
                        recordDemoPlay();
                    }
                }

                printPlaybackStatus();


                
            } else if (x < 260) {

                    nextSong();

                } else {

                    repeatEnabled = !repeatEnabled;

                    Serial.println(
                        repeatEnabled
                        ? "REPEAT ON"
                        : "REPEAT OFF"
                    );
                }

                // Refresh the music controls
                drawSong();
                drawStatus();
                drawProgressBar();
                drawControls();
                drawFavouriteHeart();
            }

        } // Closes NowPlaying condition

    } // Closes new-touch condition

    // 4. Detect finger release
    if (!pressed && previousTouch) {
        Serial.println("FINGER RELEASED!");
    }

    // 5. Remember touch state
    previousTouch = pressed;

    delay(isPlaying ? 1 : 30);

} 
