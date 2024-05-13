import paho.mqtt.client as mqtt
import ujson as json
import time
import machine
import network
import ubinascii
import struct
import sqlite3
from machine import SPI, Pin

class Ra02:
    def __init__(self, spi, nss, reset, dio0):
        self.spi = spi
        self.nss = nss
        self.reset = reset
        self.dio0 = dio0

        self.spi.init(baudrate=1000000, polarity=0, phase=0)

        self.nss.init(self.nss.OUT, value=1)
        self.reset.init(self.reset.OUT, value=1)
        self.dio0.init(self.dio0.IN)

        self.reset.value(0)
        time.sleep(0.1)
        self.reset.value(1)
        time.sleep(0.1)

        self.set_mode(0)

    def set_mode(self, mode):
        self.nss.value(0)
        self.spi.write(bytes([0x40 | mode]))
        self.nss.value(1)

    def receive(self):
        self.set_mode(3)
        
        while not self.dio0.value():
            pass

        self.nss.value(0)
        data = self.spi.read(8)
        self.nss.value(1)

        self.set_mode(0)

        return data

class Sim800l:
    def __init__(self, spi, reset, power):
        self.spi = spi
        self.reset = reset
        self.power = power
        
        self.spi.init(baudrate=9600, polarity=0, phase=0)

        self.reset.init(self.reset.OUT, value=1)
        self.power.init(self.power.OUT, value=1)

        self.power.value(0)
        time.sleep(0.1)
        self.power.value(1)
        time.sleep(3)

        self.reset.value(0)
        time.sleep(0.1)
        self.reset.value(1)
        time.sleep(3)

    def get_gps_data(self):
        self.spi.write(bytes('AT+CGPSPWR=1\r\n', 'utf-8'))
        time.sleep(1)
        self.spi.write(bytes('AT+CGPSINF=0\r\n', 'utf-8'))
        time.sleep(1)
        response = self.spi.read(100)
        response_str = response.decode('utf-8').strip().split(',')
        latitude = float(response_str[3]) / 100000
        longitude = float(response_str[5]) / 100000
        altitude = float(response_str[7])
        return (latitude, longitude, altitude)
    
lora = Ra02(spi=SPI(1), nss=Pin(10), reset=Pin(9), dio0=Pin(8))
gps = Sim800l(spi=SPI(2), reset=Pin(16), power=Pin(15))
adc = machine.ADC(28)
conn = sqlite3.connect('data.db')
c = conn.cursor()
c.execute('''CREATE TABLE IF NOT EXISTS data
             (timestamp TEXT, t REAL, h REAL, t2 REAL, n REAL, p REAL, k REAL, ph REAL)''')
conn.commit()
mqtt_client = mqtt.Client()
mqtt_client.connect('mqtt_broker_address')


def handle_lora_data(data):
    t, h, t2, n, p, k, ph = struct.unpack('>ffffff', data)
    data_json = {'t': t, 'h': h, 't2': t2, 'n': n, 'p': p, 'k': k, 'ph': ph}
    for key, value in data_json.items():
        topic = 'lora_id/' + key
        mqtt_client.publish(topic, str(value))

    
    c.execute('INSERT INTO data VALUES (?, ?, ?, ?, ?, ?, ?, ?)', (time.time(), t, h, t2, n, p, k, ph))
    conn.commit()


def handle_gps_data():
    gps_data = gps.get_gps_data()
    gps_data_json = {'latitude': gps_data[0], 'longitude': gps_data[1], 'altitude': gps_data[2]}
    mqtt_client.publish('lora_id/gps', json.dumps(gps_data_json))


def handle_battery_percentage():
    battery_percentage = adc.read() / 4095 * 100
    mqtt_client.publish('lora_id/battery', str(battery_percentage))


while True:
    if network.WLAN().isconnected():
        data = lora.receive()
        if data is not None:
            handle_lora_data(data)
        handle_gps_data()
        handle_battery_percentage()
    else:
        c.execute('INSERT INTO data VALUES (?, ?, ?, ?, ?, ?, ?, ?)', (time.time(), None, None, None, None, None, None, None))
        conn.commit()
