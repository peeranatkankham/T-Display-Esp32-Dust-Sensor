#include <TFT_eSPI.h>
#include <SPI.h>
#include "PMS.h"
#include <Button2.h>
#include <esp_adc_cal.h>
#include <Pangodream_18650_CL.h>
#include "icon.h"
#include "logo.h"
#include "driver/rtc_io.h"

#define RXD2 25  // To sensor TXD
#define TXD2 26  // To sensor RXD

#define BUTTON_PIN 0    // GPIO pin connected to the first button
#define BUTTON2_PIN 35  // GPIO pin connected to the second button
#define ADC_EN 14       //ADC_EN is the ADC detection enable port
#define TFT_BL 4

#define MIN_USB_VOL 4.3
#define ADC_PIN 34
#define CONV_FACTOR 1.8
#define READS 20

#define GREEN 0x1722
#define YELLOW 0xEF42
#define ORANGE TFT_ORANGE
#define RED TFT_RED
#define PURPLE TFT_MAGENTA

int vref = 1100;

TFT_eSPI tft = TFT_eSPI(135, 240);

PMS pms(Serial1);
PMS::DATA data;

String lastSensorData = "";
uint16_t lastFillColor = GREEN;

Button2 button = Button2(BUTTON_PIN);
Button2 button2 = Button2(BUTTON2_PIN);  // Initialize the second button
bool displayVoltage = false;
bool batt100 = false;
bool batt10 = false;
bool charge = false;

int currentState = 0;  // 0: mainscreen, 1: pm2.5, 2: pm1, 3: pm10

volatile bool buttonPressed = false;
volatile bool button2Pressed = false;  // Flag to indicate if the second button is pressed
volatile unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;  // Adjust as needed

Pangodream_18650_CL BL(ADC_PIN, CONV_FACTOR, READS);

uint16_t tabColor;
uint16_t lasttabColor;

static unsigned long screenOnTime = 0;


bool notLessThan10 = false;
bool notLessThan100 = false;
bool pm1_100 = false;
bool pm25_100 = false;
bool pm10_100 = false;

unsigned long lastButtonPressTime = 0;
const unsigned long debounceInterval = 200;  // Adjust debounce interval as needed (in milliseconds)

unsigned long period = 1000;
unsigned long last_time = 0;
unsigned long previousTime = 0;

unsigned long previousMillis = 0; 
const unsigned long intervaldelay = 2000;
uint64_t batlevel = 0;


void IRAM_ATTR handleInterrupt() {
  if (millis() - lastDebounceTime >= debounceDelay) {
    buttonPressed = true;
    lastDebounceTime = millis();
  }
}

void IRAM_ATTR handleInterrupt2() {
  if (millis() - lastDebounceTime >= debounceDelay) {
    button2Pressed = true;
    lastDebounceTime = millis();
  }
}

void setup() {
  Serial.begin(115200);
  Serial1.begin(9600, SERIAL_8N1, RXD2, TXD2);

  pms.wakeUp();

  pinMode(ADC_EN, OUTPUT);
  digitalWrite(ADC_EN, HIGH);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);  // Landscape mode

  tft.fillScreen(TFT_BLACK);
  tft.setSwapBytes(true);
  tft.pushImage(56, 3, 128, 128, aqi);
  // tft.drawBitmap(56, 3, air_quality, 128, 128, 0x229f);
  espDelay(5000);

  displayOriginalScreen();

  button.setClickHandler(handleButtonClick);
  button2.setClickHandler(handleButton2Click);  // Set click handler for the second button

  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Assuming your button is connected to GPIO 0
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleInterrupt, FALLING);
  pinMode(BUTTON2_PIN, INPUT_PULLUP);  // Assuming your second button is connected to GPIO 35
  attachInterrupt(digitalPinToInterrupt(BUTTON2_PIN), handleInterrupt2, FALLING);


  esp_adc_cal_characteristics_t adc_chars;
  esp_adc_cal_value_t val_type = esp_adc_cal_characterize(ADC_UNIT_1, ADC_ATTEN_DB_11, ADC_WIDTH_BIT_12, 1100, &adc_chars);  //Check type of calibration value used to characterize ADC
  if (val_type == ESP_ADC_CAL_VAL_EFUSE_VREF) {
    Serial.printf("eFuse Vref:%u mV", adc_chars.vref);
    vref = adc_chars.vref;
  } else if (val_type == ESP_ADC_CAL_VAL_EFUSE_TP) {
    Serial.printf("Two Point --> coeff_a:%umV coeff_b:%umV\n", adc_chars.coeff_a, adc_chars.coeff_b);
  } else {
    Serial.println("Default Vref: 1100mV");
  }
}

