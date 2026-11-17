"""Generate the laboratory report: docs/laboratory-report.pdf.

Style (same as Lab 2): Times New Roman 12 pt, black text, justified paragraphs, grayscale figures.
Requires reportlab and Pillow. Run from any directory:  python scripts/build_lab_report.py
On Windows the real Times New Roman font is embedded; elsewhere the standard PDF Times font is used.
"""
from html import escape
from io import BytesIO
from pathlib import Path

from PIL import Image as PILImage
from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_JUSTIFY, TA_LEFT
from reportlab.lib.pagesizes import A4
from reportlab.lib.styles import ParagraphStyle
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.platypus import (Image, KeepTogether, PageBreak, Paragraph, SimpleDocTemplate,
                                Spacer, Table, TableStyle)

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "docs/laboratory-report.pdf"
IMAGES = ROOT / "docs/images"
REPORT_DATE = "17 November 2026"
TITLE = "Laboratory Activity 3: Activity Recognition"
MARGIN = 72                     # 1 inch
WIDTH = A4[0] - 2 * MARGIN      # usable text width

# ---------------------------------------------------------------- fonts
FONT_DIR = Path("C:/Windows/Fonts")
if (FONT_DIR / "times.ttf").exists():
    pdfmetrics.registerFont(TTFont("TNR", str(FONT_DIR / "times.ttf")))
    pdfmetrics.registerFont(TTFont("TNR-Bold", str(FONT_DIR / "timesbd.ttf")))
    pdfmetrics.registerFont(TTFont("TNR-Italic", str(FONT_DIR / "timesi.ttf")))
    pdfmetrics.registerFont(TTFont("TNR-BoldItalic", str(FONT_DIR / "timesbi.ttf")))
    pdfmetrics.registerFontFamily("TNR", normal="TNR", bold="TNR-Bold", italic="TNR-Italic", boldItalic="TNR-BoldItalic")
    REG, BOLD, ITAL = "TNR", "TNR-Bold", "TNR-Italic"
else:
    REG, BOLD, ITAL = "Times-Roman", "Times-Bold", "Times-Italic"

SIZE, LEAD = 12, 16
BLACK = colors.black

body = ParagraphStyle("body", fontName=REG, fontSize=SIZE, leading=LEAD, alignment=TA_JUSTIFY,
                      spaceAfter=8, textColor=BLACK, bulletFontName=REG, bulletFontSize=SIZE)
title = ParagraphStyle("title", parent=body, fontName=BOLD, alignment=TA_CENTER, spaceAfter=2)
centered = ParagraphStyle("centered", parent=body, alignment=TA_CENTER, spaceAfter=2)
heading = ParagraphStyle("heading", parent=body, fontName=BOLD, alignment=TA_LEFT, spaceBefore=10, spaceAfter=8,
                         keepWithNext=1)
subheading = ParagraphStyle("subheading", parent=body, fontName=BOLD, alignment=TA_LEFT, spaceBefore=6,
                            spaceAfter=6, keepWithNext=1)
cell = ParagraphStyle("cell", parent=body, alignment=TA_LEFT, spaceAfter=0, leading=14)
cell_bold = ParagraphStyle("cell_bold", parent=cell, fontName=BOLD)
caption = ParagraphStyle("caption", parent=body, fontName=ITAL, alignment=TA_CENTER, spaceAfter=12)
reference = ParagraphStyle("reference", parent=body, alignment=TA_LEFT)
code = ParagraphStyle("code", parent=body, fontName="Courier", fontSize=10, leading=13, alignment=TA_LEFT,
                      leftIndent=18, spaceAfter=10)

story = []


def p(text):
    story.append(Paragraph(text, body))


def ref(text):
    story.append(Paragraph(text, reference))


def h1(text, new_page=False):
    if new_page:
        story.append(PageBreak())
    story.append(Paragraph(text, heading))


def h2(text):
    story.append(Paragraph(text, subheading))


