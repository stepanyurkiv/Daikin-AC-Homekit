PROGRAM = main

EXTRA_COMPONENTS = \
    	extras/onewire \
    	extras/ds18b20 \
	extras/dht \
	extras/http-parser \
	extras/dhcpserver \
	extras/rboot-ota \
	$(abspath ../lib/adv_button) \
        $(abspath ../lib/led_codes)\
	$(abspath ../components/wifi_config) \
	$(abspath ../components/wolfssl) \
	$(abspath ../components/cJSON) \
	$(abspath ../components/homekit) \
	$(abspath ../components/irremote) \

FLASH_SIZE = 8
FLASH_MODE = dout
FLASH_SPEED = 40

HOMEKIT_SPI_FLASH_BASE_ADDR = 0x8c000
HOMEKIT_MAX_CLIENTS = 16
HOMEKIT_SMALL = 0

EXTRA_CFLAGS += -I../.. -DHOMEKIT_SHORT_APPLE_UUIDS
#EXTRA_CFLAGS += -DHOMEKIT_OVERCLOCK_PAIR_VERIFY
#EXTRA_CFLAGS += -DHOMEKIT_OVERCLOCK_PAIR_SETUP

## DEBUG
#EXTRA_CFLAGS += -DHOMEKIT_DEBUG=1


include $(SDK_PATH)/common.mk

monitor:
	$(FILTEROUTPUT) --port $(ESPPORT) --baud 115200 --elf $(PROGRAM_OUT)
