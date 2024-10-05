/**

  Smart MediBox - A device remind user to take medicine on time!

  Functions:
    1. Menu options
        a. Set UTC offset to configure timezone (UTC offset range from -12 to 14 hours)
        b. Set alarms (3) to remind user for medicine time
        c. Disable all the alarms
    2. Display Time from NTP server over WI-FI based on configured UTC offset.
    3. Buzzer & Red LED used to alert the user to remind about medicine time based configured alarms
    4. Cancel button used stop alarms when ringing.
    5. Check temperature continuously and warn the user when exceed the recommended healthy limit.
        Yellow LED to warn user when exceeded below limits.
        a. Temperature between 26 C to 32 C
        b. Humidity between 60% to 80%
    6. OLED Display to print alert or warning messages and current time based on UTC offset.
  
  ========================== ADVANCED ===========================
  Smart MediBox (Advanced Features)

  Functions:
    1. LDR to monitor light inensity & send it to Node-Red Dashboard
    2. Servo to control the shaded sliding window to prevent excessive light & 
       take control input from nod-red dashboard

*/

// Included libraries
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHTesp.h>
#include <WiFi.h>

#include <ESP32Servo.h>
#include <PubSubClient.h>

// OLED Parameters
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// Pin Configurations for Components
#define BUZZER 5
#define LED_1 15 // Used to Alert the user for medicine time
#define LED_2 2 // Used to warn user for about temperature & humidity
#define CANCEL 25 // Push button for cancel the menu selection and stop alarm when ringing
#define OK 32 // Push button to open menu selection & confirm the selections
#define UP 35
#define DOWN 33
#define DHTPIN 12

// Time parameters
#define NTP_SERVER     "pool.ntp.org"
#define UTC_OFFSET     0
#define UTC_OFFSET_DST 0

// Delay time after print message in display
#define MESSAGE_DELAY 1000
// Delay time for push buttons debounce & other common wait
#define DELAY 200

// Object parameters for display & sensor
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
DHTesp dhtSensor;

// Time parameters to keep hours & minutes to check alarms
int hours = 0;
int minutes = 0;

// UTC Offset parameter in seconds
long utc_offset_sec = 0;

// Alarm parameters
struct alarm {
  int hours;
  int minutes;
  bool triggered;
};

bool alarm_enabled = false;
int n_alarms = 3;

struct alarm alarms[] = {
  {0, 0, true},
  {0, 0, true},
  {0, 0, true}
};

// Buzzer tone parameters
int n_notes = 8;
int C = 262;
int D = 294;
int E = 330;
int F = 348;
int G = 392;
int A = 440;
int B = 494;
int C_H = 523;
int notes[] = {C, D, E, F, G, A, B, C_H};

// Menu parameters
int current_mode = 0;
int max_modes = 5;
String modes[] = {
  "1 - Set Timezone",
  "2 - Set Alarm 1",
  "3 - Set Alarm 2",
  "4 - Set Alarm 3",
  "5 - Disable Alarms"
};

/** ========================== ADVANCED =========================== **/

// Pin Configurations for LDR & Servo
#define LIGHT_SENSOR_PIN 34
#define SERVO_PIN 19

// MQTT Client object parameters
WiFiClient espClient;
PubSubClient mqttClient(espClient);
Servo servo;

const char* MQTT_SERVER = "test.mosquitto.org";
// Producer topics
const char* LIGHT_INTENSITY_TOPIC = "LIGHT_INTENSITY";
const char* SERVO_MOTOR_ANGLE_TOPIC = "SERVO_MOTOR_ANGLE";
const char* TEMPERATURE_TOPIC = "MEDIBOX_TEMPERATURE";
const char* HUMIDITY_TOPIC = "MEDIBOX_HUMIDITY";
// Subscribe topics 
const char* SERVO_MIN_ANGLE_TOPIC = "SERVO_MIN_ANGLE";
const char* SERVO_CONTROL_FACTOR_TOPIC = "SERVO_CONTROLLING_FACTOR";

// Servo motor angle parameters
int offset_angle = 30;
float intensity = 0;
float control_factor = 0.75;