def bullets(items):
    for item in items:
        story.append(Paragraph(item, body, bulletText="\u2022"))


def listing(text):
    story.append(Paragraph(escape(text).replace("\n", "<br/>").replace("  ", "&nbsp;&nbsp;"), code))


def table(headers, rows, fractions):
    widths = [WIDTH * f for f in fractions]
    data = [[Paragraph(escape(str(v)), cell_bold) for v in headers]]
    data += [[Paragraph(escape(str(v)), cell) for v in row] for row in rows]
    t = Table(data, colWidths=widths, repeatRows=1, hAlign="CENTER")
    t.setStyle(TableStyle([
        ("GRID", (0, 0), (-1, -1), 0.5, BLACK),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
        ("LEFTPADDING", (0, 0), (-1, -1), 5),
        ("RIGHTPADDING", (0, 0), (-1, -1), 5),
        ("TOPPADDING", (0, 0), (-1, -1), 4),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
    ]))
    story.append(KeepTogether([t]))
    story.append(Spacer(1, 12))


FIGURE_NUMBER = [0]


def figure(name, max_height, text, max_width=WIDTH):
    """Add a numbered figure in grayscale with its caption under it."""
    FIGURE_NUMBER[0] += 1
    gray = PILImage.open(IMAGES / name).convert("L")
    buf = BytesIO()
    gray.save(buf, format="PNG")
    buf.seek(0)
    w, h = gray.size
    scale = min(max_width / w, max_height / h)
    im = Image(buf, width=w * scale, height=h * scale)
    cap = Paragraph("Figure %d. %s" % (FIGURE_NUMBER[0], text), caption)
    lead = []
    while story and isinstance(story[-1], Paragraph) and story[-1].style.name in ("heading", "subheading"):
        lead.insert(0, story.pop())
    story.append(KeepTogether(lead + [im, Spacer(1, 4), cap]))


def figures_side_by_side(items, height):
    """Several images in one row, each with its own numbered caption."""
    cells, caps = [], []
    col = WIDTH / len(items)
    for name, text in items:
        FIGURE_NUMBER[0] += 1
        gray = PILImage.open(IMAGES / name).convert("L")
        buf = BytesIO()
        gray.save(buf, format="PNG")
        buf.seek(0)
        w, h = gray.size
        scale = min((col - 12) / w, height / h)
        cells.append(Image(buf, width=w * scale, height=h * scale))
        caps.append(Paragraph("Figure %d. %s" % (FIGURE_NUMBER[0], text), caption))
    t = Table([cells, caps], colWidths=[col] * len(items))
    t.setStyle(TableStyle([("VALIGN", (0, 0), (-1, -1), "TOP"), ("ALIGN", (0, 0), (-1, -1), "CENTER")]))
    story.append(t)
    story.append(Spacer(1, 6))


def footer(canvas, doc):
    canvas.saveState()
    canvas.setFont(REG, SIZE)
    canvas.setFillColor(BLACK)
    canvas.drawCentredString(A4[0] / 2, 36, str(doc.page))
    canvas.restoreState()


# ================================================================= title
story.append(Paragraph(TITLE, title))
story.append(Paragraph("Laboratory Report", centered))
story.append(Spacer(1, 6))
story.append(Paragraph("Kevin Christian Villareal", centered))
story.append(Paragraph("BCA182 Embedded Systems Programming", centered))
story.append(Paragraph(REPORT_DATE, centered))
story.append(Paragraph("GitHub: github.com/uninhabitableelbow/bca182-activity-recognition", centered))
story.append(Spacer(1, 14))

# ================================================================= 1
h1("1. Problem and Requirements", new_page=False)
p("In this laboratory activity I built a complete Internet of Things system that tells, in real time, whether "
  "the person carrying the board is walking or running. The RT-Spark board reads its accelerometer and "
  "gyroscope and sends the readings over Wi-Fi with MQTT. A cloud service receives them, classifies them with "
  "a machine-learning model, stores the results in a database and serves them through a web API. An Android "
  "app reads the API and shows the current activity and the history.")
