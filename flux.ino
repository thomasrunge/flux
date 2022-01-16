#include <FastLED.h>
#include <OneButton.h>
#include "palettes.h"


// 50 FPS: 1000/50 = 20 ms
#define FLASH_EVERY_MS 20
#define FLASH_DURATION_MS 2

#define ACCEL_EVERY_MS 250

#define HAL_PIN 2
#define FL_PIN 3
#define PHASE_PIN 4 
#define ENABLE_PIN 5
#define BUTTON_PIN A3

#define LED_TYPE    WS2811
#define COLOR_ORDER GRB
#define NUM_LEDS    12


#define SECONDS_PER_PALETTE 10
uint8_t gCurrentPaletteNumber = 0;
CRGBPalette16 gCurrentPalette(CRGB::Black);
CRGBPalette16 gTargetPalette(gGradientPalettes[0]);

OneButton button(BUTTON_PIN, true);
boolean anim_stopped;

CRGB leds[NUM_LEDS];
uint8_t hue;
unsigned long flash_start, rpm_stat, rpm_change;
float vmotor, motor_step;
volatile uint16_t revolutiontime;
volatile unsigned long last_revolution;

void colorwaves();
void heartbeat();
void flash();

typedef enum {
  STATE_STOPPED=0,
  STATE_ACCEL,
  STATE_ROT,
  STATE_STOPPING
} state_t;

typedef struct {
  state_t state;     // what is the current animation
  uint16_t duration; // how many milli seconds
  uint8_t motor;     // motor regulation
  void (*fnc)();     // operate leds
} flux_state_t;

flux_state_t flux_anim[] = {
  { STATE_STOPPED,   7000, 0,  heartbeat  },
  { STATE_ACCEL,     4000, 68, heartbeat  },
  { STATE_ROT,       3000, 68, heartbeat  },
  { STATE_ROT,      15000, 68, flash      },
  { STATE_STOPPING,  4000,  0, colorwaves },
  { STATE_STOPPED,   7000,  0, colorwaves },
  { STATE_ACCEL,     4000, 68, colorwaves },
  { STATE_ROT,       3000, 68, colorwaves },
  { STATE_ROT,      15000, 68, flash      },
  { STATE_STOPPING,  4000, 0,  heartbeat  }
};
unsigned long state_changed;
uint8_t state_idx;
uint8_t state_max;

// This function draws color waves with an ever-changing,
// widely-varying set of parameters, using a color palette.
void colorwaves() {
  static uint16_t sPseudotime = 0;
  static uint16_t sLastMillis = 0;
  static uint16_t sHue16 = 0;
 
  //uint8_t sat8 = beatsin88( 87, 220, 250);
  uint8_t brightdepth = beatsin88( 341, 96, 224);
  uint16_t brightnessthetainc16 = beatsin88( 203, (25 * 256), (40 * 256));
  uint8_t msmultiplier = beatsin88(147, 23, 60);

  uint16_t hue16 = sHue16;//gHue * 256;
  uint16_t hueinc16 = beatsin88(113, 300, 1500);
  
  uint16_t ms = millis();
  uint16_t deltams = ms - sLastMillis ;
  sLastMillis  = ms;
  sPseudotime += deltams * msmultiplier;
  sHue16 += deltams * beatsin88( 400, 5,9);
  uint16_t brightnesstheta16 = sPseudotime;
  
  for(uint16_t i = 0 ;i < NUM_LEDS; i++) {
    hue16 += hueinc16;
    uint8_t hue8 = hue16 / 256;
    uint16_t h16_128 = hue16 >> 7;
    if( h16_128 & 0x100) {
      hue8 = 255 - (h16_128 >> 1);
    } else {
      hue8 = h16_128 >> 1;
    }

    brightnesstheta16  += brightnessthetainc16;
    uint16_t b16 = sin16( brightnesstheta16  ) + 32768;

    uint16_t bri16 = (uint32_t)((uint32_t)b16 * (uint32_t)b16) / 65536;
    uint8_t bri8 = (uint32_t)(((uint32_t)bri16) * brightdepth) / 65536;
    bri8 += (255 - brightdepth);
    
    uint8_t index = hue8;
    index = scale8(index, 240);

    CRGB newcolor = ColorFromPalette(gCurrentPalette, index, bri8);

    uint16_t pixelnumber = i;
    pixelnumber = (NUM_LEDS-1) - pixelnumber;
    
    nblend(leds[pixelnumber], newcolor, 128);
  }
}

void heartbeat() {
  float breath = 60+80*(exp(sin(millis()/2000.*PI))-0.4);
  fill_solid(leds, NUM_LEDS, CHSV(0, 255, breath));
}