// Array to store values which used to send data.
char intensity_array[6];
char servo_angle_array[6];

void setup() {
  Serial.begin(9600);

  // Pin configurations
  pinMode(BUZZER, OUTPUT);
  pinMode(LED_1, OUTPUT);
  pinMode(LED_2, OUTPUT);
  pinMode(CANCEL, INPUT);
  pinMode(OK, INPUT);
  pinMode(UP, INPUT);
  pinMode(DOWN, INPUT);

  // Initialize Temp & Humidity sensor
  dhtSensor.setup(DHTPIN, DHTesp::DHT22);

  // Initialize the OLED Display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("SSD1306 allocation failed");
    for (;;);
  }

  display.display();
  delay(MESSAGE_DELAY);
  print_line("Welcome to Medibox!", 10, 20, 2, true);
  delay(MESSAGE_DELAY);

  // Initialize the WIFI
  WiFi.begin("Wokwi-GUEST", "", 6);
  while (WiFi.status() != WL_CONNECTED) {
    delay(DELAY);
    print_line("Connecting to WIFI", 0, 0, 2, true);
  }

  print_line("Connected to WIFI", 0, 0, 2, true);

  // Configure Initial Time using WIFI network
  configTime(UTC_OFFSET, UTC_OFFSET_DST, NTP_SERVER);
  
  /** ========================== ADVANCED =========================== **/
  // MQTT Setup function
  setup_mqtt();

  servo.attach(SERVO_PIN, 500, 2400);
}

void loop() {
  // Check for menu button pressed
  if (digitalRead(OK) == LOW) {
    delay(DELAY);
    go_to_menu();
  }

  /** ========================== ADVANCED =========================== **/
  if (!mqttClient.connected()) {
    connect_to_broker();
  }

  mqttClient.loop();
  read_intensity();
  delay(MESSAGE_DELAY);

  update_time();
  check_alarm();
  check_temperature_humidity();
}

/**
   Function update time and print in OLED display (HH:MM:SS)
*/
void update_time() {
  struct tm timeinfo;
  getLocalTime(&timeinfo);

  char time_hour[3];
  char time_minute[3];
  char full_time[9];

  strftime(time_hour, 3, "%H", &timeinfo);
  strftime(time_minute, 3, "%M", &timeinfo);
  strftime(full_time, 9, "%T", &timeinfo);

  print_line(String(full_time), 15, 0, 2, true);

  hours = atoi(time_hour);
  minutes = atoi(time_minute);
}

/**
   Function to check alarms and ring if any.
*/
void check_alarm() {
  if (!alarm_enabled) {
    return;
  }

  for (int i = 0; i < n_alarms; i++) {
    if (!alarms[i].triggered && alarms[i].hours == hours && alarms[i].minutes == minutes) {
      ring_alarm();
      alarms[i].triggered = true;
    }
  }
}

/**
   Function to ring alarm to notify user
*/
void ring_alarm() {
  print_line("Medicine Time!", 0, 0, 2, true);

  digitalWrite(LED_1, HIGH);

  bool canceled = false;

  //Buzzer ring alarm until user press cancel button
  while (!canceled && digitalRead(CANCEL) == HIGH) {
    for (int i = 0; i < n_notes; i++) {
      if (digitalRead(CANCEL) == LOW) {
        delay(DELAY);
        canceled = true;
        break;
      }
      tone(BUZZER, notes[i]);
      delay(DELAY);
      noTone(BUZZER);
      delay(2);
    }
  }

  digitalWrite(LED_1, LOW);
  display.clearDisplay();
}

/**
   Function to check menu and run based on user selection
*/
void go_to_menu() {
  while (digitalRead(CANCEL) == HIGH) {
    print_line(modes[current_mode], 0, 0, 2, true);

    int pressed = wait_for_button_press();

    if (pressed == UP) {
      delay(DELAY);
      current_mode += 1;
      current_mode = current_mode % max_modes;
    }

    else if (pressed == DOWN) {
      delay(DELAY);
      current_mode -= 1;
      if (current_mode < 0) {
        current_mode = max_modes - 1;
      }
    }

    else if (pressed == OK) {
      delay(DELAY);
      run_mode(current_mode);
    }

    else if (pressed == CANCEL) {
      delay(DELAY);
      break;
    }
  }
}

