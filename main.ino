#include <arduinoFFT.h>
#include <FastLED.h>
#include <EasyButton.h>

#define LED_PIN_STRIP_ONE     5
#define LED_PIN_STRIP_TWO     6
#define BTN_PIN         7             // Connect a push button to this pin to change patterns
#define AUDIO_PIN 3


#define BRIGHTNESS  64
#define LED_TYPE    WS2812B
#define COLOR_ORDER GRB

//Colors
//                Red,Grn,blu
#define LC_BLACK        0,  0,  0
#define LC_CHARCOAL    50, 50, 50
#define LC_DARK_GREY  140,140,140
#define LC_GREY       185,185,185   
#define LC_LIGHT_GREY 250,250,250
#define LC_WHITE      255,255,255

#define LC_RED        255,  0,  0
#define LC_PINK       255,130,208

#define LC_GREEN        0,255,  0
#define LC_DARK_GREEN   0, 30,  0
#define LC_OLIVE       30, 30,  1

#define LC_BLUE         0,  0,255
#define LC_LIGHT_BLUE 164,205,255
#define LC_NAVY         0,  0, 30

#define LC_PURPLE     140,  0,255
#define LC_LAVENDER   218,151,255
#define LC_ORANGE     255,128,  0

#define LC_CYAN         0,255,255
#define LC_MAGENTA    255,  0,255
#define LC_YELLOW     255,255,  0

//button
#define LONG_PRESS_MS   200           // Number of ms to count as a long press
const int BRIGHTNESS_SETTINGS[3] = {5, 70, 200};  // 3 Integer array for 3 brightness settings (based on pressing+holding BTN_PIN)


//fft
#define SAMPLES         1024          // Must be a power of 2
#define SAMPLING_FREQ   40000         // Hz, must be 40000 or less due to ADC conversion time. Determines maximum frequency that can be analysed by the FFT Fmax=sampleF/2.
#define AMPLITUDE_MID       4500         // Depending on your audio source level, you may need to alter this value. Can be used as a 'sensitivity' control.
#define AMPLITUDE_BASS  3000
#define AMPLITUDE_TREBLE  7000
#define AMPLITUDE_LAST  2
#define NUM_BANDS       30            // To change this, you will need to change the bunch of if statements describing the mapping from bins to bands
#define NOISE           500           // Used as a crude noise filter, values below this are ignored
const uint8_t kMatrixWidth = 30;                          // Matrix width
const uint8_t kMatrixHeight = 10;                         // Matrix height
#define TOP            (kMatrixHeight - 0)                // Don't allow the bars to go offscreen
#define NUM_PIXELS_STRIP_ONE      150     // Total number of LEDs
#define NUM_PIXELS_STRIP_TWO      150     // Total number of LEDs

//strip stuff
CRGB leds_strip_one[NUM_PIXELS_STRIP_ONE];
CRGB leds_strip_two[NUM_PIXELS_STRIP_TWO];
CRGB color = CRGB(0,0,0);
uint8_t colorTimer = 0;


