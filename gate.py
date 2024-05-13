import machine
import time
import ustruct
import usocket as socket
import ujson
from umqtt.simple import MQTTClient
import sqlite3

# Initialize the SPI interface and connect to the SIM800L module
spi = machine.SPI(1, baudrate=1000000, sck=machine.Pin(5), mosi=machine.Pin(6), miso=machine.Pin(7))
sim800l = machine.UART(2, baudrate=9600, tx=machine.Pin(8), rx=machine.Pin(9))
sim800l.init(9600, bits=8, parity=None, stop=1)

datetime_data = {}

# Define a function to send AT commands to the SIM800L module
def send_at_command(command):
    sim800l.write(command + b'\r\n')
    time.sleep(1)
    response = sim800l.readline()
    return response.decode('utf-8').rstrip()

# Set up the SIM800L module for MQTT communication
send_at_command(b'AT+SAPBR=3,1,"CONTYPE","GPRS"')
send_at_command(b'AT+SAPBR=3,1,"APN","Megafon"')
send_at_command(b'AT+SAPBR=1,1')

# Get the IP address assigned to the SIM800L
response = send_at_command(b'AT+SAPBR=2,1')
ip_address = response.split(':')[1].split(',')[0]

# Set up the MQTT client
mqtt_client = MQTTClient("client_id", "91.230.107.224", user="amir.mukumov", password="123123")

# Define a function to send MQTT data
def send_mqtt_data(topic, payload):
    if mqtt_client.isconnected():
        mqtt_client.publish(topic, payload)
    else:
        conn = sqlite3.connect("data.db")
        cursor = conn.cursor()
        cursor.execute("CREATE TABLE IF NOT EXISTS data (topic TEXT, payload TEXT)")
        cursor.execute("INSERT INTO data (topic, payload) VALUES (?, ?)", (topic, payload))
        conn.commit()
        conn.close()

# Parse the incoming LoRaWAN data and send it as MQTT data
def handle_lorawan_data(data_string):
    # Parse the incoming LoRaWAN data
    sensor_data = ujson.loads(data_string)

    # Get the current timestamp
    timestamp = time.time()

    # Send each sensor reading as a separate MQTT topic, with the timestamp included
    for sensor_name, sensor_value in sensor_data.items():
        topic = "remote_device/" + sensor_name
        payload = str(sensor_value) + "|" + str(timestamp)
        send_mqtt_data(topic, payload)

# Send GPS and datetime data as MQTT data (assuming you have a GPS module connected to the RV1103 Pi)
def handle_gps_data(gps_data):
    send_mqtt_data("gps/latitude", gps_data["latitude"])
    send_mqtt_data("gps/longitude", gps_data["longitude"])
    send_mqtt_data("gps/altitude", gps_data["altitude"])

    # Extract the datetime data from the GPS data
    datetime_data = {
        "year": gps_data["year"],
        "month": gps_data["month"],
        "day": gps_data["day"],
        "hour": gps_data["hour"],
        "minute": gps_data["minute"],
        "second": gps_data["second"]
    }

    # Send datetime data as a single MQTT topic, with the data formatted as a string
    topic = "datetime"
    payload = "{year}-{month:02d}-{day:02d}T{hour:02d}:{minute:02d}:{second:02d}".format(**datetime_data)
    send_mqtt_data(topic, payload)

# Send battery percentage as MQTT data
def handle_battery_data(battery_percentage):
    topic = "gateway/battery_percentage"
    payload = str(battery_percentage)
    send_mqtt_data(topic, payload)

# Connect to the MQTT server
mqtt_client.connect()

# Check the LTE connection status every 10 seconds
while True:
    # Check if the LTE connection is active
    if send_at_command(b"AT+CSQ")).split(",")[0] != "0":
        # Send any data in the SQLite database to the MQTT server
        conn = sqlite3.connect("data.db")
        cursor = conn.cursor()
        cursor.execute("SELECT * FROM data")
        rows = cursor.fetchall()
        for row in rows:
            topic, payload = row
            mqtt_client.publish(topic, payload)
        cursor.execute("DELETE FROM data")
        conn.commit()
        conn.close()
    else:
        # LTE connection is not active, do nothing
        pass
    time.sleep(10)
