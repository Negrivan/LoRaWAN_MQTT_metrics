#include "rp2040.h"
#include "hardware/spi.h"
#include "FreeRTOS.h"
#include "task.h"
#include "hardware/adc.h"
#include "lmic.h"
#include "MQTTClient.h"
#include "stdio.h"

#define RA02_SPI_PORT spi0
#define RA02_SPI_SCK 3  // SCK pin
#define RA02_SPI_MOSI 4  // MOSI pin
#define RA02_SPI_MISO 5  // MISO pin
#define RA02_SPI_CSN 6  // Chip select pin


void vSoilMoistureTask(void *pvParameters);
void vLoRaWANTask(void *pvParameters);
void spi_init();
void spi_send_command(uint8_t command, uint8_t* data, size_t data_len);
void spi_receive_response(uint8_t* buffer, size_t buffer_len)
void adc_init();
uint16_t read_soil_moisture();
void prepare_soil_moisture_data(char* buffer, size_t buffer_len);
void send_soil_moisture_data();

QueueHandle_t xSensorQueue;
QueueHandle_t xLoRaWANQueue;

void vSensorTask(void *pvParameters);
void vLoRaWANTask(void *pvParameters);

int main() {
    // Initialize the ADC
    adc_init();

    // Initialize the LoRaWAN stack
    os_init();

    // Create the tasks
    xTaskCreate(vSoilMoistureTask, "SoilMoisture", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);
    xTaskCreate(vLoRaWANTask, "LoRaWAN", configMINIMAL_STACK_SIZE, NULL, tskIDLE_PRIORITY + 1, NULL);

    // Start the scheduler
    vTaskStartScheduler();

    // This will never be reached
    return 0;
}


void mqtt_init() {
    // Create the MQTT client
    MQTTClient_create(&client, broker, port, NULL);

    // Set the MQTT client options
    MQTTClient_connectOptions opts = MQTTClient_connectOptions_initializer;
    opts.keepAliveInterval = 60;
    opts.cleansession = 1;

    // Connect to the MQTT broker
    int rc = MQTTClient_connect(client, &opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        // Handle the error
        // ...
    }
}

void vSoilMoistureTask(void *pvParameters) {
    (void) pvParameters;

    for (;;) {
        // Read the soil moisture data
        uint16_t soil_moisture = read_soil_moisture();

        // Prepare the soil moisture data for transmission
        char buffer[32];
        prepare_soil_moisture_data(buffer, sizeof(buffer));

        // Send the soil moisture data via the LoRaWAN task
        xQueueSend(xLoRaWANQueue, &buffer, portMAX_DELAY);

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
void adc_init() {
    adc_init();
    adc_gpio_init(26);  // A0 is connected to GPIO 26
}

// Read the soil moisture data
uint16_t read_soil_moisture() {
    return adc_read();
}


// Prepare the soil moisture data for transmission
void prepare_soil_moisture_data(char* buffer, size_t buffer_len) {
    uint16_t soil_moisture = read_soil_moisture();
    snprintf(buffer, buffer_len, "SM:%u", soil_moisture);
}



// Send the soil moisture data via LoRaWAN
void send_soil_moisture_data() {
    char buffer[32];
    prepare_soil_moisture_data(buffer, sizeof(buffer));
    LMIC_setTxData2(1, (uint8_t*)buffer, strlen(buffer), 0);
}