// Sampling and FFT stuff
unsigned int sampling_period_us;
int peak[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};              // The length of these arrays must be >= NUM_BANDS
int oldBarHeights[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
int bandValues[] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
float vReal[SAMPLES];
float vImag[SAMPLES];
unsigned long newTime;
ArduinoFFT<float> FFT = ArduinoFFT<float>(vReal, vImag, SAMPLES, SAMPLING_FREQ);

// Button stuff
int buttonPushCounter = 0;
bool autoChangePatterns = false;
EasyButton modeBtn(BTN_PIN);



void setup() {
  //setup strip one
  FastLED.addLeds<LED_TYPE, LED_PIN_STRIP_ONE, COLOR_ORDER>(leds_strip_one, NUM_PIXELS_STRIP_ONE).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness(  BRIGHTNESS );
  //setup strip two
  FastLED.addLeds<LED_TYPE, LED_PIN_STRIP_TWO, COLOR_ORDER>(leds_strip_two, NUM_PIXELS_STRIP_TWO).setCorrection( TypicalLEDStrip );
  FastLED.setBrightness(  BRIGHTNESS );

  modeBtn.begin();
  modeBtn.onPressed(changeMode);
  modeBtn.onPressedFor(LONG_PRESS_MS, brightnessButton);
  modeBtn.onSequence(3, 2000, startAutoMode);
  modeBtn.onSequence(5, 2000, brightnessOff);

  //sampling_period_us = round(1000000 * (1.0 / SAMPLING_FREQ));
  sampling_period_us = round(1000000 * (1.0 / SAMPLING_FREQ));
}

void changeMode() {
  Serial.println("Button pressed");
  if (FastLED.getBrightness() == 0) FastLED.setBrightness(BRIGHTNESS_SETTINGS[0]);  //Re-enable if lights are "off"
  autoChangePatterns = false;
  buttonPushCounter = (buttonPushCounter + 1) % 6;
}

void startAutoMode() {
  autoChangePatterns = true;
}

void brightnessButton() {
  if (FastLED.getBrightness() == BRIGHTNESS_SETTINGS[2])  FastLED.setBrightness(BRIGHTNESS_SETTINGS[0]);
  else if (FastLED.getBrightness() == BRIGHTNESS_SETTINGS[0]) FastLED.setBrightness(BRIGHTNESS_SETTINGS[1]);
  else if (FastLED.getBrightness() == BRIGHTNESS_SETTINGS[1]) FastLED.setBrightness(BRIGHTNESS_SETTINGS[2]);
  else if (FastLED.getBrightness() == 0) FastLED.setBrightness(BRIGHTNESS_SETTINGS[0]); //Re-enable if lights are "off"
}

void brightnessOff(){
  FastLED.setBrightness(0);  //Lights out
}

void loop() {

  modeBtn.read();

  
  // Reset bandValues[]
  for (int i = 0; i<NUM_BANDS; i++){
    bandValues[i] = 0;
  }

  // Sample the audio pin
  for (int i = 0; i < SAMPLES; i++) {
    newTime = micros();
    vReal[i] = analogRead(AUDIO_PIN); // A conversion takes about 9.7uS on an ESP32

    vImag[i] = 0;
    while ((micros() - newTime) < sampling_period_us) { /* chill */ }
  }

  // Compute FFT
  FFT.dcRemoval();
  FFT.windowing(FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFT.compute(FFT_FORWARD);
  FFT.complexToMagnitude();

  // Analyse FFT results
  for (int i = 2; i < (SAMPLES/2); i++){       // Don't use sample 0 and only first SAMPLES/2 are usable. Each array element represents a frequency bin and its value the amplitude.

    if (vReal[i] > NOISE) {                    // Add a crude noise filter

     //30 bands, 12kHz top band
      if (i<=2 )           bandValues[0]  += (int)vReal[i];
      if (i>2   && i<=3  ) bandValues[1]  += (int)vReal[i];
      if (i>3   && i<=4  ) bandValues[2]  += (int)vReal[i];
      if (i>4   && i<=5  ) bandValues[3]  += (int)vReal[i];
      if (i>5   && i<=6  ) bandValues[4]  += (int)vReal[i];
      if (i>6   && i<=7  ) bandValues[5]  += (int)vReal[i];
      if (i>7   && i<=8  ) bandValues[6]  += (int)vReal[i];
      if (i>8   && i<=9  ) bandValues[7]  += (int)vReal[i];
      if (i>9   && i<=10  ) bandValues[8]  += (int)vReal[i];
      if (i>10   && i<=11  ) bandValues[9]  += (int)vReal[i];
      if (i>11   && i<=13  ) bandValues[10]  += (int)vReal[i];
      if (i>13   && i<=14  ) bandValues[11]  += (int)vReal[i];
      if (i>14   && i<=16  ) bandValues[12]  += (int)vReal[i];
      if (i>16   && i<=20  ) bandValues[13]  += (int)vReal[i];
      if (i>20   && i<=24  ) bandValues[14]  += (int)vReal[i];
      if (i>24   && i<=28  ) bandValues[15]  += (int)vReal[i];
      if (i>28   && i<=34  ) bandValues[16]  += (int)vReal[i];
      if (i>34   && i<=41  ) bandValues[17]  += (int)vReal[i];
      if (i>41   && i<=47  ) bandValues[18]  += (int)vReal[i];
      if (i>47   && i<=56  ) bandValues[19]  += (int)vReal[i];
      if (i>56   && i<=67  ) bandValues[20]  += (int)vReal[i];
      if (i>67   && i<=81  ) bandValues[21]  += (int)vReal[i];
      if (i>81   && i<=97  ) bandValues[22]  += (int)vReal[i];
      if (i>97   && i<=116  ) bandValues[23]  += (int)vReal[i];
      if (i>116   && i<=139  ) bandValues[24]  += (int)vReal[i];
      if (i>139   && i<=166  ) bandValues[25]  += (int)vReal[i];
      if (i>166   && i<=198  ) bandValues[26]  += (int)vReal[i];
      if (i>198   && i<=236  ) bandValues[27]  += (int)vReal[i];
      if (i>236   && i<=282  ) bandValues[28]  += (int)vReal[i];
      if (i>282   && i<=320  ) bandValues[29]  += (int)vReal[i];
    

    }
  }

 
  // Process the FFT data into bar heights
  for (int band = 0; band < NUM_BANDS; band++) {
    int barHeight = 0;

    if(band <= 11){
      // Scale the bars for the display
      barHeight = bandValues[band] / AMPLITUDE_BASS;
    }else if(band >= 21){
      // Scale the bars for the display
      barHeight = bandValues[band] / AMPLITUDE_TREBLE;
    }else if(band >= 29){
      // Scale the bars for the display
      barHeight = bandValues[band] / AMPLITUDE_LAST;
    }
    else{
      // Scale the bars for the display
      barHeight = bandValues[band] / AMPLITUDE_MID;
    }
    
    if (barHeight > TOP) barHeight = TOP;

    // Small amount of averaging between frames
    //barHeight = ((oldBarHeights[band] * 1) + barHeight) / 2;

    //Debug Stuff
    // Serial.print("Band ");
    // Serial.print(band);
    // Serial.print(": ");
    // Serial.print(barHeight);
    // Serial.print(", raw bandValue: ");
    // Serial.print(bandValues[band]);
    // Serial.println(" ");
    // Serial.println("vReal 3");
    // Serial.print(vReal[3]);
    // delay(1000);


    // // Draw bars
    // switch (buttonPushCounter) {
    //   case 0:
    //     color = CRGB(LC_PURPLE);
    //     break;
    //   case 1:
    //     color = CRGB(LC_CYAN);
    //     break;
    //   case 2:
    //     color = CRGB(LC_MAGENTA);
    //     break;
    //   case 3:
    //     color = CRGB(LC_YELLOW);
    //     break;
    //   case 4:
    //     //changing bars
    //     break;
    //   case 5:
    //     color = CRGB(LC_WHITE);
    //     break;
    // }


   
    if(band < 15){
      //Even number (LED Strip in down direction)
      if((band+1) % 2 == 0){
        // Draw bars
        drawBarsDownDirectionLeftMatrix(band, barHeight);
      }else{ //Odd number (LED Strip in up direction)
        drawBarsUpDirectionLeftMatrix(band, barHeight);
      }
    }
    else{
      if((band+1) % 2 == 0){
        // Draw bars
        drawBarsUpDirectionRightMatrix(band, barHeight);
      }else{ //Odd number (LED Strip in up direction)
        drawBarsDownDirectionRightMatrix(band, barHeight);
      }
    }

  // Save oldBarHeights for averaging later
  oldBarHeights[band] = barHeight;
    
  }

  // Apply the colors to the LED strip     
  FastLED.show();

  
  
  // Decay peak
  EVERY_N_MILLISECONDS(60) {
    for (byte band = 0; band < NUM_BANDS; band++)
      if (peak[band] > 0) peak[band] -= 1;
    colorTimer++;
  }

  // Used in some of the patterns
  EVERY_N_MILLISECONDS(10) {
    colorTimer++;
  }

  EVERY_N_SECONDS(10) {
    if (autoChangePatterns) buttonPushCounter = (buttonPushCounter + 1) % 6;
  }

}

// PATTERNS BELOW //

void drawBarsUpDirectionLeftMatrix(int band, int barHeight) {
  int startPixel = band * TOP;
  if(oldBarHeights[band] > barHeight){
    for (int pixel = startPixel; pixel < startPixel + TOP; pixel++) {
      leds_strip_one[pixel] = CRGB(0,0,0);
    }
  }
  else{
    for (int pixel = startPixel; pixel < startPixel + barHeight; pixel++) {
      // leds_strip_one[pixel] = CRGB(0,255,0);
      // leds_strip_one[pixel] = CRGB(color);
      leds_strip_one[pixel] = ledColor(pixel);
    }
  }
    
}

void drawBarsDownDirectionLeftMatrix(int band, int barHeight) {
  int startPixel = ((band + 1) * TOP - 1);
  if(oldBarHeights[band] > barHeight){
    for (int pixel = startPixel; pixel >= startPixel - TOP; pixel--) {
      leds_strip_one[pixel] = CRGB(0,0,0);
    }
  }
  else{
    for (int pixel = startPixel; pixel > startPixel - barHeight; pixel--) {
      // leds_strip_one[pixel] = CRGB(0,0,255);
      // leds_strip_one[pixel] = CRGB(color);
      leds_strip_one[pixel] = ledColor(pixel);
    }
  }
}

void drawBarsUpDirectionRightMatrix(int band, int barHeight) {
  int startPixel = ((NUM_BANDS - 1 ) - band ) * TOP;
  if(oldBarHeights[band] > barHeight){
    for (int pixel = startPixel; pixel < startPixel + TOP; pixel++) {
      leds_strip_two[pixel] = CRGB(0,0,0);
    }
  }
  else{
    for (int pixel = startPixel; pixel < startPixel + barHeight; pixel++) {
      // leds_strip_two[pixel] = CRGB(0,255,0);
      // leds_strip_two[pixel] = CRGB(color);
      leds_strip_two[pixel] = ledColor(pixel);
    }
  }
}

void drawBarsDownDirectionRightMatrix(int band, int barHeight) {
  int startPixel = (((NUM_BANDS - band) * TOP)-1);
  if(oldBarHeights[band] > barHeight){
    for (int pixel = startPixel; pixel >= startPixel - TOP; pixel--) {
      leds_strip_two[pixel] = CRGB(0,0,0);
    }
  }
  else{
    for (int pixel = startPixel; pixel > startPixel - barHeight; pixel--) {
      // leds_strip_two[pixel] = CRGB(0,0,255);
      // leds_strip_two[pixel] = CRGB(color);
      leds_strip_two[pixel] = ledColor(pixel);
    }
  }
}

CRGB ledColor(int pixel){
  // Draw bars
  switch (buttonPushCounter) {
    case 0:
      return CRGB(LC_PURPLE);
      break;
    case 1:
      return CRGB(LC_CYAN);
      break;
    case 2:
      return CRGB(LC_MAGENTA);
      break;
    case 3:
      return CRGB(LC_YELLOW);
      break;
    case 4:
      return CHSV(pixel * (255 / kMatrixHeight) + colorTimer, 255, 255);
      break;
    case 5:
      return CRGB(LC_WHITE);
      break;
  }
}

