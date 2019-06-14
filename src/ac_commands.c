#include "FreeRTOS.h"
#include "task.h"


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>


#include <ac_commands.h>
#include <irremote/irremote.h>
#include <irremote/irremote.c>


uint8_t temperature;

uint8_t mode;

void sendIRbyte(uint8_t sendByte, int bitMarkLength, int zeroSpaceLength, int oneSpaceLength)

{
    for (int i = 0; i < 8; i++)

    {
        if (sendByte & 0x01)

        {
            ir_mark(bitMarkLength);

            ir_space(oneSpaceLength);
        }
        else

        {
            ir_mark(bitMarkLength);

            ir_space(zeroSpaceLength);
        }

        sendByte >>= 1;
    }
}

uint8_t IRbitReverse(uint8_t x)

{
    //          01010101  |         10101010

    x = ((x >> 1) & 0x55) | ((x << 1) & 0xaa);

    //          00110011  |         11001100

    x = ((x >> 2) & 0x33) | ((x << 2) & 0xcc);

    //          00001111  |         11110000

    x = ((x >> 4) & 0x0f) | ((x << 4) & 0xf0);

    return x;
}

void pass_temp_mode_values(int target_temp, int target_state)
{
    // printf("target_temp %d\n", target_temp);

    // printf("target_state %d\n", target_state);

    temperature = target_temp;

    mode = target_state;
}

// MACRO OFF

void ac_button_off_task()
{
    uint8_t Off[] = { 0x14, 0x63, 0x00, 0x10, 0x10, 0x02 };

    taskENTER_CRITICAL();

    // Header

    ir_mark(3324);

    ir_space(1574);

    for (uint8_t i = 0; i < sizeof(Off); i++)

    {
        sendIRbyte(Off[i], 448, 390, 1182);
    }

    // End mark

    ir_mark(448);

    ir_space(8100);

    taskEXIT_CRITICAL();

    vTaskDelete(NULL);
}

// MACRO

void ac_button_temp_task()
{
                                                                                 // TEMP //MODE                      //CHECKSUM
    uint8_t FujitsuTemplate[] = { 0x14, 0x63, 0x00, 0x10, 0x10, 0xFC, 0x08, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    // printf("temperature %d\n", temperature);

    // HEAT/COOL Mode

    if (mode == 1)
    {
        FujitsuTemplate[9] = 0x04; // HEAT
    }
    else if (mode == 2)
    {
        FujitsuTemplate[9] = 0x01; // COOL
    }

    // Calculate Temp

    FujitsuTemplate[8] = (temperature - 16) << 4  | 0x01;

    // Calculate the checksum

    uint8_t checksum = 0x00;

    for (int i = 0; i < 14; i++)
    {
        checksum += FujitsuTemplate[i];
    }

    FujitsuTemplate[14] = (uint8_t)(0x9B - checksum);

    taskENTER_CRITICAL();

    // Header

    ir_mark(3324);

    ir_space(1574);

    for (uint8_t i = 0; i < sizeof(FujitsuTemplate); i++)

    {
        sendIRbyte(FujitsuTemplate[i], 448, 390, 1182);
    }

    // End mark

    ir_mark(448);

    ir_space(8100);

    taskEXIT_CRITICAL();

    vTaskDelete(NULL);
}

// TASKS

void ac_button_off()

{
    xTaskCreate(ac_button_off_task, "Send IR", 2048, NULL,

        2 | portPRIVILEGE_BIT, NULL);
}

void ac_button_temp()

{
    xTaskCreate(ac_button_temp_task, "Send IR", 2048, NULL,

        2 | portPRIVILEGE_BIT, NULL);
}
