#include <esp/uart.h>
#include <sysparam.h>
#include <esplibs/libmain.h>
#include "espressif/esp_common.h"
#include <homekit/homekit.h>
#include <homekit/characteristics.h>


#include "../common/custom_characteristics.h"
#include <wifi_config.h>


#include <led_codes.h>
#include <adv_button.h>


#include <dht/dht.h>
#include <ds18b20/ds18b20.h>
#include <unistd.h>
#include <string.h>
#include <FreeRTOS.h>
#include "task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include <rboot-api.h>


#include <irremote/irremote.h>
#include <ac_commands.h>


const int led_gpio = 2; //BUILT-IN LED on pin D4
const int SENSOR_PIN = 4; //DHT sensor on pin D2
const int GPIO_IR_PIN = 14; //IR sensor on pin D5

#define TEMP_OFFSET "1"

volatile float old_humidity_value = 0.0, old_temperature_value = 0.0, switch_temp_update = 0;

ETSTimer device_restart_timer, change_settings_timer, save_states_timer, extra_func_timer;

volatile bool paired = false;

volatile bool Wifi_Connected = false;

homekit_value_t read_ip_addr();

void update_state();

void update_temp();

void change_settings_callback();

void reboot_callback();

void ota_firmware_callback();

void on_update(homekit_characteristic_t * ch, homekit_value_t value, void * context) {

  update_state();

}

void on_temp_update(homekit_characteristic_t * ch, homekit_value_t value, void * context) {

  update_temp();

}

//GLOBAL CHARACTERISTICS

homekit_characteristic_t manufacturer = HOMEKIT_CHARACTERISTIC_(MANUFACTURER, "Curla92");

homekit_characteristic_t name = HOMEKIT_CHARACTERISTIC_(NAME, "Air Conditioner");

homekit_characteristic_t model = HOMEKIT_CHARACTERISTIC_(MODEL, "AirConditioner");

homekit_characteristic_t serial = HOMEKIT_CHARACTERISTIC_(SERIAL_NUMBER, NULL);

homekit_characteristic_t revision = HOMEKIT_CHARACTERISTIC_(FIRMWARE_REVISION, "0.0.6");

//GENERAL AND CUSTOM

homekit_characteristic_t temp_offset = HOMEKIT_CHARACTERISTIC_(CUSTOM_TEMPERATURE_OFFSET, 0.0, .id = 108, .callback = HOMEKIT_CHARACTERISTIC_CALLBACK(change_settings_callback));

homekit_characteristic_t ip_addr = HOMEKIT_CHARACTERISTIC_(CUSTOM_IP_ADDR, "", .id = 109, .getter = read_ip_addr);

homekit_characteristic_t reboot_device = HOMEKIT_CHARACTERISTIC_(CUSTOM_REBOOT_DEVICE, false, .id = 110, .callback = HOMEKIT_CHARACTERISTIC_CALLBACK(reboot_callback));

homekit_characteristic_t ota_firmware = HOMEKIT_CHARACTERISTIC_(CUSTOM_OTA_UPDATE, false, .id = 111, .callback = HOMEKIT_CHARACTERISTIC_CALLBACK(ota_firmware_callback));

//AC PARAMETERS

homekit_characteristic_t target_temperature = HOMEKIT_CHARACTERISTIC_(

  TARGET_TEMPERATURE, 22,

  .min_value = (float[]) {

    18

  },

  .max_value = (float[]) {

    30

  },

  .min_step = (float[]) {

    1

  },

  .callback = HOMEKIT_CHARACTERISTIC_CALLBACK(on_temp_update)

);

homekit_characteristic_t current_temperature = HOMEKIT_CHARACTERISTIC_(CURRENT_TEMPERATURE, 0);

homekit_characteristic_t current_humidity = HOMEKIT_CHARACTERISTIC_(CURRENT_RELATIVE_HUMIDITY, 0);

