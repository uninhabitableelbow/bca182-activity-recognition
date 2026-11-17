"""Draw the documentation diagrams into docs/images/ (PNG, tall layout for A4 pages).

Usage:  python tools/make_diagrams.py
Diagrams: system-architecture, firmware-tasks.
They describe firmware/App, cloud/ and android/ - update both together.
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parents[1] / "docs" / "images"
OUT.mkdir(parents=True, exist_ok=True)

NAVY = (23, 53, 74)
TEAL = (22, 123, 128)
GREY = (90, 104, 112)
LIGHT = (234, 242, 245)
WHITE = (255, 255, 255)
BOARD = (226, 236, 250)
CLOUD = (230, 244, 236)
PHONE = (250, 240, 222)
TASK = (255, 255, 255)


def font(size):
    return ImageFont.load_default(size=size)


F_TITLE, F_HEAD, F_TEXT, F_SMALL = font(30), font(23), font(20), font(18)
LINE = 26


class Canvas:
    def __init__(self, w, h, title):
        self.img = Image.new("RGB", (w, h), WHITE)
        self.d = ImageDraw.Draw(self.img)
        self.d.text((30, 18), title, font=F_TITLE, fill=NAVY)

    def group(self, x, y, w, h, label, fill):
        self.d.rounded_rectangle((x, y, x + w, y + h), radius=16, fill=fill, outline=GREY, width=2)
        self.d.text((x + 16, y + 10), label, font=F_HEAD, fill=GREY)

    def box(self, x, y, w, h, head, lines=(), fill=TASK, edge=NAVY):
        self.d.rounded_rectangle((x, y, x + w, y + h), radius=10, fill=fill, outline=edge, width=2)
        self.d.text((x + 12, y + 8), head, font=F_HEAD, fill=NAVY)
        ty = y + 40
        for line in lines:
            self.d.text((x + 12, ty), line, font=F_TEXT, fill=(30, 30, 30))
            ty += LINE

    def arrow(self, pts, label=None, at=None, color=TEAL, anchor="lm"):
        self.d.line(pts, fill=color, width=3, joint="curve")
        (x1, y1), (x2, y2) = pts[-2], pts[-1]
        import math
        a = math.atan2(y2 - y1, x2 - x1)
        s = 14
        self.d.polygon([(x2, y2), (x2 - s * math.cos(a - 0.4), y2 - s * math.sin(a - 0.4)),
                        (x2 - s * math.cos(a + 0.4), y2 - s * math.sin(a + 0.4))], fill=color)
        if label:
            if at is None:
                at = ((pts[0][0] + x2) / 2 + 12, (pts[0][1] + y2) / 2)
            for i, text in enumerate(label.split("\n")):
                pos = (at[0], at[1] + i * 22)
                b = self.d.textbbox(pos, text, font=F_SMALL, anchor=anchor)
                self.d.rectangle((b[0] - 3, b[1] - 2, b[2] + 3, b[3] + 2), fill=WHITE)
                self.d.text(pos, text, font=F_SMALL, fill=color, anchor=anchor)

    def save(self, name):
        self.img.save(OUT / name, optimize=True)
        print(OUT / name)


def system_architecture():
    c = Canvas(1000, 1500, "System architecture")
    # Device
    c.group(40, 70, 920, 320, "RT-Spark board (STM32F407, FreeRTOS)", BOARD)
    c.box(70, 115, 400, 120, "ICM20608 IMU", ["accelerometer + gyroscope", "soft I2C on PF0/PF1"])
    c.box(530, 115, 400, 120, "RW007 Wi-Fi module", ["SPI2, raw Ethernet frames", "lwIP TCP/IP on the STM32"])
    c.box(70, 260, 860, 110, "Firmware", ["SensorTask: 5 readings/s  ->  queue  ->  NetworkTask: coreMQTT",
                                           "Driver_WiFi0 (CMSIS-Driver WiFi API) -> lwIP sockets -> RW007"])
    # Network
    c.arrow([(500, 390), (500, 480)], "MQTT publish, 1 message/s\nbca182/activity/rt-spark-01/sensor",
            at=(520, 410))
    c.box(300, 480, 400, 80, "Phone hotspot \"weh\" (Wi-Fi)", [], fill=LIGHT)
    c.arrow([(500, 560), (500, 640)])
    # Cloud
    c.group(40, 640, 920, 520, "Cloud service (this PC, run natively)", CLOUD)
    c.box(70, 690, 400, 110, "Mosquitto 2.1", ["MQTT broker, port 1883"])
    c.box(530, 690, 400, 140, "Classifier service", ["paho-mqtt subscriber", "PyTorch 1D CNN, 370 params",
                                                      "2 s window -> walk / run"])
    c.arrow([(470, 745), (530, 745)])
    c.box(530, 880, 400, 110, "SQLite database", ["readings, predictions"])
    c.arrow([(730, 830), (730, 880)])
    c.box(70, 880, 400, 140, "Flask web API, port 5000", ["/api/latest  /api/history", "/api/summary  /api/devices",
                                                           "/  (web dashboard)"])
    c.arrow([(530, 935), (470, 935)], "SQL", at=(500, 915), anchor="mm")
    c.box(70, 1050, 860, 80, "Browser dashboard (polls every second)", [], fill=WHITE)
    c.arrow([(270, 1020), (270, 1050)])
    # Phone
    c.arrow([(270, 1160), (270, 1250)], "HTTP + JSON (Volley)", at=(290, 1195))
    c.group(40, 1250, 920, 210, "Android phone", PHONE)
    c.box(70, 1295, 860, 140, "Activity Monitor app (Java, Volley)",
          ["live prediction card (1 s), 60-minute walk/run bar (10 s)",
           "history list: new rows every 2 s, older pages on scroll",
           "server address and device name saved in settings"])
    c.save("system-architecture.png")


def firmware_tasks():
    c = Canvas(1000, 1160, "Firmware tasks and communication")
    c.box(60, 80, 400, 210, "SensorTask (Normal)", ["1. init ICM20608", "2. calibrate gyro (1 s)",
                                                     "3. every 200 ms: read IMU", "4. every 5 readings:",
                                                     "   queue one batch"])
    c.box(540, 80, 400, 210, "NetworkTask (Normal)", ["1. wait: sensors ready", "2. Driver_WiFi0 Initialize,",
                                                       "   PowerControl, Activate", "3. coreMQTT connect",
                                                       "4. publish each batch"])
    c.arrow([(460, 270), (540, 270)], "sensors\nready", at=(500, 222), anchor="mm")
    c.box(330, 350, 340, 110, "batch_queue", ["4 batches of 5 readings", "full -> drop the oldest"], fill=LIGHT)
    c.arrow([(260, 290), (260, 405), (330, 405)])
    c.arrow([(670, 405), (700, 405), (700, 290)])
    c.box(540, 520, 400, 140, "lwIP tcpip thread (High)", ["TCP/IP, DHCP, ARP", "sockets used by", "Driver_WiFi_RW007.c"])
    c.arrow([(850, 290), (850, 520)], "SocketSend /\nSocketRecv", at=(862, 395))
    c.box(540, 720, 400, 170, "RW007 driver tasks", ["xfer task (High): SPI2 frames,", "  woken by the INT pin (EXTI)",
                                                    "process task (Above normal):", "  frames -> lwIP, events, replies"])
    c.arrow([(740, 660), (740, 720)])
    c.box(540, 950, 400, 90, "RW007 module", ["SPI2 + CS PF10, INT PG11"], fill=LIGHT)
    c.arrow([(740, 890), (740, 950)])
    c.box(60, 520, 400, 200, "Shared resources", ["UART log: mutex in app_log()", "I2C bus: SensorTask only",
                                                   "SPI2: xfer task only", "RW007 commands: cmd_mutex",
                                                   "lwIP: core locking"], fill=LIGHT)
    c.box(60, 780, 400, 260, "Failure handling", ["IMU read fails: skip that second,", "  seq gap resets cloud window",
                                                   "Wi-Fi join fails: retry, scan", "MQTT dead link: 2 s send timeout,",
                                                   "  10 s ping, 5 s answer -> reconnect", "stack overflow / heap: hooks log",
                                                   "  the task name"], fill=LIGHT)
    c.d.text((60, 1090), "Priorities are FreeRTOS CMSIS-RTOS v2 levels. Only the lwIP and RW007 threads run above",
             font=F_SMALL, fill=GREY)
    c.d.text((60, 1115), "Normal, so network traffic is handled as soon as it arrives.", font=F_SMALL, fill=GREY)
    c.save("firmware-tasks.png")


if __name__ == "__main__":
    system_architecture()
    firmware_tasks()