p("The lab sheet splits the work into three tasks, done in this order: (1) the cloud service and the "
  "machine-learning model, (2) the device firmware, and (3) the mobile app. The table below lists what each "
  "task asked for and what I built.")
table(["What the lab asked for", "What I built"], [
    ["Task 1: MQTT broker, ML classifier (TensorFlow or PyTorch), database (MariaDB, PostgreSQL or SQLite), "
     "web API",
     "Mosquitto 2.1, a PyTorch classifier service, SQLite, and a Flask web API with a web dashboard."],
    ["A model that uses temporal information, stays lightweight and generalises across rigorous tests",
     "A 1D convolutional network with 370 parameters over a 2-second window, tested on unseen days, rotated "
     "data and the other wrist."],
    ["Task 2: firmware that initialises the sensors, initialises Wi-Fi, connects to the broker and sends a "
     "message every second, using CubeMX, the CMSIS-Driver WiFi API and coreMQTT",
     "FreeRTOS firmware for the RT-Spark that does these steps in this order. The lab's Driver_WiFi0 sample "
     "code runs unchanged on top of my driver for the board's Wi-Fi module."],
    ["Task 3: Android app using Volley that shows the real-time prediction and the history, runs smoothly and "
     "is bug-free",
     "The Activity Monitor app in Java with Volley: a live card, a 60-minute summary and a scrolling "
     "history."],
], [0.45, 0.55])

h2("Hardware and differences from the lab sheet")
p("I used the RT-Spark board (STM32F407ZGT6, 168 MHz, 192 KB RAM) and an Android phone. The lab sheet was "
  "written for a board with an LSM6DSL sensor and an ISM43362 Wi-Fi module. When I checked my board, it has "
  "an ICM20608 sensor (it answered at I2C address 0x68 with the ID 0xAF) and an RW007 Wi-Fi module on SPI2. "
  "I kept the structure the sheet asks for and changed only what could not be used:")
table(["Lab sheet", "What I did", "Why"], [
    ["LSM6DSL sensor", "ICM20608 driver", "It is the sensor on my board."],
    ["ISM43362 Wi-Fi on SPI3 with its interrupt",
     "RW007 on SPI2 with its SPI and INT-pin interrupts. I ported RT-Thread's RW007 driver to FreeRTOS and "
     "wrote the CMSIS Driver_WiFi0 interface on top of it.",
     "It is the Wi-Fi module on my board. Because Driver_WiFi0 is the same API, the lab's sample code works "
     "as written."],
    ["CMSIS solution with cpackget packs",
     "PlatformIO with the CMSIS-Driver WiFi header and the coreMQTT sources; MQTT_DO_NOT_USE_CUSTOM_CONFIG "
     "defined in platformio.ini",
     "The course uses VSCode and PlatformIO, as in Labs 1 and 2."],
    ["Cloud virtual machine, or Docker",
     "The same four services run on my Windows PC, on the same Wi-Fi as the board",
     "I could not open a cloud account without a credit card (my student e-mail was not accepted), and "
     "Docker cannot be installed on this PC."],
    ["One reading every second",
     "Five readings every second, sent together in one message every second",
     "The training data was recorded at 5 readings per second, so the model needs that rate."],
], [0.24, 0.42, 0.34])

# ================================================================= 2
h1("2. System Design")
figure("system-architecture.png", 470, "The whole system. The board publishes over MQTT through the phone's "
       "hotspot; the services on the PC classify, store and serve the data; the app reads it over HTTP.")
p("Every second the board publishes one MQTT message to the topic bca182/activity/rt-spark-01/sensor. "
  "The device name in the topic lets the cloud handle several boards. The message is JSON:")