void loop() {
  if (buttonPressed) {
    handleButtonClick(button);
    buttonPressed = false;
  }

  if (button2Pressed) {
    handleButton2Click(button2);
    button2Pressed = false;
  }

  if (!displayVoltage) {
    if (currentState == 0) {
      mainscreen();
    } else if (currentState == 1) {
      pm25();
    } else if (currentState == 2) {
      pm1();
    } else if (currentState == 3) {
      pm10();
    }
  } else {
    showVoltage();
  }

  if (millis() - screenOnTime >= 5 * 60 * 1000) {  // 5 minutes in milliseconds
    putToSleep();
    screenOnTime = millis();
  }
}

void mainscreen() {
  if (pms.read(data)) {
    String currentSensorData = "PM 1.0: " + String(data.PM_AE_UG_1_0) + "  PM 2.5: " + String(data.PM_AE_UG_2_5) + "  PM 10.0: " + String(data.PM_AE_UG_10_0);

    if (currentSensorData != lastSensorData) {
      updateDisplay(currentSensorData);
      lastSensorData = currentSensorData;
    }
  }
}
void setAQIColor(uint16_t pm25Value) {
  uint16_t fillColor;

  if (pm25Value <= 12) {
    fillColor = GREEN;  // Good (0-12)
  } else if (pm25Value <= 35.4) {
    fillColor = YELLOW;  // Moderate (12.1-35.4)
  } else if (pm25Value <= 55.4) {
    fillColor = ORANGE;  // Unhealthy for Sensitive Groups (35.5-55.4)
  } else if (pm25Value <= 150.4) {
    fillColor = RED;  // Unhealthy (55.5-150.4)
  } else {
    fillColor = PURPLE;  // Very Unhealthy (150.5+)
  }

  if (fillColor != lastFillColor) {
    tft.fillRoundRect(150, 0, 100, 135, 15, fillColor);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.print("PM 2.5");
    tft.setCursor(10, 115);
    tft.print("ug/m3");
    if (fillColor == YELLOW) {
      tft.setTextColor(TFT_BLACK);
      // tft.fillRect(160, 65, 230, 70, TFT_BLACK);
    } else {
      tft.setTextColor(TFT_WHITE);  // Set text color to white
    }
    tft.setCursor(175, 10);
    tft.print("PM 1");
    tft.setCursor(170, 75);
    tft.print("PM 10");
    lastFillColor = fillColor;
  }
}

void setAQITextColor(uint16_t pm25Value) {
  if (pm25Value <= 12) {
    tft.setTextColor(TFT_WHITE, GREEN);  // Good (0-12)
  } else if (pm25Value <= 35.4) {
    tft.setTextColor(TFT_BLACK, YELLOW);  // Moderate (12.1-35.4)
  } else if (pm25Value <= 55.4) {
    tft.setTextColor(TFT_WHITE, ORANGE);  // Unhealthy for Sensitive Groups (35.5-55.4)
  } else if (pm25Value <= 150.4) {
    tft.setTextColor(TFT_WHITE, RED);  // Unhealthy (55.5-150.4)
  } else {
    tft.setTextColor(TFT_WHITE, PURPLE);  // Very Unhealthy (150.5+)
  }
}

void updateDisplay(const String& sensorData) {
  setAQIColor(data.PM_AE_UG_2_5);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(6);  // Set text size
  if (data.PM_AE_UG_2_5 < 10) {
    tft.fillRect(80, 50, 50, 50, TFT_BLACK);
  }
  if (data.PM_AE_UG_2_5 > 99) {
    pm25_100 = true;
    tft.setCursor(30, 50);
  } else {
    if (pm25_100 == true){
      tft.fillRect(0, 50, 150, 50, TFT_BLACK);
      pm25_100 = false;
    }
    tft.setCursor(50, 50);
  }
  tft.println(data.PM_AE_UG_2_5);
  setAQITextColor(data.PM_AE_UG_2_5);

  tft.setTextSize(3);
  if (data.PM_AE_UG_1_0 > 99) {
    pm1_100 = true;
    tft.setCursor(172, 35);
  } else {
    if (pm1_100 == true){
      tft.fillRect(170, 35, 55, 25, RED);
      pm1_100 = false;
    }
    tft.setCursor(185, 35);
  }
  if (data.PM_AE_UG_1_0 < 10 && data.PM_AE_UG_10_0 < 10) {
    tft.fillRect(200, 35, 25, 25, GREEN);
  } else if (data.PM_AE_UG_1_0 < 10 && data.PM_AE_UG_2_5 > 12) {
    tft.fillRect(200, 35, 25, 25, YELLOW);
  }
  tft.println(data.PM_AE_UG_1_0);

  if (data.PM_AE_UG_10_0 > 99) {
    pm10_100 = true;
    tft.setCursor(172, 100);
  } else {
    if (pm10_100 == true){
      tft.fillRect(170, 100, 60, 30, RED);
      pm10_100 = false;
    }
    tft.setCursor(185, 100);
  }
  if (data.PM_AE_UG_10_0 < 10) {
    tft.fillRect(200, 100, 30, 30, GREEN);
  }
  tft.println(data.PM_AE_UG_10_0);
  if (BL.getBatteryChargeLevel() < 5) {
    tft.drawBitmap(110, 110, bat, 24, 24, RED);
  }
}