homekit_characteristic_t current_heating_cooling_state = HOMEKIT_CHARACTERISTIC_(CURRENT_HEATING_COOLING_STATE, 0);

homekit_characteristic_t target_heating_cooling_state = HOMEKIT_CHARACTERISTIC_(TARGET_HEATING_COOLING_STATE, 0, .callback = HOMEKIT_CHARACTERISTIC_CALLBACK(on_update), .min_value = (float[]) {

  0

}, .max_value = (float[]) {

  2

}, .min_step = (float[]) {

  1

});

homekit_characteristic_t units = HOMEKIT_CHARACTERISTIC_(TEMPERATURE_DISPLAY_UNITS, 0);

//homekit_characteristic_t cooling_threshold = HOMEKIT_CHARACTERISTIC_(COOLING_THRESHOLD_TEMPERATURE, 22, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_temp_update),.min_value = (float[]) {18},.max_value = (float[]) {30},.min_step = (float[]) {1} );

//homekit_characteristic_t heating_threshold = HOMEKIT_CHARACTERISTIC_(HEATING_THRESHOLD_TEMPERATURE, 28, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_temp_update),.min_value = (float[]) {16},.max_value = (float[]) {30},.min_step = (float[]) {1});

//homekit_characteristic_t rotation_speed = HOMEKIT_CHARACTERISTIC_(ROTATION_SPEED, 0, .callback=HOMEKIT_CHARACTERISTIC_CALLBACK(on_update));

void update_state() {

  uint8_t state = target_heating_cooling_state.value.int_value;

  if (state == 1 && current_temperature.value.int_value > target_temperature.value.int_value) {

    current_heating_cooling_state.value = HOMEKIT_UINT8(0);

    //printf("Target State1: %d\n",state );

  } else if (state == 2 && current_temperature.value.int_value < target_temperature.value.int_value) {

    current_heating_cooling_state.value = HOMEKIT_UINT8(0);

    //printf("Target State2: %d\n",state );

  } else {

    current_heating_cooling_state.value = HOMEKIT_UINT8(state);

    //printf("Target State3: %d\n",state );

  }

  vTaskDelay(200 / portTICK_PERIOD_MS);

  homekit_characteristic_notify( & current_heating_cooling_state, current_heating_cooling_state.value);

  //printf("switch_temp_update: %f\n", switch_temp_update );

  //OFF CLIMA

  if (state == 0) {

    //printf("OFF\n" );

    ac_button_off();

    led_code(led_gpio, FUNCTION_C);

  } else if (switch_temp_update == 0)

  {

    //printf("AC is now ON, updating temp\n" );

    update_temp();

  }

  switch_temp_update = 0;

}

