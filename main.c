#include "rp2040.h"
#include "SPI.h"
#include "FreeRTOS.h"
#include "task.h"
#include "hardware/adc.h"
#include "lmic.h"
#include "MQTTClient.h"
#include "stdio.h"
#include "string.h"
#include "pico/stdlib.h"
#include "LoRa-RP2040.h"
#include "DHT.h"
#include "onewire.h"
#include <DS18B20.h>
#include "uart.h"

#define RA02_SPI_PORT spi0
#define RA02_SPI_SCK 3  // SCK pin
#define RA02_SPI_MOSI 4  // MOSI pin
#define RA02_SPI_MISO 5  // MISO pin
#define RA02_SPI_CSN 6  // Chip select pin
#define DHT_PIN 26
#define SOIL_MOISTURE_PIN 27
#define PH_PIN 28


void vLoRaWANTask(void *pvParameters);
void vSensorTask(void *pvParameters);

void spi_init();
void spi_send_command(uint8_t command, uint8_t* data, size_t data_len);
void spi_receive_response(uint8_t* buffer, size_t buffer_len)
void adc_init();
uint16_t read_soil_moisture(float* soil_moisture);
void prepare_soil_moisture_data(char* buffer, size_t buffer_len);
void send_soil_moisture_data();
void read_ph_data(float* ph);
void send_ph_data();
void read_npk_data(float* npk);

QueueHandle_t xSensorQueue;
QueueHandle_t xLoRaWANQueue;

void vSensorTask(void *pvParameters);
void vLoRaWANTask(void *pvParameters);

int main() {
    ADC_Init();
    dht_init();
    ds18b20_init();
    uart_init();

    os_init();

    xSensorQueue = xQueueCreate(10, sizeof(float));
    xLoRaWANQueue = xQueueCreate(10, sizeof(char) * 64);
    xTaskCreate(vSensorTask, "Sensor", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(vLoRaWANTask, "LoRaWAN", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);

    vTaskStartScheduler();

    // This will never be reached
    return 0;
}

void vSensorTask(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        float temperature, humidity;
        read_air_data(&temperature, &humidity);
        xQueueSend(xSensorQueue, &temperature, portMAX_DELAY);
        xQueueSend(xSensorQueue, &humidity, portMAX_DELAY);

        float ds18b20_temp;
        read_ds18b20_data(&ds18b20_temp);
        xQueueSend(xSensorQueue, &ds18b20_temp, portMAX_DELAY);

        float npk[3];
        read_npk_data(npk);
        xQueueSend(xSensorQueue, npk, portMAX_DELAY);

        float ph;
        read_ph_data(&ph);
        xQueueSend(xSensorQueue, &ph, portMAX_DELAY);

        float soil_moisture;
        read_soil_moisture(&soil_moisture);
        xQueueSend(xSensorQueue, &soil_moisture, portMAX_DELAY);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// LoRaWAN task
void vLoRaWANTask(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        // Wait for data to be sent
        char* buffer;
        xQueueReceive(xLoRaWANQueue, &buffer, portMAX_DELAY);

        // Send the data via LoRaWAN
        send_soil_moisture_data(buffer);
    }
}


// Initialize SPI
void spi_init() {
    // Initialize SPI bus
    spi_init(RA02_SPI_PORT, 1000 * 1000);  // 1 MHz
    gpio_set_function(RA02_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(RA02_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(RA02_SPI_MISO, GPIO_FUNC_SPI);
    gpio_set_function(RA02_SPI_CSN, GPIO_FUNC_SIO);
    gpio_set_dir(RA02_SPI_CSN, GPIO_OUT);
    gpio_put(RA02_SPI_CSN, 1);  // Set CSN high to deselect the RA02
}

// Send a command to the RA02
void spi_send_command(uint8_t command, uint8_t* data, size_t data_len) {
    gpio_put(RA02_SPI_CSN, 0);  // Set CSN low to select the RA02
    spi_write_blocking(RA02_SPI_PORT, &command, 1);  // Send the command
    spi_write_blocking(RA02_SPI_PORT, data, data_len);  // Send the data
    gpio_put(RA02_SPI_CSN, 1);  // Set CSN high to deselect the RA02
}

// Receive a response from the RA02
void spi_receive_response(uint8_t* buffer, size_t buffer_len) {
    gpio_put(RA02_SPI_CSN, 0);  // Set CSN low to select the RA02
    spi_read_blocking(RA02_SPI_PORT, 0, buffer, buffer_len);  // Read the response
    gpio_put(RA02_SPI_CSN, 1);  // Set CSN high to deselect the RA02
}

void vRA02Task(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        // Send a command to the RA02
        uint8_t command = 0x01;  // Example command
        uint8_t data[] = {0x02, 0x03, 0x04};  // Example data
        spi_send_command(command, data, sizeof(data));

        // Wait for the response
        vTaskDelay(pdMS_TO_TICKS(100));

        // Receive the response from the RA02
        uint8_t response[10];  // Example buffer
        spi_receive_response(response, sizeof(response));

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}


// Initialize the ADC
void ADC_Init() {
    adc_init(void);
    adc_gpio_init(SOIL_MOISTURE_PIN);
    adc_gpio_init(PH_PIN);
}

// Read the soil moisture data
uint16_t read_soil_moisture(float* soil_moisture) {
    *soil_moisture = ADC_Read(SOIL_MOISTURE_PIN);
}


// Prepare the soil moisture data for transmission
void prepare_data(char* buffer) {
    // Read the sensor data from the queue
    float sensor_data[7];
    xQueueReceive(xSensorQueue, &sensor_data[0], portMAX_DELAY);
    xQueueReceive(xSensorQueue, &sensor_data[1], portMAX_DELAY);
    xQueueReceive(xSensorQueue, &sensor_data[2], portMAX_DELAY);
    xQueueReceive(xSensorQueue, &sensor_data[3], portMAX_DELAY);
    xQueueReceive(xSensorQueue, &sensor_data[4], portMAX_DELAY);
    xQueueReceive(xSensorQueue, &sensor_data[5], portMAX_DELAY);
    xQueueReceive(xSensorQueue, &sensor_data[6], portMAX_DELAY);

    // Format the data as a string
    snprintf(buffer, sizeof(buffer), "T:%.1f,H:%.1f,T2:%.1f,N:%.2f,P:%.2f,K:%.2f,pH:%.2f",
             sensor_data[0], sensor_data[1], sensor_data[2], sensor_data[3], sensor_data[4], sensor_data[5], sensor_data[6]);
}


// Send the soil moisture data via LoRaWAN
void send_soil_moisture_data() {
    char buffer[128];
    prepare_data(buffer, sizeof(buffer));
    LMIC_setTxData2(1, (uint8_t*)buffer, strlen(buffer), 0);
}


void read_ph_data(float* ph) {
    *ph = adc_read(PH_PIN);
}

void send_ph_data() {
    float ph;
    read_ph_data(&ph);

    char buffer[32];
    snprintf(buffer, sizeof(buffer), "pH:%.2f", ph);

    LMIC_setTxData2(1, (uint8_t*)buffer, strlen(buffer), 0);
}


void read_npk_data(float* npk) {
    uint8_t buffer[32];
    uart_read_blocking(UART_ID, buffer, sizeof(buffer));
    sscanf((const char*)buffer, "N:%f,P:%f,K:%f", &npk_data[0], &npk_data[1], &npk_data[2])
}