/**
   Function to run selected menu
*/
void run_mode(int mode) {
  if (mode == 0) {
    set_timezone_and_config();
  }

  else if (mode == 1 || mode == 2 || mode == 3) {
    set_alarm(mode - 1);
  }

  else if (mode == 4) {
    disable_alarms();
  }
}

/**
   Function set timezone and config based on user input
*/
void set_timezone_and_config() {
  int current_offset_hours = utc_offset_sec / 3600;
  int current_offset_minutes = (utc_offset_sec % 3600) / 60;

  int offset_hours = read_value("Enter offset hours: ", current_offset_hours, 14, -12);
  int offset_minutes = read_value("Enter offset minutes: ", current_offset_minutes, 59, 0);

  utc_offset_sec = offset_hours * 3600 + offset_minutes * 60;
  configTime(utc_offset_sec, UTC_OFFSET_DST, NTP_SERVER);
  print_line("Timezone set to " + String(offset_hours) + ":" + String(offset_minutes), 0, 0, 2, true);
  delay(MESSAGE_DELAY);
}

/**
   Function to set alarm based on user input
   alarm - Alarm index
*/
void set_alarm(int alarm) {
  alarms[alarm].hours = read_value("Enter hour: ", alarms[alarm].hours, 23, 0);
  alarms[alarm].minutes = read_value("Enter minute: ", alarms[alarm].minutes, 59, 0);
  alarms[alarm].triggered = false;

  if (!alarm_enabled) {
    alarm_enabled = true;
  }

  print_line("Alarm " + String(alarm) + " is set to " + String(alarms[alarm].hours) + ":" + String(alarms[alarm].minutes), 0, 0, 2, true);
  delay(MESSAGE_DELAY);
}

/**
  Function to disable all the alarms and set to default values
*/
void disable_alarms() {
  alarm_enabled = false;
  // Clear all the alarms
  for (int i = 0; i < n_alarms; i++) {
    alarms[i] = {0, 0, true};
  }
  print_line("Alarms are disabled", 0, 0, 2, true);
  delay(MESSAGE_DELAY);
}

/**
   Function to check temperature and humidity and warn if exceeded recommended limit
*/
void check_temperature_humidity() {
  TempAndHumidity data = dhtSensor.getTempAndHumidity();

  bool warning = false;
  if (data.temperature > 32) {
    warning = true;
    print_line("Temperature HIGH", 0, 40, 1, false);
  }

  else if (data.temperature < 26) {
    warning = true;
    print_line("Temperature LOW", 0, 40, 1, false);
  }

  if (data.humidity > 80) {
    warning = true;
    print_line("Humidity HIGH", 0, 50, 1, false);
  }

  else if (data.humidity < 60) {
    warning = true;
    print_line("Humidity LOW", 0, 50, 1, false);
  }

  if (warning) {
    digitalWrite(LED_2, HIGH);
    delay(DELAY);
    digitalWrite(LED_2, LOW);
  }

  /** ========================== ADVANCED =========================== **/

  char temperature_array[6];
  String(data.temperature, 2).toCharArray(temperature_array, 6);
  mqttClient.publish(TEMPERATURE_TOPIC, temperature_array);

  char humidity_array[6];
  String(data.humidity).toCharArray(humidity_array, 6);
  mqttClient.publish(HUMIDITY_TOPIC, humidity_array);
}

/**Common Functions**/

/**
   Function to print message in OLED Display
   text - Text Message
   column - Column position in display
   row - Row position in display
   text_size - Text size
   clear - Clear the display before print the text
*/
void print_line(String text, int column, int row, int text_size, bool clear) {
  if (clear) {
    display.clearDisplay();
  }
  display.setTextSize(text_size);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(column, row);
  display.println(text);

  display.display();
}