void update_temp() {

  //printf("Running update_temp() \n" );

  uint8_t target_state = target_heating_cooling_state.value.int_value;

  float target_temp = 0;

  if (target_state == 1) {

    //Read the Heat target

    target_temp = target_temperature.value.float_value;

  } else if (target_state == 2) {

    //Else read the Cool target

    target_temp = target_temperature.value.float_value;

  }

  //printf("Target State: %d\n",target_state );

  //printf("Target temp: %f\n",target_temp );

  target_temp = (int) target_temp;

  pass_temp_mode_values(target_temp, target_state);

  //HEAT TARGET STATE 1

  if (target_state == 1) {

    switch ((int) target_temp) {

      //case 16:

      //printf("New Heating Temp: %d\n", 16 );

      //ac_button_heat_16();

      //break;

      //case 17:

      //printf("New Heating Temp: %d\n", 17 );

      //ac_button_heat_17();

      //break;

    case 18:

      //printf("New Heating Temp: %d\n", 18 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 19:

      //printf("New Heating Temp: %d\n", 19 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 20:

      //printf("New Heating Temp: %d\n", 20 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 21:

      //printf("New Heating Temp: %d\n", 21 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 22:

      //printf("New Heating Temp: %d\n", 22 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 23:

      //printf("New Heating Temp: %d\n", 23 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 24:

      //printf("New Heating Temp: %d\n", 24 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 25:

      //printf("New Heating Temp: %d\n", 25 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 26:

      //printf("New Heating Temp: %d\n", 26 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 27:

      //printf("New Heating Temp: %d\n", 27 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 28:

      //printf("New Heating Temp: %d\n", 28 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 29:

      //printf("New Heating Temp: %d\n", 29 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 30:

      //printf("New Heating Temp: %d\n", 30 );

      ac_button_temp();

      led_code(led_gpio, FUNCTION_C);

      switch_temp_update = 1;

      update_state();

      break;

    default:

      printf("No action \n");

    }

  }

  //COOL TARGET STATE 2

  if (target_state == 2) {

    switch ((int) target_temp)

    {

    case 18:

      //printf("New Cooling Temp: %d\n", 18 );

      ac_button_temp();

      led_code(led_gpio, FUNCTION_C);

      switch_temp_update = 1;

      update_state();

      break;

    case 19:

      //printf("New Cooling Temp: %d\n", 19 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 20:

      //printf("New Cooling Temp: %d\n", 20 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 21:

      //printf("New Cooling Temp: %d\n", 21 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 22:

      //printf("New Cooling Temp: %d\n", 22 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 23:

      //printf("New Cooling Temp: %d\n", 23 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 24:

      //printf("New Cooling Temp: %d\n", 24 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 25:

      //printf("New Cooling Temp: %d\n", 25 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 26:

      //printf("New Cooling Temp: %d\n", 26 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 27:

      //printf("New Cooling Temp: %d\n", 27 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 28:

      //printf("New Cooling Temp: %d\n", 28 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 29:

      //printf("New Cooling Temp: %d\n", 29 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    case 30:

      //printf("New Cooling Temp: %d\n", 30 );

      ac_button_temp();

      switch_temp_update = 1;

      update_state();

      break;

    default:

      printf("No action \n");

    }

  }

}

//LED ON/OFF

void led_write(bool on) {

  gpio_write(led_gpio, on ? 0 : 1);

}

void identify_task(void * _args) {

  for (int i = 0; i < 3; i++) {

    for (int j = 0; j < 2; j++) {

      led_write(true);

      vTaskDelay(100 / portTICK_PERIOD_MS);

      led_write(false);

      vTaskDelay(100 / portTICK_PERIOD_MS);

    }

    vTaskDelay(250 / portTICK_PERIOD_MS);

  }

  led_write(false);

  vTaskDelete(NULL);

}

void identify(homekit_value_t _value) {

  printf("identify\n");

  xTaskCreate(identify_task, "identify", 128, NULL, 2, NULL);

  vTaskDelay(100 / portTICK_PERIOD_MS);

}

//CHANGE SETTINGS

void change_settings_callback() {

  sdk_os_timer_arm( & change_settings_timer, 3500, 0);

}

//SAVE SETTINGS

void save_settings() {

  sysparam_set_int32(TEMP_OFFSET, temp_offset.value.float_value * 100);

}

//SETTINGS INIT

void settings_init() {

  sysparam_set_int32(TEMP_OFFSET, 0);

}

//IP Address

homekit_value_t read_ip_addr() {

  struct ip_info info;

  if (sdk_wifi_get_ip_info(STATION_IF, & info)) {

    char * buffer = malloc(16);

    snprintf(buffer, 16, IPSTR, IP2STR( & info.ip));

    return HOMEKIT_STRING(buffer);

  }

  return HOMEKIT_STRING("");

}

void reset_configuration_task() {

  led_code(led_gpio, WIFI_CONFIG_RESET);

  printf("Resetting Wifi Config\n");

  wifi_config_reset();

  vTaskDelay(1000 / portTICK_PERIOD_MS);

  printf("Resetting HomeKit Config\n");

  homekit_server_reset();

  vTaskDelay(1000 / portTICK_PERIOD_MS);

  printf("Restarting\n");

  sdk_system_restart();

  vTaskDelete(NULL);

}