listing('{"device":"rt-spark-01","seq":42,\n "samples":[[ax,ay,az,gx,gy,gz], ... 5 readings]}')
p("Acceleration is in g with gravity included, and rotation rate is in radians per second, because those are "
  "the units of the training data. The sequence number lets the cloud notice a lost message. Each message is "
  "about 280 bytes, so the board sends less than 300 bytes per second.")

# ================================================================= 3
h1("3. Machine Learning Model")
h2("Dataset and units")
p("I used the Kaggle run-or-walk dataset suggested in the lab sheet: 88,588 readings recorded by one person "
  "over 12 days, 5 readings per second, with a label (walk or run) and the wrist the phone was worn on. The "
  "dataset does not state its units. From the data, the acceleration is about 1 when the device is still, so "
  "it is in g, and the rotation rates reach values that only make sense in radians per second. My firmware "
  "converts the ICM20608 readings to the same units.")
h2("How I chose the model")
p("The lab sheet asks for a model that uses temporal information, stays small, and generalises across "
  "rigorous testing scenarios. I started with the simplest model and added complexity only when it helped. "
  "Every number below is a leave-one-day-out result: each day is predicted by a model that never saw that day, "
  "so the model cannot pass by remembering a recording.")
table(["Model", "Uses time?", "Parameters", "Accuracy", "With random rotations"], [
    ["Logistic regression, one reading", "No", "14", "78.21%", "48.39%"],
    ["1D CNN, 2 s window", "Yes", "370", "99.95%", "57.61%"],
    ["GRU, 2 s window", "Yes", "402", "99.93%", "58.36%"],
], [0.34, 0.13, 0.17, 0.15, 0.21])
p("A single reading gives only 78%, while the two models that look at 2 seconds of readings reach 99.9%. "
  "This confirms the hint in the lab sheet that the time pattern of the movement matters. The CNN and the GRU "
  "are equally accurate, so I kept the CNN, which is smaller and simpler.")
p("I then tested a harder scenario: the same data, but every test window turned by a random 3D rotation, as "
  "if the board were held at any angle. All the models above fall to about 58%. This matters for my board, "
  "which is mounted differently from a phone on a wrist. I fixed it in two steps. First, I normalise each "
  "sensor with one scale shared by its three axes, so turning the board does not change the scale. Second, "
  "during training every window is turned by a random rotation, so the model learns the movement and not "
  "the direction.")
table(["Trained with rotations", "Window", "Filters", "Parameters", "Accuracy", "With random rotations"], [
    ["CNN", "1 s", "8", "370", "98.61%", "98.71%"],
    ["CNN", "2 s", "8", "370", "99.49%", "99.65%"],
    ["CNN", "2 s", "16", "1,122", "99.61%", "99.66%"],
    ["CNN", "4 s", "8", "370", "99.77%", "99.81%"],
], [0.22, 0.12, 0.12, 0.16, 0.16, 0.22])
p("I chose the 2-second window with 8 filters. It is within 0.2 percentage points of the best result, it "
  "has the fewest parameters, and a 2-second window means the app shows a change of activity about 2 "
  "seconds after it happens, where a 4-second window would take twice as long. The last test trains on one "
  "wrist and tests on the other: without rotation training the accuracy drops to 74.7% in one direction; with "
  "it, it stays at 98.8% and 99.0%.")
h2("Final model")
p("The final model is a 1D CNN: two convolution layers with 8 filters each, average pooling over time, and "
  "one output layer, 370 parameters in total. It was trained with the Adam optimiser on all the data with "
  "rotation augmentation, and it trains in seconds on a laptop CPU. Its output is a softmax probability, so "
  "every prediction comes with a confidence, which the dashboard and the app show. The model file is 4 KB.")