void displayOriginalScreen() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextWrap(true);
  tft.setTextSize(2);
  tft.fillRoundRect(150, 0, 100, 135, 15, GREEN);
  tft.setCursor(10, 10);
  tft.print("PM 2.5");
  tft.setCursor(10, 115);
  tft.print("ug/m3");
  tft.setTextSize(2);
  tft.setCursor(175, 10);
  tft.print("PM 1");
  tft.setCursor(170, 75);
  tft.print("PM 10");
}

void pm25() {
  if (pms.read(data)) {
    String currentSensorData = "PM 1.0: " + String(data.PM_AE_UG_1_0) + "  PM 2.5: " + String(data.PM_AE_UG_2_5) + "  PM 10.0: " + String(data.PM_AE_UG_10_0);
    if (currentSensorData != lastSensorData) {
      changeAQIColor(data.PM_AE_UG_2_5);
      if (tabColor != lasttabColor) {
        buttom_bar(1);
      }
      showpm(data.PM_AE_UG_2_5);
      lastSensorData = currentSensorData;
    }
  }
}

void pm1() {
  if (pms.read(data)) {
    String currentSensorData = "PM 1.0: " + String(data.PM_AE_UG_1_0) + "  PM 2.5: " + String(data.PM_AE_UG_2_5) + "  PM 10.0: " + String(data.PM_AE_UG_10_0);
    if (currentSensorData != lastSensorData) {
      changeAQIColor(data.PM_AE_UG_2_5);
      if (tabColor != lasttabColor) {
        buttom_bar(2);
      }
      showpm(data.PM_AE_UG_1_0);
      lastSensorData = currentSensorData;
    }
  }
}

void pm10() {
  if (pms.read(data)) {
    String currentSensorData = "PM 1.0: " + String(data.PM_AE_UG_1_0) + "  PM 2.5: " + String(data.PM_AE_UG_2_5) + "  PM 10.0: " + String(data.PM_AE_UG_10_0);
    if (currentSensorData != lastSensorData) {
      changeAQIColor(data.PM_AE_UG_2_5);
      if (tabColor != lasttabColor) {
        buttom_bar(3);
      }
      showpm(data.PM_AE_UG_10_0);
      lastSensorData = currentSensorData;
    }
  }
}

void showpm(uint16_t pmValue) {
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(7);
  if (pmValue < 10) {
    if (notLessThan10 == false) {
      tft.fillRoundRect(0, 0, 240, 105, 20, TFT_BLACK);
      notLessThan10 = true;
    }
    tft.setCursor(105, 25);
  } else if (pmValue > 99) {
    tft.setCursor(70, 25);
    notLessThan100 = true;
  } else {
    if (notLessThan100 == true) {
      tft.fillRoundRect(0, 0, 240, 105, 20, TFT_BLACK);
      notLessThan100 = false;
    }
    notLessThan10 = false;

    tft.setCursor(85, 25);
  }
  tft.println(pmValue);
}

void buttom_bar(uint16_t con) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  buttom_setAQITextColor(data.PM_AE_UG_2_5);
  tft.fillRoundRect(0, 0, 240, 105, 20, TFT_BLACK);
  tft.fillRect(0, 0, 240, 30, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(172, 110);
  tft.print("ug/m3");
  tft.setTextSize(2);
  tft.setCursor(5, 110);
  switch (con) {
    case 1:
      tft.print("PM 2.5");
      break;
    case 2:
      tft.print("PM 1.0");
      break;
    case 3:
      tft.print("PM 10");
      break;
  }
  tft.setTextColor(TFT_WHITE);
}

void buttom_setAQITextColor(uint16_t pm25Value) {
  if (pm25Value <= 12) {
    tft.fillRect(0, 90, 240, 40, GREEN);  // Good (0-12)
    lasttabColor = GREEN;
  } else if (pm25Value <= 35.4) {
    tft.fillRect(0, 90, 240, 40, YELLOW);
    tft.setTextColor(TFT_BLACK);  // Moderate (12.1-35.4)
    lasttabColor = YELLOW;
  } else if (pm25Value <= 55.4) {
    tft.fillRect(0, 90, 240, 40, ORANGE);  // Unhealthy for Sensitive Groups (35.5-55.4)
    lasttabColor = ORANGE;
  } else if (pm25Value <= 150.4) {
    tft.fillRect(0, 90, 240, 40, RED);  // Unhealthy (55.5-150.4)
    lasttabColor = RED;
  } else {
    tft.fillRect(0, 90, 240, 40, PURPLE);  // Very Unhealthy (150.5+)
    lasttabColor = PURPLE;
  }
}

