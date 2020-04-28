//#include <Arduino.h>
#include <stdio.h>
#include <cstdarg>

// STM32 configuration
const int water_level_pin = PB13;
const int relay_state_pin = PB14;
const int trigger_pin = PB12;

// ESP8266 configuration
// const int water_level_pin = 4;
// const int relay_state_pin = 14;
// const int trigger_pin = 5;

const unsigned long water_level_bounce_time = 2000;
const unsigned long wait_until_relay_opened_time_max = 10000;
const unsigned long wait_until_relay_opened_time_min = 7000;
const unsigned long trigger_push_time = 500;
const unsigned long delay_before_button_and_relay = 800;

char b[1024];

void log(const char *format, ...)
{
  String t = String((double)millis()/1000, 3);
  snprintf(b, 1024, "--> [%s] ", t.c_str());
  Serial.print(b);
  va_list argptr;
  va_start(argptr, format);
  snprintf(b, 1024, format, argptr);
  Serial.print(b);
  va_end(argptr);
  Serial.println();
}

enum RelayState
{
  RELAY_CLOSED,
  RELAY_OPENED,
  RELAY_OPENNING,
  RELAY_CLOSING,
  RELAY_INTERRUPTED_OPENNING,
  RELAY_INTERRUPTED_CLOSING,
  RELAY_CLOSED_WAIT_EXTRA_ROTATION,
  RELAY_OPENED_WAIT_EXTRA_ROTATION,
};

enum ButtonState
{
  BUTTON_STOP,
  BUTTON_START_PUSHING,
  BUTTON_PUSHING,
  BUTTON_RELEASING,
};

enum SensorState
{
  SENSOR_OFF,
  SENSOR_ON,
  SENSOR_CHECK_OFF,
  SENSOR_CHECK_ON,
};

class RelayController
{
private:
  uint32_t last_button_time = 0;
  uint32_t last_sensor_time = 0;
  uint32_t last_relay_time = 0;
  uint32_t prev_relay_time = 0;
  RelayState relay_state = RELAY_CLOSED;
  SensorState sensor_state = SENSOR_OFF;
  ButtonState button_state = BUTTON_STOP;
  uint32_t interrupted_wait_time = 0;
  bool required_extra_rotation = false;

public:
  bool prev_relay_state = false;
  bool prev_sensor_state = false;

public:
  RelayController() {};
  void loop(bool relay_state, bool water_level_state);
};