# ================================================================= 4
h1("4. Cloud Service")
p("The cloud service runs on my PC as four parts, which are the parts the lab sheet lists:")
bullets([
    "<b>MQTT broker:</b> Mosquitto 2.1 as a Windows service, listening on port 1883.",
    "<b>Classifier service</b> (classifier_service.py): subscribes to bca182/activity/+/sensor, checks each "
    "message, keeps the last 10 readings of each device and, once it has 10, classifies them after every new "
    "message. It saves both the readings and the predictions.",
    "<b>Database:</b> SQLite with two tables, readings and predictions. It runs in WAL mode so the API can "
    "read while the classifier writes.",
    "<b>Web API</b> (api.py, Flask): /api/latest, /api/history (newest first, paged), /api/summary (seconds "
    "of walking and running in the last minutes), /api/devices, /api/health, and a web dashboard at /.",
])
p("The classifier checks every message before using it: it must be JSON with a device name, a whole-number "
  "sequence number and 1 to 50 readings of 6 finite numbers. A bad message is logged and dropped, so a broken "
  "device cannot stop the service. If a sequence number is missing or the device was silent for more than 3 "
  "seconds, the device's window starts again, so a prediction never joins readings from two different "
  "moments.")
p("The /api/latest answer includes how old the prediction is and whether the device is live (newer than 5 "
  "seconds), so the dashboard and the app can tell a stopped board from a board that is standing still.")
h2("Running it on my PC instead of a cloud VM")
p("The lab sheet's main option is a virtual machine on AWS, Google Cloud or Azure. Each of them asked for a "
  "credit card, and Azure for Students did not accept my school e-mail. The sheet's second option is Docker on "
  "my PC, but Docker Desktop cannot be installed on this Windows 10 build. So I ran the same services directly "
  "on the PC. Moving them to a VM would only change the broker address in the firmware and the server address "
  "in the app.")
p("Two network settings were needed. scripts/setup_network_admin.ps1 makes Mosquitto listen on the network "
  "instead of only on the PC itself and opens ports 1883 and 5000 in the firewall. I also had to mark the "
  "hotspot as a private network, because Windows had a rule blocking Python on public networks, which stopped "
  "the phone from reaching the API.")

# ================================================================= 5
h1("5. Device Firmware")
h2("STM32CubeMX configuration")
table(["Item", "Setting"], [
    ["Clock", "8 MHz crystal, PLL to 168 MHz"],
    ["FreeRTOS", "CMSIS-RTOS v2, 48 KB heap, stack overflow check and heap-failure hook on"],
    ["Tasks", "SensorTask (2 KB stack) and NetworkTask (6 KB stack), both Normal priority"],
    ["SPI2 (Wi-Fi)", "Master, 10.5 Mbit/s, mode 0, SPI2 global interrupt on (the sheet's SPI3 step)"],
    ["Wi-Fi pins", "CS PF10, INT PG11 (falling-edge interrupt), RESET PG15; flash CS PB12 kept high"],
    ["Sensor I2C", "PF0 (data) and PF1 (clock), open-drain with pull-ups"],
    ["USART1", "PA9/PA10, 115200 baud, for the log"],
    ["HAL time base", "TIM6, because FreeRTOS uses SysTick"],
], [0.25, 0.75])
p("I wrote all my code in an App folder and called it from the USER CODE parts of the generated files, so "
  "CubeMX can generate the project again without losing anything.")
h2("Wi-Fi: from the RW007 to the lab's Driver_WiFi0")
p("The RW007 is not like the ISM43362 in the lab sheet. The ISM43362 has its own TCP/IP stack and is "
  "controlled with text commands. The RW007 only passes raw network frames over SPI, so the TCP/IP stack has "
  "to run on the STM32. I built the Wi-Fi support in three layers:")