/**
   Function to read user input value using push buttons.
   return int value based on user input
   text - Text message for user
   init_value - Initial Value
   max_value - Maximum allowed value
   min_value - Minimum allowed value
*/
int read_value(String text, int init_value, int max_value, int min_value) {
  int temp_value = init_value;
  while (true) {
    print_line(text + String(temp_value), 0, 0, 2, true);

    int pressed = wait_for_button_press();

    if (pressed == UP) {
      delay(DELAY);
      temp_value += 1;
      if (temp_value > max_value) {
        temp_value = min_value;
      }
    }

    else if (pressed == DOWN) {
      delay(DELAY);
      temp_value -= 1;
      if (temp_value < min_value) {
        temp_value = max_value;
      }
    }

    else if (pressed == OK) {
      delay(DELAY);
      return temp_value;
    }

    else if (pressed == CANCEL) {
      delay(DELAY);
      break;
    }
  }

  return init_value;
}

/**
   Function to wait for user to press push button.
   return Pin no for each button when button pressed by user.
*/
int wait_for_button_press() {
  while (true) {
    if (digitalRead(UP) == LOW) {
      delay(DELAY);
      return UP;
    }

    else if (digitalRead(DOWN) == LOW) {
      delay(DELAY);
      return DOWN;
    }

    else if (digitalRead(OK) == LOW) {
      delay(DELAY);
      return OK;
    }

    else if (digitalRead(CANCEL) == LOW) {
      delay(DELAY);
      return CANCEL;
    }
  }
}

/** ========================== ADVANCED =========================== **/

/**
   Function to setup mqtt server & receiveCallback
*/
void setup_mqtt() {
  mqttClient.setServer(MQTT_SERVER, 1883);
  mqttClient.setCallback(receive_callback);
}

/**
   Function to connect to mqtt broker
*/
void connect_to_broker() {
  while (!mqttClient.connected()) {
    Serial.print("Attempting MQTT Connection....");

    if (mqttClient.connect("EPS32-41234123")) {
      Serial.println("Connected");
      mqttClient.subscribe(SERVO_MIN_ANGLE_TOPIC);
      mqttClient.subscribe(SERVO_CONTROL_FACTOR_TOPIC);
    } else {
      Serial.println("Failed");
      Serial.print(mqttClient.state());
      delay(5000);
    }
  }
}

/**
   Function to read light intensity from LDR sensor & publish values
*/
void read_intensity() {
  int analog_value = analogRead(LIGHT_SENSOR_PIN);

  Serial.println("Analog Value = " + String(analog_value));

  float prev_value = intensity;

  intensity = map(analog_value, 0, 4095, 100, 0) / 100.00;

  String(intensity, 2).toCharArray(intensity_array, 6);

  mqttClient.publish(LIGHT_INTENSITY_TOPIC, intensity_array);

  Serial.println("Light Intensity = " + String(intensity));

  //if intensity value changed calculate & update servo motor angle.
  if (prev_value != intensity) {
    calculate_servo_position();
  }
}

/**
   Callback function to listening to incoming mqtt messages.
*/
void receive_callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("Message received [" + String(topic) + "]");

  char payload_array[length];
  for (int i = 0; i < length; i++) {
    payload_array[i] = (char) payload[i];
  }

  if (strcmp(topic, SERVO_MIN_ANGLE_TOPIC) == 0) {
    offset_angle = atof(payload_array);
    Serial.println("Offset Angle = " + String(offset_angle));
  } else if (strcmp(topic, SERVO_CONTROL_FACTOR_TOPIC) == 0) {
    control_factor = atof(payload_array);
    Serial.println("Controlling Factor = " + String(control_factor));
  }

  calculate_servo_position();
}

/**
   Function to calculate servo motor angle to adjust sliding window
*/
void calculate_servo_position() {
  int servo_angle = offset_angle + (180 - offset_angle) * intensity * control_factor;

  Serial.println("Servo Motor Angle = " + String(servo_angle));

  servo.write(servo_angle);

  String(servo_angle).toCharArray(servo_angle_array, 6);

  mqttClient.publish(SERVO_MOTOR_ANGLE_TOPIC, servo_angle_array);
}