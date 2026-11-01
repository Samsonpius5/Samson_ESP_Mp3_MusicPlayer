
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <cstring>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>

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

    
    // Elapsed time: actual playback is not connected yet
    tft.setCursor(16, 139);
    tft.print("0:00");

    // Duration of the selected song
    tft.setCursor(250, 139);

    if (songCount == 0 ||
        songDurations[currentSong] == 0) {

        tft.print("--:--");

    } else {

        unsigned int duration = songDurations[currentSong];

        unsigned int minutes = duration / 60;
        unsigned int seconds = duration % 60;

        tft.print(minutes);
        tft.print(":");

        if (seconds < 10) {
            tft.print("0");
        }

        tft.print(seconds);
    }


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

                // Get the real index from the filtered list
                currentSong = visibleSongIndices[position];

                isPlaying = false;

                // Return to Now Playing
                currentScreen = Screen::NowPlaying;
                drawPlayerScreen();

                Serial.print("Selected song: ");
                Serial.println(songs[currentSong]);
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



void printPlaybackStatus() {

    if (isPlaying) {
        Serial.println("Playback status: PLAYING");
    } else {
        Serial.println("Playback status: PAUSED");
    }
}

void recordDemoPlay() {
    if (songCount == 0) {
    return;
}
    playCounts[currentSong]++;

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
    currentSong++;

    if (currentSong >= songCount) {
        currentSong = 0;
    }

    Serial.print("Next song: ");
    Serial.println(songs[currentSong]);

    inspectSelectedSong();

    printPlaybackStatus();

    if (isPlaying) {
    recordDemoPlay();
}
}

// Select previous song

void previousSong() {

    if (songCount == 0) {
    return;
}

    currentSong--;

    if (currentSong < 0) {
        currentSong = songCount - 1;
    }

    Serial.print("Previous song: ");
    Serial.println(songs[currentSong]);

    printPlaybackStatus();

    if (isPlaying) {
    recordDemoPlay();
}
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

    // NEW: Check SD card and print its files
    testSDCard();

    // Show startup screen
drawSplashScreen();

// Start the five-second timer
splashStartTime = millis();

Serial.println("Splash screen started!");
}


void loop() {

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

    delay(30);
    return;
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

                isPlaying = !isPlaying;

                if (isPlaying) {
                    recordDemoPlay();
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

    delay(30);

} 