bullets([
    "<b>rw007.c:</b> a FreeRTOS port of RT-Thread's RW007 driver. A high-priority task does the SPI transfers "
    "when the module pulls its INT pin low, and a second task passes frames to the network stack and answers "
    "commands (join, scan, read the MAC address).",
    "<b>wifi_netif.c:</b> connects the RW007 to lwIP 2.1.2, which provides TCP, DHCP and DNS.",
    "<b>Driver_WiFi_RW007.c:</b> implements ARM's CMSIS-Driver WiFi interface (ARM_DRIVER_WIFI "
    "Driver_WiFi0), including its socket functions, on top of lwIP.",
])
p("With these layers, the sample code from the lab sheet runs as written. I added the error checking the "
  "sheet asks for: every call's result is checked, a failed join is retried every 3 seconds, and the first "
  "failure (and every fifth one after it) lists the networks the module can see. That list helped me find "
  "that the board could not see the hotspot, because the phone was on 5 GHz and the RW007 only works on "
  "2.4 GHz.")
listing('Driver_WiFi0.Initialize(NULL);\nDriver_WiFi0.PowerControl(ARM_POWER_FULL);\n'
        'wifiConfig.ssid = WIFI_SSID;  wifiConfig.pass = WIFI_PASSWORD;\n'
        'wifiConfig.security = ARM_WIFI_SECURITY_WPA2;\nDriver_WiFi0.Activate(0, &wifiConfig);')
p("The Wi-Fi name and password are in wifi_config.h, which is not in the git repository. The repository has "
  "a template, wifi_config.h.example, instead.")
h2("MQTT with coreMQTT")
p("mqtt_link.c uses AWS coreMQTT 5.0.2. Its transport functions send and receive through the Driver_WiFi0 "
  "socket functions, so MQTT also goes through the lab's API. The board connects with a clean session and a "
  "30-second keep-alive, and publishes with QoS 0: a lost reading is replaced one second later, so waiting "
  "for acknowledgements would only add delay.")
h2("Tasks")
figure("firmware-tasks.png", 430, "Firmware tasks. SensorTask samples, NetworkTask sends; a queue joins them.")
p("The firmware follows the lab sheet's sequence. SensorTask initialises the ICM20608, measures the gyroscope "
  "offset during 1 second at rest, and then signals NetworkTask, which starts Wi-Fi and MQTT. From then on, "
  "SensorTask reads the sensor every 200 ms with vTaskDelayUntil, so the timing does not drift, and puts every "
  "5 readings into a queue. NetworkTask turns each batch into the JSON message and publishes it.")
p("The queue holds 4 batches. If the network is slow and the queue is full, the oldest batch is dropped, "
  "because for real-time recognition new data is worth more than old data. After a reconnection the queue is "
  "emptied for the same reason.")

# ================================================================= 6
h1("6. Mobile App")
p("The Activity Monitor app is written in Java and uses Volley for all HTTP requests, as the lab sheet "
  "recommends. Volley runs the network work on its own threads and returns the answers on the main thread, so "
  "the screen never waits for the network.")
figures_side_by_side([
    ("app-run.png", "Live prediction: RUN while the board is shaken."),
    ("app-offline.png", "The board stopped sending: the card turns grey."),
], 330)
p("The screen has three parts:")
bullets([
    "<b>Live prediction</b> (/api/latest, every second): a large card that is blue for WALK and orange for "
    "RUN, with the confidence, the time and the sequence number. When no data arrives for 5 seconds it turns "
    "grey and shows how long ago the last data came, and it shows a red message if the server cannot be "
    "reached.",
    "<b>Last 60 minutes</b> (/api/summary, every 10 seconds): a bar with the share of walking and running.",
    "<b>History</b> (/api/history, every 2 seconds): the predictions, newest first. Only rows newer than the "
    "top row are added. Scrolling to the bottom loads the next older page with before_id.",
])
p("To keep the app smooth, only one request of each kind is in flight at a time, so requests cannot pile up "
  "on a slow network. The list adds rows with range updates instead of redrawing everything, and polling "
  "stops when the app goes to the background. The <b>Server</b> button sets the API address and the device "
  "name, which are saved, so a new PC address does not need a new build. The code is split into three "
  "classes: MainActivity (screen and polling), HistoryAdapter (the list) and Prediction (one record and its "
  "formatting). All text is in strings.xml.")