//RESTART

void device_restart_task() {

  vTaskDelay(5500 / portTICK_PERIOD_MS);

  if (ota_firmware.value.bool_value) {

    rboot_set_temp_rom(1);

    vTaskDelay(150 / portTICK_PERIOD_MS);

  }

  sdk_system_restart();

  vTaskDelete(NULL);

}

void device_restart() {

  printf("RC > Restarting device\n");

  led_code(led_gpio, FUNCTION_C);

  xTaskCreate(device_restart_task, "device_restart_task", configMINIMAL_STACK_SIZE, NULL, 1, NULL);

}

//RESET

void reset_configuration() {

  printf("Resetting configuration\n");

  xTaskCreate(reset_configuration_task, "Reset configuration", 256, NULL, 2, NULL);

  vTaskDelay(100 / portTICK_PERIOD_MS);

}

//REBOOT

void reboot_callback() {

  if (reboot_device.value.bool_value) {

    sdk_os_timer_setfn( & device_restart_timer, device_restart, NULL);

    sdk_os_timer_arm( & device_restart_timer, 5000, 0);

  } else {

    sdk_os_timer_disarm( & device_restart_timer);

  }

}

//SETUP

//OTA

void ota_firmware_callback() {

  if (ota_firmware.value.bool_value) {

    sdk_os_timer_setfn( & device_restart_timer, device_restart, NULL);

    sdk_os_timer_arm( & device_restart_timer, 5000, 0);

  } else {

    sdk_os_timer_disarm( & device_restart_timer);

  }

}

void sensor_worker() {

  //Temperature measurement

  float humidity_value, temperature_value;

  bool get_temp = false;

  get_temp = dht_read_float_data(DHT_TYPE_DHT22, SENSOR_PIN, & humidity_value, & temperature_value);

  if (get_temp) {

    temperature_value += temp_offset.value.float_value;

    // printf("RC >>> Sensor: temperature %g, humidity %g\n", temperature_value, humidity_value);

    if (temperature_value != old_temperature_value) {

      old_temperature_value = temperature_value;

      current_temperature.value = HOMEKIT_FLOAT(temperature_value); //Update AC current temp

      homekit_characteristic_notify( & current_temperature, HOMEKIT_FLOAT(temperature_value));

    }

    if (humidity_value != old_humidity_value) {

      old_humidity_value = humidity_value;

      current_humidity.value = HOMEKIT_FLOAT(humidity_value);

      homekit_characteristic_notify( & current_humidity, current_humidity.value);

    }

  } else

  {

    //led_code(LED_GPIO, SENSOR_ERROR);

    printf("Couldnt read data from sensor\n");

  }

  //uint32_t freeheap = xPortGetFreeHeapSize();

  //printf("xPortGetFreeHeapSize = %d bytes\n", freeheap);

}

homekit_accessory_t * accessories[] = {

  HOMEKIT_ACCESSORY(.id = 1, .category = homekit_accessory_category_thermostat, .services = (homekit_service_t * []) {

    HOMEKIT_SERVICE(ACCESSORY_INFORMATION, .characteristics = (homekit_characteristic_t * []) {

        &name,

        &manufacturer,

        &serial,

        &model,

        &revision,

        HOMEKIT_CHARACTERISTIC(IDENTIFY, identify),

          NULL

      }),

      HOMEKIT_SERVICE(THERMOSTAT, .primary = true, .characteristics = (homekit_characteristic_t * []) {

        HOMEKIT_CHARACTERISTIC(NAME, "Clima"),

          &current_temperature,

          &target_temperature,

          &current_humidity,

          &current_heating_cooling_state,

          &target_heating_cooling_state,

          //&cooling_threshold,

          //&heating_threshold,

          &units,

          //&rotation_speed,

          &temp_offset,

          &ip_addr,

          &reboot_device,

          &ota_firmware,

          NULL

      }),

      NULL

  }),

  NULL

};