void changeAQIColor(uint16_t pm25Value) {
  if (pm25Value <= 12) {
    tabColor = GREEN;  // Good (0-12)
  } else if (pm25Value <= 35.4) {
    tabColor = YELLOW;  // Moderate (12.1-35.4)
  } else if (pm25Value <= 55.4) {
    tabColor = ORANGE;  // Unhealthy for Sensitive Groups (35.5-55.4)
  } else if (pm25Value <= 150.4) {
    tabColor = RED;  // Unhealthy (55.5-150.4)
  } else {
    tabColor = PURPLE;  // Very Unhealthy (150.5+)
  }
}


void handleButtonClick(Button2& btn) {
  charge = false;
  if (displayVoltage == true) {
    displayVoltage = false;
  }
  currentState++;
  if (currentState == 1) {
    buttom_bar(1);
    lastSensorData = " ";
  } else if (currentState == 2) {
    buttom_bar(2);
    lastSensorData = " ";
  } else if (currentState == 3) {
    buttom_bar(3);
    lastSensorData = " ";
  }
  if (currentState > 3) {
    currentState = 0;
    lastFillColor = 0;
    lastSensorData = " ";
    displayOriginalScreen();
    mainscreen();
  }
}


void handleButton2Click(Button2& btn) {
  charge = false;
  unsigned long currentMillis = millis();
  // Check if sufficient time has passed since the last button press
  if (currentMillis - lastButtonPressTime < debounceInterval) {
    return;  // Ignore the button press
  }
  // Update the last button press time
  lastButtonPressTime = currentMillis;
  // Toggle voltage display flag

  displayVoltage = !displayVoltage;

  if (displayVoltage) {
    tft.fillScreen(TFT_BLACK);
    tft.fillRoundRect(37, 20, 160, 80, 15, TFT_WHITE);
    tft.fillRoundRect(42, 25, 150, 70, 10, TFT_BLACK);
    tft.fillRoundRect(192, 39, 15, 42, 4, TFT_WHITE);

  } else {
    currentState = 0;
    lastFillColor = 0;
    lastSensorData = " ";
    displayOriginalScreen();
    mainscreen();
  }
}

void showVoltage() {
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(7);

  if (BL.getBatteryVolts() >= MIN_USB_VOL) {
    if (charge == false) {
      tft.fillRoundRect(42, 25, 150, 70, 10, GREEN);
      charge = true;
    }
    tft.drawBitmap(88, 28, lightning, 64, 64, TFT_WHITE);
  } else {
    if (charge == true) {
      tft.fillRoundRect(42, 25, 150, 70, 10, TFT_BLACK);
    }
    charge = false;
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= intervaldelay) {
      batlevel = BL.getBatteryChargeLevel();
      previousMillis = currentMillis;
    }
    if (batlevel > 99) {
      tft.setCursor(55, 34);
      batt100 = true;
    } else if (batlevel < 10) {
      if (batt10 == false) {
        tft.fillRoundRect(42, 25, 150, 70, 10, TFT_BLACK);
        batt10 = true;
      }
      // tft.fillRoundRect(42, 25, 150, 70, 10, TFT_BLACK);
      tft.setCursor(105, 34);
    } else {
      if (batt100 == true) {
        tft.fillRoundRect(42, 25, 150, 70, 10, TFT_BLACK);
        batt100 = false;
      }
      batt10 = false;
      tft.setCursor(80, 34);
    }
    tft.println(batlevel);
  }

  if (millis() - last_time > period) {
    last_time = millis();
    tft.setTextSize(2);
    tft.setCursor(180, 120);
    tft.print(BL.getBatteryVolts());
    tft.println("V");
  }
}

void putToSleep() {
  
  // Turn off display
  tft.fillScreen(TFT_BLACK);
  int r = digitalRead(TFT_BL);
  delay(1000);
  digitalWrite(TFT_BL, !r);

  tft.writecommand(TFT_DISPOFF);
  tft.writecommand(TFT_SLPIN);
  pms.sleep();
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_TIMER);
  rtc_gpio_init(GPIO_NUM_14);
  rtc_gpio_set_direction(GPIO_NUM_14, RTC_GPIO_MODE_OUTPUT_ONLY);
  rtc_gpio_set_level(GPIO_NUM_14, 1);
  delay(500);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);
  delay(200);
  esp_deep_sleep_start();
}

void espDelay(int ms) {
  esp_sleep_enable_timer_wakeup(ms * 1000);
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  esp_light_sleep_start();
}