p("I did not install Android Studio, which needs about 10 GB. I built the app with the same tools Android "
  "Studio uses (JDK 17, the Android SDK and Gradle) from the command line, and installed the APK on my phone "
  "by downloading it from the PC.")

# ================================================================= 7
h1("7. Testing")
h2("Unit tests")
table(["Part", "Tests", "What they check"], [
    ["Cloud (Python unittest)", "16",
     "Message checking (bad JSON, wrong types, NaN, too many readings), the window filling and resetting on "
     "a gap or silence, separate devices, every API endpoint, bad parameters, paging."],
    ["Firmware (Unity on the PC, pio test -e native)", "13",
     "Unit conversion to g and rad/s, gyroscope offset, number formatting, the exact JSON format, the largest "
     "message fitting the 512-byte buffer, unsafe device names, small buffers."],
], [0.28, 0.10, 0.62])
h2("Tests on the real system")
table(["Test", "Result"], [
    ["Model on unseen days / rotated / other wrist", "99.49% / 99.65% / 98.8% and 99.0%"],
    ["Recorded data replayed through the whole cloud pipeline", "38 of 39 predictions correct"],
    ["Board start-up", "Sensor found, Wi-Fi joined 6.1 s after the join started (8.6 s after power-up), MQTT connected 0.1 s later"],
    ["Board sending", "One message per second, a prediction every second"],
    ["Board at rest / shaken", "WALK / RUN within about 1 second"],
    ["App live card, history and summary", "Correct, updated every second"],
    ["Board stops sending", "App card turns grey with the time since the last data"],
    ["Connection dropped silently by the PC", "Board reconnects by itself in about 1 second"],
], [0.55, 0.45])
figures_side_by_side([
    ("demo-board-run.jpg", "Shaking the board in front of the dashboard: RUN."),
    ("dashboard-run-to-walk.png", "Dashboard history: run while shaken, then walk again."),
], 320)
p("The dashboard history shows one prediction of WALK with 70% confidence right after the shaking stopped. "
  "The 2-second window still held a second of shaking, so the model was less sure. This is the expected "
  "delay of a 2-second window.")
p("The full list of 23 tests is in docs/functional-verification.md, and docs/requirements-traceability.md "
  "connects every requirement of the lab sheet to the code and the test that checks it.")
h2("Static analysis")
p("Cppcheck on my firmware code found 0 high and 0 medium issues. I fixed the one real low issue (a value "
  "that was never used); the remaining 9 low notes are byte layouts of the RW007 protocol and functions "
  "whose types are fixed by the C library and lwIP. Android Lint first reported 0 errors and 22 warnings. I "
  "fixed 19, including a real one (upper-casing without a locale), and 3 remain on purpose, for example "
  "plain HTTP, which is needed because the API runs on a local PC without a certificate. Details are in "
  "docs/static-analysis.md.")

# ================================================================= 8
h1("8. Discussion")
h2("Problems I solved")
p("<b>A different board.</b> The lab sheet's sensor, Wi-Fi module and driver files do not exist on my board. "
  "I identified the real parts by reading their ID registers and RT-Thread's files for the RT-Spark, then "
  "wrote the drivers. Putting the CMSIS Driver_WiFi0 interface on top of the RW007 kept the lab's sample code "
  "and structure.")
p("<b>The hotspot was invisible.</b> At first the board could not join. The scan I had added to the error "
  "handling listed eight neighbouring networks but not mine, which showed that the phone's hotspot was on "
  "5 GHz. After switching it to 2.4 GHz the board joined in about 6 seconds.")
p("<b>A hidden hang.</b> When I changed the PC's network profile, Windows dropped the board's connection "
  "without telling the board. The board kept sending until its TCP buffer was full, and then the send call "
  "waited forever. coreMQTT did not notice, because it only sends keep-alive pings when nothing else is "
  "being sent. I added a 2-second send timeout, limited coreMQTT's send retries to 5 seconds, and made it "
  "ping after 10 seconds without any reply. When I repeated the same action, the board found the dead "
  "connection and reconnected within about 1 second each time.")