void on_event(homekit_event_t event) {

  if (event == HOMEKIT_EVENT_SERVER_INITIALIZED) {

    //led_status_set(led_status, paired ? &normal_mode : &unpaired);

    printf("SERVER JUST INITIALIZED\n");

    if (homekit_is_paired()) {

      printf("Found pairing, starting timers\n");

    }

  } else if (event == HOMEKIT_EVENT_CLIENT_CONNECTED) {

    if (!paired)

      // led_status_set(led_status, &pairing);

      printf("CLIENT JUST CONNECTED\n");

  } else if (event == HOMEKIT_EVENT_CLIENT_DISCONNECTED) {

    if (!paired)

      //led_status_set(led_status, &unpaired);

      printf("CLIENT JUST DISCONNECTED\n");

  } else if (event == HOMEKIT_EVENT_PAIRING_ADDED || event == HOMEKIT_EVENT_PAIRING_REMOVED) {

    paired = homekit_is_paired();

    // led_status_set(led_status, paired ? &normal_mode : &unpaired);

    printf("CLIENT JUST PAIRED\n");

  }

}

void create_accessory() {

  // Accessory Name

  uint8_t macaddr[6];

  sdk_wifi_get_macaddr(STATION_IF, macaddr);

  int name_len = snprintf(NULL, 0, "Fujitsu AC-%02X%02X%02X",

    macaddr[3], macaddr[4], macaddr[5]);

  char * name_value = malloc(name_len + 1);

  snprintf(name_value, name_len + 1, "Fujitsu AC-%02X%02X%02X",

    macaddr[3], macaddr[4], macaddr[5]);

  name.value = HOMEKIT_STRING(name_value);

  // Accessory Serial

  char * serial_value = malloc(13);

  snprintf(serial_value, 13, "%02X%02X%02X%02X%02X%02X", macaddr[0], macaddr[1], macaddr[2], macaddr[3], macaddr[4], macaddr[5]);

  serial.value = HOMEKIT_STRING(serial_value);

}

homekit_server_config_t config = {

  .accessories = accessories,

  .password = "111-11-111",

  .on_event = on_event,

};

void on_wifi_event(wifi_config_event_t event) {

  if (event == WIFI_CONFIG_CONNECTED) {

    printf("CONNECTED TO >>> WIFI <<<\n");

    Wifi_Connected = true;

    homekit_server_init( & config);

    led_code(led_gpio, WIFI_CONNECTED);

  } else if (event == WIFI_CONFIG_DISCONNECTED) {

    Wifi_Connected = false;

    printf("DISCONNECTED FROM >>> WIFI <<<\n");

  }

}

void hardware_init() {

  //LED INIT

  gpio_enable(led_gpio, GPIO_OUTPUT);

  led_write(false);

  // DTH GPIO INIT

  gpio_set_pullup(SENSOR_PIN, false, false);

  // IR Common INIT

  gpio_enable(GPIO_IR_PIN, GPIO_OUTPUT);

  gpio_write(GPIO_IR_PIN, 0);

  ir_set_pin(GPIO_IR_PIN);
  ir_set_frequency(38);

}

void user_init(void) {

  uart_set_baud(0, 115200);

  printf("SDK version:%s\n", sdk_system_get_sdk_version());

  hardware_init();

  settings_init();

  sdk_os_timer_setfn( & extra_func_timer, sensor_worker, NULL);

  sdk_os_timer_arm( & extra_func_timer, 10000, 1);

  sdk_os_timer_setfn( & change_settings_timer, save_settings, NULL);

  create_accessory();

  //wifi_config_init("Fujitsu AC", NULL, on_wifi_ready);

  wifi_config_init2("Fujitsu AC", NULL, on_wifi_event);

}