void flash() {
  unsigned long m = millis();

  if (m > flash_start) {
    flash_start = m+FLASH_EVERY_MS;
    fill_solid(leds, NUM_LEDS, CHSV(hue, 255, 255));
    FastLED.delay(FLASH_DURATION_MS);
    fill_solid(leds, NUM_LEDS, CRGB::Black);
  }
}

void on_magnet() {
  unsigned long m = millis();

  revolutiontime = m - last_revolution;
  last_revolution = m;
}

void cb_click() {
  anim_stopped = !anim_stopped;
}

void nextState() {
  if (anim_stopped) {
    return;
  }

  state_idx++;
  if (state_idx >= state_max) {
    state_idx = 0;
  }
  state_changed = millis();

  Serial.print("nextState: ");
  Serial.println(state_idx);

  if (flux_anim[state_idx].state == STATE_STOPPING || flux_anim[state_idx].state == STATE_ACCEL) {
    motor_step = ACCEL_EVERY_MS*(float)(abs((flux_anim[state_idx].motor-vmotor)))/(float)(flux_anim[state_idx].duration);
    Serial.print("duration: ");
    Serial.print(flux_anim[state_idx].duration);
    Serial.print(", motor ist: ");
    Serial.print(vmotor);
    Serial.print(", motor soll: ");
    Serial.print(flux_anim[state_idx].motor);
    Serial.print(", motor_step=");
    Serial.println(motor_step);
  } else {
    vmotor = flux_anim[state_idx].motor;
    analogWrite(ENABLE_PIN, vmotor);
  }
}

void setup() {
  hue = 0;
  flash_start = 0;
  rpm_stat = 0;
  rpm_change = 0;
  revolutiontime = 0xFFFF;
  state_idx = 0;
  state_changed = millis();
  state_max = sizeof(flux_anim) / sizeof(flux_anim[0]);
  vmotor = 0.;
  motor_step = 0;

  Serial.begin(57600);
  Serial.println("Hello");

  button.attachClick(cb_click);
  pinMode(PHASE_PIN, OUTPUT);
  digitalWrite(PHASE_PIN, LOW);
  analogWrite(ENABLE_PIN, vmotor);
  attachInterrupt(digitalPinToInterrupt(HAL_PIN), on_magnet, CHANGE);

  FastLED.addLeds<LED_TYPE, FL_PIN, COLOR_ORDER>(leds, NUM_LEDS).setCorrection(TypicalLEDStrip);
  FastLED.setMaxPowerInVoltsAndMilliamps(6, 2000);
}

void loop() {

  EVERY_N_MILLISECONDS(40) {
    button.tick();
    hue++;
    nblendPaletteTowardPalette(gCurrentPalette, gTargetPalette, 16);
  }
  EVERY_N_SECONDS(SECONDS_PER_PALETTE) {
    gCurrentPaletteNumber = addmod8(gCurrentPaletteNumber, 1, gGradientPaletteCount);
    gTargetPalette = gGradientPalettes[gCurrentPaletteNumber];
  }

  unsigned long m = millis();

  // Dauer der Animation abgelaufen, Umschalten zur nächsten
  if (m > state_changed + flux_anim[state_idx].duration) {
    nextState();
  }

  // LED animation ausführen
  flux_anim[state_idx].fnc();

  switch (flux_anim[state_idx].state) {
    case STATE_STOPPED:
    break;
    case STATE_STOPPING:
      if (m > rpm_change) {
        if (vmotor != flux_anim[state_idx].motor) {
          vmotor -= motor_step;
          if (vmotor < flux_anim[state_idx].motor) {
            vmotor = flux_anim[state_idx].motor;
          }
          Serial.print("new vmotor (s): ");
          Serial.println(vmotor);
        }
        analogWrite(ENABLE_PIN, vmotor);
        rpm_change = m + ACCEL_EVERY_MS;
      }
    break;
    case STATE_ACCEL:
      if (m > rpm_change) {
        if (vmotor != flux_anim[state_idx].motor) {
          vmotor += motor_step;
          if (vmotor > flux_anim[state_idx].motor) {
            vmotor = flux_anim[state_idx].motor;
          }
          Serial.print("new vmotor (a): ");
          Serial.println(vmotor);
        }
        analogWrite(ENABLE_PIN, vmotor);
        rpm_change = m + ACCEL_EVERY_MS;
      }
    break;
    case STATE_ROT:
      if (m > rpm_stat) {
        float rps = 1000./revolutiontime/2.;
        float rpm = rps*60;
        Serial.print("revs/sec:");
        Serial.print(rps);
        Serial.print(", RPM:");
        Serial.println(rpm);
        rpm_stat = m+1000;
      }
    break;
    default:
    break;
  }

  FastLED.show();
}

