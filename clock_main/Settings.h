// ===============================================================================
// Update these values to match your build and board type if not using D1 Mini
#define NUM_LEDS 86                           // Total of 86 LED's if built as shown    
#define MILLI_AMPS 6000                       // Update to match max milliamp output of your power suppy (or about 500 milliamp lower than max to be safe)
#define DATA_PIN D6                           // Change this if you are using another type of ESP board than a WeMos D1 Mini
#define COUNTDOWN_OUTPUT D5                   // Output pin to drive buzzer or other device upon countdown expiration
#define WIFIMODE 2                            // 0 = Only Soft Access Point, 1 = Only connect to local WiFi network with UN/PW, 2 = Both
#define MQTTMODE 1                            // 0 = Disable MQTT, 1 = Enable (will only be enabled if WiFi mode = 1 or 2 - broker must be on same network)
#define MQTTSERVER "192.168.1.108"            // IP Address (or url) of MQTT Broker. Use '0.0.0.0' if not enabling MQTT
#define MQTTPORT 1883                         // Port of MQTT Broker.  This is usually 1883
// Optional pushbuttons (colors listed match ones used in build instructions)
// All pins need to be LOW when button open or code modifications will be required in main sketch
#define MODE_PIN D3                           // Push button (white): Change Mode
#define V1_PIN D4                             // Push button (red): Visitor+/Minutes Add button
#define V0_PIN 3                              // Push button (green): Visitor-/Seconds Add button (RX pin)
#define H1_PIN D7                             // Push button (yellow): Home+/Countdown start button
#define H0_PIN 1                              // Push button (blue): Home-/Countdown stop/pause button (TX pin)

// ---------------------------------------------------------------------------------------------------------------
// Options - Defaults upon boot-up.  Most values can be updated via web app or via Home Assistant/MQTT  after boot
// ---------------------------------------------------------------------------------------------------------------

byte clockMode = 0;                             // Default mode at boot: 0=Clock, 1=Countdown, 2=Temperature, 3=Scoreboard
byte brightness = 255;                          // Default starting brightness at boot. 255=max brightness based on milliamp rating of power supply
byte temperatureSymbol = 13;                    // Default temp display: 12=Celcius, 13=Fahrenheit
float temperatureCorrection = -3.0;             // Temp from RTC module.  Generally runs "hot" due to heat from chip.  Adjust as needed.
unsigned long temperatureUpdatePeriod = 180000; // How often, in milliseconds to update MQTT time. Recommend minimum of 60000 (one minute) or greater. Set to 0 to disable updates.
byte hourFormat = 12;                           // Change this to 24 if you want default 24 hours format instead of 12     
byte scoreboardLeft = 0;                        // Starting "Visitor" (left) score on scoreboard
byte scoreboardRight = 0;                       // Starting "Home" (right) score on scoreboard

// Default starting colors for modes
// Named colors can be used.  Valid values found at: http://fastled.io/docs/3.1/struct_c_r_g_b.html
// Alternatively, you can specify each of the following as rgb values.  Example: CRGB clockColor = CRBG(0,0,255);

CRGB clockColor = CRGB:: Blue;
CRGB countdownColor = CRGB::Green;
CRGB countdownColorPaused = CRGB::Orange;     // If different from countdownColor, countdown color will change to this when paused/stopped.
CRGB countdownColorFinalMin = CRGB::Red;      // If different from countdownColor, countdown color will change to this for final 60 seconds.
CRGB temperatureColor = CRGB::Turquoise;
CRGB scoreboardColorLeft = CRGB::Red;
CRGB scoreboardColorRight = CRGB::Green;
CRGB alternateColor = CRGB::Black;            // Recommend to leave as Black. Otherwise unused pixels will be lit in digits