void RelayController::loop(bool new_relay_state, bool new_sensor_state)
{
  uint32_t cur_time = millis();
  switch (relay_state)
  {
  case RELAY_CLOSED:
    if (new_relay_state != prev_relay_state)
    {
      if (new_relay_state)
      {
        relay_state = RELAY_OPENNING;
        log("RELAY_OPENNING");
        last_relay_time = cur_time;
      }
    }
    break;
  case RELAY_OPENED:
    if (new_relay_state != prev_relay_state)
    {
      if (!new_relay_state)
      {
        relay_state = RELAY_CLOSING;
        log("RELAY_CLOSING");
        last_relay_time = cur_time;
      }
    }
    if (sensor_state == SENSOR_ON && button_state == BUTTON_STOP)
    {
      button_state = BUTTON_START_PUSHING;
    }
    break;
  case RELAY_OPENNING:
    if (cur_time - last_relay_time >= wait_until_relay_opened_time_max - interrupted_wait_time)
    {
      relay_state = required_extra_rotation ? RELAY_OPENED_WAIT_EXTRA_ROTATION : RELAY_OPENED;
      last_relay_time = cur_time;
      log("RELAY_OPENED");
      if (interrupted_wait_time) {
        snprintf(b, 1024, "- short time %d\n", (int)(wait_until_relay_opened_time_max - interrupted_wait_time));
        Serial.print(b);
        // Serial.printf("- short time %d\n", (int)(wait_until_relay_opened_time_max - interrupted_wait_time));
      }
      interrupted_wait_time = 0;
    }
    else if (new_relay_state != prev_relay_state)
    {
      interrupted_wait_time = cur_time - last_relay_time;
      relay_state = RELAY_INTERRUPTED_OPENNING;
      log("RELAY_INTERRUPTED_OPENNING");
      last_relay_time = cur_time;
    }
    break;
  case RELAY_CLOSING:
    if (cur_time - last_relay_time >= wait_until_relay_opened_time_max - interrupted_wait_time)
    {
      relay_state = required_extra_rotation ? RELAY_CLOSED_WAIT_EXTRA_ROTATION : RELAY_CLOSED;
      last_relay_time = cur_time;
      log("RELAY_CLOSED");
      if (interrupted_wait_time) {
        snprintf(b, 1024, "- short time %d\n", (int)(wait_until_relay_opened_time_max - interrupted_wait_time));
        Serial.print(b);
        // Serial.printf("- short time %d\n", (int)(wait_until_relay_opened_time_max - interrupted_wait_time));
      }
      interrupted_wait_time = 0;
    }
    else if (new_relay_state != prev_relay_state)
    {
      interrupted_wait_time = cur_time - last_relay_time;
      last_relay_time = cur_time;
      relay_state = RELAY_INTERRUPTED_CLOSING;
      log("RELAY_INTERRUPTED_CLOSING");
    }
    break;
  case RELAY_INTERRUPTED_OPENNING:
    if (button_state == BUTTON_STOP)
    {
      button_state = BUTTON_START_PUSHING;
      relay_state = RELAY_CLOSED;
      log("RELAY_CLOSED");
      last_relay_time = cur_time;
      required_extra_rotation = true;
    }
    break;
  case RELAY_INTERRUPTED_CLOSING:
    if (button_state == BUTTON_STOP)
    {
      button_state = BUTTON_START_PUSHING;
      relay_state = RELAY_OPENED;
      log("RELAY_OPENED");
      last_relay_time = cur_time;
      required_extra_rotation = true;
    }
    break;
  case RELAY_CLOSED_WAIT_EXTRA_ROTATION:
    required_extra_rotation = false;
    // keep valve closed
    if (sensor_state == SENSOR_ON) {
      relay_state = RELAY_CLOSED;
    }
    else if (button_state == BUTTON_STOP)
    {
      button_state = BUTTON_START_PUSHING;
      relay_state = RELAY_CLOSED;
    }
    break;
  case RELAY_OPENED_WAIT_EXTRA_ROTATION:
    required_extra_rotation = false;
    if (button_state == BUTTON_STOP)
    {
      button_state = BUTTON_START_PUSHING;
      relay_state = RELAY_OPENED;
    }
    break;
  default:
    break;
  }

  switch (sensor_state)
  {
  case SENSOR_OFF:
    if (new_sensor_state)
    {
      sensor_state = SENSOR_CHECK_ON;
      log("SENSOR_CHECK_ON");
      last_sensor_time = cur_time;
    }
    break;
  case SENSOR_ON:
    if (!new_sensor_state)
    {
      sensor_state = SENSOR_CHECK_OFF;
      log("SENSOR_CHECK_OFF");
      last_sensor_time = cur_time;
    }
    // close valve if it is open
    if (relay_state == RELAY_OPENED && button_state == BUTTON_STOP)
    {
      button_state = BUTTON_START_PUSHING;
    }
    break;
  case SENSOR_CHECK_OFF:
    if (cur_time - last_sensor_time >= water_level_bounce_time)
    {
      sensor_state = new_sensor_state ? SENSOR_ON : SENSOR_OFF;
      snprintf(b, 1024, "  - new sensor state: %d\n", new_sensor_state);
      Serial.print(b);
      // Serial.printf("  - new sensor state: %d\n", new_sensor_state);
      // log("new sensor state: %d", new_sensor_state);
      last_sensor_time = cur_time;
    }
    break;
  case SENSOR_CHECK_ON:
    if (cur_time - last_sensor_time >= water_level_bounce_time)
    {
      sensor_state = new_sensor_state ? SENSOR_ON : SENSOR_OFF;
      snprintf(b, 1024, "  - new sensor state: %d\n", new_sensor_state);
      Serial.print(b);
      // Serial.printf("  - new sensor state: %d\n", new_sensor_state);
      // log("new sensor state: %d", new_sensor_state);
      last_sensor_time = cur_time;
    }
    break;
  default:
    break;
  }

  switch (button_state)
  {
  case BUTTON_STOP:
    digitalWrite(trigger_pin, HIGH);
    break;
  case BUTTON_START_PUSHING:
    last_button_time = cur_time;
    button_state = BUTTON_PUSHING;
    log("BUTTON_START_PUSHING");
    break;
  case BUTTON_PUSHING:
    digitalWrite(trigger_pin, LOW);
    if (cur_time - last_button_time >= trigger_push_time)
    {
      log("BUTTON_RELEASING");
      button_state = BUTTON_RELEASING;
      last_button_time = cur_time;
    }
    break;
  case BUTTON_RELEASING:
    digitalWrite(trigger_pin, HIGH);
    if (cur_time - last_button_time >= delay_before_button_and_relay)
    {
      log("BUTTON_STOP");
      button_state = BUTTON_STOP;
      last_button_time = cur_time;
    }
    break;
  default:
    break;
  }

  prev_relay_state = new_relay_state;
  prev_sensor_state = new_sensor_state;
}

RelayController controller = RelayController();

void setup()
{
  // Serial.setDebugOutput(true);
  Serial.begin(115200);
  Serial.println("___START___");

  pinMode(trigger_pin, OUTPUT);
  digitalWrite(trigger_pin, 1);

  pinMode(relay_state_pin, INPUT);
  pinMode(water_level_pin, INPUT);

  controller.prev_relay_state = digitalRead(relay_state_pin);
  controller.prev_sensor_state = digitalRead(water_level_pin);
}

void loop()
{
  controller.loop(digitalRead(relay_state_pin), digitalRead(water_level_pin));
  delay(10);
}
