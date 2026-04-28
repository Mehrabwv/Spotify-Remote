# Spotify-Remote
Spotify-Remote is a compact ESP32-based controller for Spotify. It shows the current song and artist on an OLED display. A button lets you play/pause with a short press and skip with a long press.

A compact Spotify remote controller built with an SSTuino II / ESP32-style board, a 0.96-inch I2C OLED display, and one push button.

The device connects to Wi-Fi, talks directly to the Spotify Web API using OAuth refresh-token authentication, displays the currently playing song on the OLED, and lets the user control Spotify playback with a physical button.

This version uses:

```cpp
const unsigned long TRACK_CHECK_INTERVAL = 3000;  // 3 seconds
delay(20);                                       // main loop delay
```

The 3-second refresh gives a good balance between responsiveness and stability. The 20 ms loop delay helps the board check the button more often, making short presses easier to detect.

---

## What the System Does

The project turns the SSTuino / ESP32 board into a small Spotify controller.

It can:

- Connect to Wi-Fi.
- Generate a Spotify access token from a stored refresh token.
- Ask Spotify what song is currently playing.
- Display song title, artist name, playback state, and API status on the OLED.
- Short press the button to play/pause.
- Hold the button for 5 seconds to skip to the next track.
- Automatically refresh the song information every 3 seconds.
- Clean unsupported Spotify text characters, such as curly apostrophes, so they display better on the OLED.

This device does **not** play audio directly. Spotify audio still plays on another device such as a phone, laptop, speaker, or TV. The SSTuino acts as a remote control and display.

---

## Hardware Used

- SSTuino II / ESP32-compatible board
- 0.96-inch I2C OLED display, usually SSD1306, 128 × 64 pixels
- Push button
- Jumper wires
- USB cable for power and programming

The potentiometer is not used in this version.

---

## Pin Connections

### OLED Display Wiring

| OLED Pin | Board Pin | Purpose |
|---|---|---|
| VCC | 3.3V | Power for OLED |
| GND | GND | Ground |
| SDA | GPIO 20 / SDA | I2C data |
| SCL | GPIO 21 / SCL | I2C clock |

In the Arduino code, the OLED is initialized using:

```cpp
Wire.begin();
```

On SSTuino II, the board package usually maps this automatically to the board's SDA and SCL pins. If using a normal ESP32 board instead, you may need to use:

```cpp
Wire.begin(20, 21);
```

or change the numbers to match your board.

### Button Wiring

| Button Pin | Board Pin | Purpose |
|---|---|---|
| One leg | GPIO 8 | Digital input |
| Other leg | GND | Ground |

The button uses the internal pull-up resistor:

```cpp
pinMode(BUTTON_PIN, INPUT_PULLUP);
```

That means:

```text
Not pressed = HIGH
Pressed     = LOW
```

---

## Button Controls

| Action | Result |
|---|---|
| Short press | Toggle play/pause |
| Hold for 5 seconds | Skip to next track |

The code remembers whether Spotify was playing before the button was pressed. This prevents the OLED's temporary button message from breaking the play/pause logic.

---

## OLED Display Information

The OLED shows:

```text
Spotify Remote
State: Playing / Paused / Skipping / etc.
Song title
Artist name
API/debug status
```

Example:

```text
Spotify Remote
State: Playing
You're Where You...
Alan Menken
Track HTTP 200
```

The display text is shortened to fit the 128 × 64 OLED screen.

---

## Spotify API Requirements

This project uses Spotify Web API.

You need:

- Spotify Developer account
- Spotify app Client ID
- Spotify app Client Secret
- Spotify refresh token
- Spotify Premium for playback control features such as pause, play, and skip

Required Spotify scopes:

```text
user-read-playback-state
user-modify-playback-state
user-read-currently-playing
```

The refresh token is stored in a separate file called:

```text
arduino_secrets.h
```

Do **not** upload your real `arduino_secrets.h` file to GitHub.

---

## Secret File Setup

Create a file/tab named:

```text
arduino_secrets.h
```

Example:

```cpp
char ssid[] = "YOUR_WIFI_NAME";
char pass[] = "YOUR_WIFI_PASSWORD";

const char SPOTIFY_CLIENT_ID[] = "YOUR_CLIENT_ID";
const char SPOTIFY_CLIENT_SECRET[] = "YOUR_CLIENT_SECRET";
const char SPOTIFY_REFRESH_TOKEN[] = "YOUR_REFRESH_TOKEN";
```

For GitHub, create a safe example file instead:

```text
arduino_secrets_example.h
```

and keep the real secret file private.

---

## Recommended `.gitignore`

Add this to `.gitignore`:

```gitignore
arduino_secrets.h
```

This prevents your Wi-Fi password, Spotify client secret, and refresh token from being uploaded.

---

## Timing Settings

The latest stable timing settings are:

```cpp
const unsigned long TRACK_CHECK_INTERVAL = 3000;  // check Spotify every 3 seconds
delay(20);                                       // short loop delay for button responsiveness
```

Why not refresh every 1 second?

Refreshing every second makes the board spend too much time doing HTTPS requests, reading Spotify JSON, parsing data, and updating the OLED. This can cause button presses to be missed.

A 3-second refresh is a better balance.

---

## How the Code Works

### 1. Wi-Fi Connection

The board connects to Wi-Fi using the credentials from `arduino_secrets.h`.

### 2. Token Refresh

Spotify access tokens expire after a short time. The code uses the stored refresh token to request a new access token automatically.

The token refresh interval is:

```cpp
const unsigned long TOKEN_REFRESH_INTERVAL = 50UL * 60UL * 1000UL;
```

That means the board refreshes the access token about every 50 minutes.

### 3. Current Song Request

Every 3 seconds, the board calls Spotify's current playback endpoint and reads the currently playing track.

### 4. Stream Parsing

Instead of storing the entire Spotify JSON response in memory, the code reads the response as a stream. This is more stable on small boards because Spotify responses can be large.

The parser extracts:

- Song title
- Artist name
- Playing/paused status

### 5. Text Cleaning

Spotify may send UTF-8 characters that the OLED font cannot display, such as:

```text
’ “ ” – — …
```

The code converts them into OLED-friendly ASCII characters, for example:

```text
You’re Where You Belong → You're Where You Belong
```

### 6. Button Input

The button is checked frequently. A short press toggles play/pause. A 5-second hold skips to the next track.

---

## Troubleshooting

### OLED does not show anything

Check:

- OLED VCC is connected to 3.3V.
- OLED GND is connected to GND.
- SDA is connected to GPIO 20 / SDA.
- SCL is connected to GPIO 21 / SCL.
- OLED I2C address is usually `0x3C`.

### Button does not respond

Check:

- One button leg is connected to GPIO 8.
- The other button leg is connected to GND.
- The code uses `INPUT_PULLUP`.
- The main loop delay should be `delay(20);`, not a large delay.

### Skip works but pause/play does not

Check Spotify Premium and active playback device. Spotify controls usually require an active Spotify device.

### OLED shows strange characters

This can happen when Spotify sends Unicode characters. The latest code includes a text cleaner to convert many common unsupported characters.

### Track updates but button misses presses

Use:

```cpp
const unsigned long TRACK_CHECK_INTERVAL = 3000;
delay(20);
```

A 1-second refresh is too aggressive for this board and can make input detection unreliable.

---

## Current Feature Summary

| Feature | Status |
|---|---|
| OLED song display | Working |
| Artist display | Working |
| Spotify OAuth refresh token | Working |
| Auto token refresh | Working |
| Current track refresh | Every 3 seconds |
| Short press play/pause | Working / depends on active Spotify device |
| 5-second hold skip | Working |
| Potentiometer volume | Removed |
| OLED UTF-8 cleanup | Included |
| Main loop delay | 20 ms |

---

## Project Notes

This project is a good example of a real IoT system:

- Hardware input
- OLED user interface
- Wi-Fi networking
- HTTPS API calls
- OAuth authentication
- JSON stream parsing
- Cloud-connected device control

It is more advanced than a basic Arduino project because it connects embedded hardware directly to a real internet API.