p("<b>A model that only worked in one position.</b> The first models were 99.9% accurate on the dataset but "
  "fell to 58% when the data was rotated. Without the rotation test I would have deployed a model that was "
  "very good on paper and poor on my board. Rotation-safe scaling and rotation augmentation fixed it.")
h2("Limitations")
bullets([
    "The dataset comes from one person. The model may be less accurate for people who walk or run differently.",
    "The model has only two classes. Standing still is reported as walking, as the screenshots show.",
    "I could not walk or run with the board, because it takes its power over USB from the PC. I tested by "
    "shaking it, which shows the system works from end to end but does not measure how well it separates "
    "real walking from real running on this sensor. The next step would be a power bank and recordings of my "
    "own walking and running, which would also give data from a second person.",
    "The services run on my PC, not on a cloud VM, and the API uses plain HTTP. On a VM I would add HTTPS and "
    "a password on the broker.",
])
h2("Other ways I could have done it")
p("The lab sheet allows Bluetooth LE with the phone as a gateway instead of Wi-Fi. I chose Wi-Fi because the "
  "board has a Wi-Fi module and the sheet's main path uses it. The model could also run on the board itself "
  "(370 parameters is small enough), sending only the result, but the lab asks for the classifier in the "
  "cloud. Finally, the app polls the API; a WebSocket or MQTT connection from the app would push changes "
  "instead, but polling with Volley is what the lab recommends and is simple and reliable.")

# ================================================================= 9
h1("9. Conclusion")
p("I built every part of the system that the lab sheet describes: a PyTorch model that generalises to unseen "
  "days, rotations and the other wrist with 370 parameters; an MQTT, SQLite and Flask cloud service; FreeRTOS "
  "firmware that follows the sheet's sequence through the CMSIS Driver_WiFi0 API and coreMQTT; and an "
  "Android app with Volley that shows the live prediction and the history. The parts the sheet assumes but my "
  "board does not have were replaced with the board's own parts behind the same interfaces. The whole chain "
  "works on the real hardware: when I shake the board, the phone shows RUN about a second later.")
h2("References")
ref("[1] P. R. P. Castor, Laboratory Activity No. 3: Activity Recognition, MSU-IIT, 2026.")
ref("[2] V. Malyi, Run or Walk dataset, kaggle.com/vmalyi/run-or-walk.")
ref("[3] RT-Thread, RW007 SPI Wi-Fi driver (spi_wifi_rw007.c) and the RT-Spark board support package, "
  "github.com/RT-Thread/rt-thread and github.com/RT-Thread-Studio/sdk-bsp-stm32f407-spark.")
ref("[4] ARM, CMSIS-Driver WiFi interface (Driver_WiFi.h); AWS, coreMQTT documentation, "
  "docs.aws.amazon.com/freertos/latest/userguide/coremqtt.html.")
ref("[5] lwIP 2.1.2; FreeRTOS; STMicroelectronics STM32CubeMX 6.18.1 and STM32CubeF4 1.28.3.")
ref("[6] PyTorch, Flask, Eclipse Mosquitto and Android Volley (developer.android.com/training/volley) "
  "documentation.")
ref("[7] Project files: README.md, docs/functional-verification.md, docs/requirements-traceability.md, "
  "docs/static-analysis.md and ml/results/results.md.")

doc = SimpleDocTemplate(str(OUT), pagesize=A4, leftMargin=MARGIN, rightMargin=MARGIN,
                        topMargin=MARGIN, bottomMargin=MARGIN, title=TITLE,
                        author="Kevin Christian Villareal",
                        subject="BCA182 Laboratory Activity 3 report")
doc.build(story, onFirstPage=footer, onLaterPages=footer)
print(OUT)
