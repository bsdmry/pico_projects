#!/usr/bin/env python3
"""EZ DM-441B TCP monitor. Run with python3 dm441b_gui.py.

Uses only the Python standard library, including Tkinter. The SCPI client,
status decoder, segment artwork, and diode icon are embedded.
"""
from dataclasses import dataclass
import ipaddress
import queue
import socket
import threading
import tkinter as tk
from tkinter import messagebox


BACKGROUND = "#000000"
INACTIVE = "#454545"
ACTIVE = "#00ff00"

SEGMENTS = {'m': [[36.59275, 8.461],
       [75.08045, 8.69818],
       [84.80821, 1.23108],
       [84.80821, 0.0],
       [35.40448, 0.21231]],
 'a': [[30.53859, 12.38235],
       [27.90567, 25.12234],
       [36.3089, 41.56557],
       [38.85433, 41.02671],
       [36.50515, 20.26135],
       [32.36975, 12.42291]],
 'b': [[45.0846, 12.35609],
       [53.68228, 12.11524],
       [51.07387, 29.82982],
       [44.56637, 43.06578],
       [42.67689, 43.10428],
       [41.21747, 31.86664]],
 'c': [[70.74335, 12.0899],
       [53.96901, 31.75546],
       [47.66338, 44.12915],
       [49.15399, 45.49933],
       [56.43797, 42.15401],
       [70.60303, 25.46234],
       [73.23424, 13.36059]],
 'n': [[80.86775, 42.09952],
       [87.39549, 5.42722],
       [85.81915, 4.37963],
       [76.55035, 11.64995],
       [71.11771, 42.68089],
       [74.51983, 46.47459],
       [75.61191, 46.28227]],
 'd': [[46.55554, 53.58509],
       [65.93391, 53.66589],
       [71.47748, 48.75215],
       [68.12154, 44.87725],
       [58.45723, 44.86825],
       [48.53692, 49.80234],
       [44.43996, 49.86334],
       [44.36486, 51.54859]],
 'h': [[38.8638, 45.30612],
       [22.64168, 45.25612],
       [17.16343, 49.76814],
       [20.709, 53.73542],
       [31.40446, 53.93287],
       [39.16919, 50.17507]],
 'k': [[27.74704, 8.77668],
       [26.24036, 16.09749],
       [24.46613, 16.08539],
       [18.78508, 5.18313],
       [19.54891, 2.5696],
       [23.69993, 0.35997],
       [30.10558, 0.01406],
       [32.67035, 4.19094],
       [31.49676, 8.67588]],
 'u': [[21.07539, 42.22177],
       [15.74529, 46.97667],
       [14.11486, 46.76963],
       [10.75254, 42.64143],
       [16.90551, 13.33396],
       [18.9327, 13.02111],
       [24.66443, 25.07536]],
 't': [[11.1764, 88.77878],
       [18.12293, 56.58814],
       [15.30778, 52.90999],
       [12.9459, 52.81709],
       [8.39553, 56.61023],
       [0.0, 96.28011],
       [1.40213, 96.68877]],
 'g': [[38.37116, 55.91132],
       [29.43596, 73.9362],
       [18.27973, 88.00364],
       [15.88249, 88.22974],
       [15.11078, 87.23212],
       [17.74064, 74.97508],
       [32.2532, 56.30401]],
 'f': [[39.55822, 87.04259],
       [43.21562, 68.58593],
       [42.6598, 58.48091],
       [41.27511, 58.18525],
       [32.39087, 76.36013],
       [30.06765, 87.81494]],
 'e': [[53.77499, 57.83706],
       [61.51196, 74.63362],
       [59.47613, 87.07015],
       [57.395, 87.40903],
       [47.40126, 66.7723],
       [46.4804, 57.82861]],
 'p': [[72.883, 86.16105],
       [78.26885, 56.24973],
       [75.19507, 52.37266],
       [73.19582, 52.21615],
       [67.74793, 57.13473],
       [63.44671, 83.47587],
       [69.71752, 84.09782],
       [71.18382, 86.87794]],
 's': [[4.19102, 101.12498],
       [54.64216, 100.52999],
       [50.7383, 91.59406],
       [14.11096, 91.87506],
       [3.23884, 100.23651]],
 'r': [[55.90205, 93.48799],
       [59.42961, 100.82582],
       [66.71876, 100.73442],
       [70.82234, 97.03651],
       [70.79824, 94.62633],
       [67.5919, 88.11149],
       [64.49081, 87.36684],
       [62.82511, 88.19347],
       [62.23725, 90.29956],
       [61.0147, 91.65698],
       [55.79652, 91.30794]]}
DECIMAL = {'cx': 83.949773, 'cy': 95.17905, 'rx': 6.1188517, 'ry': 6.0109887}
GLYPH_HEIGHT = 101.19003870000002

# Vector primitives extracted from /home/igor/Pictures/prj/diode_beep.svg.
DIODE_WIDTH = 79.69813618349082
DIODE_HEIGHT = 32.110672173187695
DIODE_PARTS = [('line', (2.0, 16.27857, 51.48574, 15.88485), 4.0),
 ('polygon', (15.13415, 2.49996, 15.16438, 29.5504, 36.48098, 16.08171), 1.0),
 ('line', (36.98094, 2.0, 36.98094, 30.05036, 36.98094, 30.05036), 4.0),
 ('oval', (56.92685, 14.27236, 60.68143, 17.778), 1.0),
 ('line',
  (60.25376,
   7.91073,
   60.70923,
   8.1451,
   61.1492,
   8.39983,
   61.57262,
   8.6743,
   61.97848,
   8.96786,
   62.36583,
   9.27982,
   62.73374,
   9.60943,
   63.08133,
   9.95592,
   63.40779,
   10.31846,
   63.71233,
   10.69619,
   63.99423,
   11.08822,
   64.25283,
   11.4936,
   64.48751,
   11.91138,
   64.69771,
   12.34057,
   64.88294,
   12.78015,
   65.04275,
   13.22907,
   65.17676,
   13.68627,
   65.28466,
   14.15066,
   65.36619,
   14.62114,
   65.42115,
   15.09659,
   65.44942,
   15.57588,
   65.45092,
   16.05788,
   65.42566,
   16.54144,
   65.3737,
   17.0254,
   65.29515,
   17.50863,
   65.19021,
   17.98998,
   65.05912,
   18.46829,
   64.9022,
   18.94244,
   64.71981,
   19.4113,
   64.51239,
   19.87376,
   64.28044,
   20.32871,
   64.0245,
   20.77508,
   63.74519,
   21.2118,
   63.44316,
   21.63785,
   63.11913,
   22.0522,
   62.77387,
   22.45388,
   62.40821,
   22.84193,
   62.023,
   23.21543,
   61.61917,
   23.57349,
   61.19768,
   23.91526,
   60.75952,
   24.23992),
  3.01771364),
 ('line',
  (65.19142,
   3.83415,
   65.72378,
   4.18562,
   66.23802,
   4.56762,
   66.73292,
   4.97922,
   67.2073,
   5.41946,
   67.66003,
   5.88729,
   68.09005,
   6.3816,
   68.49632,
   6.90122,
   68.87789,
   7.4449,
   69.23384,
   8.01136,
   69.56333,
   8.59926,
   69.86559,
   9.20719,
   70.13988,
   9.83372,
   70.38557,
   10.47736,
   70.60206,
   11.13657,
   70.78885,
   11.80979,
   70.94548,
   12.49543,
   71.0716,
   13.19185,
   71.16689,
   13.8974,
   71.23113,
   14.61041,
   71.26417,
   15.32918,
   71.26593,
   16.05201,
   71.2364,
   16.77718,
   71.17567,
   17.50296,
   71.08386,
   18.22763,
   70.9612,
   18.94948,
   70.80798,
   19.66679,
   70.62457,
   20.37784,
   70.41139,
   21.08097,
   70.16896,
   21.77449,
   69.89786,
   22.45676,
   69.59871,
   23.12616,
   69.27224,
   23.78109,
   68.91923,
   24.42001,
   68.5405,
   25.0414,
   68.13696,
   25.64378,
   67.70957,
   26.22571,
   67.25934,
   26.78583,
   66.78734,
   27.32279,
   66.29469,
   27.83532,
   65.78256,
   28.32222),
  3.002919),
 ('line',
  (71.84183,
   1.54701,
   72.3979,
   1.964,
   72.93505,
   2.4172,
   73.452,
   2.90552,
   73.94751,
   3.42782,
   74.42041,
   3.98285,
   74.86958,
   4.5693,
   75.29395,
   5.18576,
   75.69251,
   5.83078,
   76.06432,
   6.50283,
   76.40849,
   7.2003,
   76.72421,
   7.92155,
   77.01072,
   8.66486,
   77.26736,
   9.42846,
   77.49349,
   10.21055,
   77.6886,
   11.00926,
   77.85221,
   11.82269,
   77.98394,
   12.64892,
   78.08348,
   13.48598,
   78.15058,
   14.33189,
   78.18509,
   15.18464,
   78.18693,
   16.04219,
   78.15609,
   16.90252,
   78.09265,
   17.76359,
   77.99675,
   18.62333,
   77.86863,
   19.47973,
   77.70859,
   20.33073,
   77.517,
   21.17433,
   77.29433,
   22.00851,
   77.0411,
   22.8313,
   76.75792,
   23.64073,
   76.44545,
   24.4349,
   76.10444,
   25.21191,
   75.73569,
   25.96992,
   75.34009,
   26.70713,
   74.91858,
   27.42179,
   74.47215,
   28.11219,
   74.00186,
   28.77671,
   73.50884,
   29.41376,
   72.99424,
   30.02182,
   72.45931,
   30.59947),
  3.0224132)]

# Names follow the source SVG, including its split top/bottom segments.
CHARACTERS = {
    "0": "kmnprstu", "1": "np", "2": "kmnhdtsr", "3": "kmnphdsr",
    "4": "unphd", "5": "kmuhdpsr", "6": "kmuhdtpsr", "7": "kmnp",
    "8": "kmnprstuhd", "9": "kmunphdsr",
    "O": "kmnprstu", "V": "utgc", "E": "kmuhdtsr", "R": "kmunhdte",
    "-": "hd", " ": "",
}


@dataclass(frozen=True)
class Reading:
    status: int
    magnitude: int
    decimal_position: int
    negative: bool
    underrange: bool
    overrange: bool
    leds: int


def decode_status(status):
    if not 0 <= status < (1 << 64):
        raise ValueError('status is outside the unsigned 64-bit range')
    magnitude = (status >> 16) & ((1 << 43) - 1)
    if magnitude > 99999:
        raise ValueError('status magnitude exceeds the five-digit display')
    return Reading(status, magnitude, (status >> 61) & 3, bool(status >> 63),
                   bool(status & (1 << 60)), bool(status & (1 << 59)), status & 0xffff)


class Meter:
    def __init__(self, host, port, timeout):
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.buffer = bytearray()

    def close(self):
        self.sock.close()

    def query(self, command):
        self.sock.sendall(command.encode('ascii') + b'\n')
        while b'\n' not in self.buffer:
            chunk = self.sock.recv(256)
            if not chunk:
                raise ConnectionError('meter closed the connection')
            self.buffer.extend(chunk)
            if len(self.buffer) > 1024:
                raise ValueError('meter response is too long')
        line, _, rest = self.buffer.partition(b'\n')
        self.buffer = bytearray(rest)
        return line.decode('ascii').strip()

    def read(self):
        response = self.query('STAT?')
        if not response or any(ch not in '0123456789' for ch in response):
            raise ValueError('invalid status response: ' + repr(response))
        return decode_status(int(response))


class MeterDisplay(tk.Canvas):
    def __init__(self, parent):
        super().__init__(parent, background=BACKGROUND, highlightthickness=0,
                         width=250, height=78)
        self.characters = [" "] * 4
        self.points = [False] * 4
        self.indicators = dict.fromkeys(("AC", "DC", "Ω", "FREQ", "hFE", "diode", "V", "mV", "A", "mA",
                                         "kΩ", "MΩ", "kHz", "HOLD", "negative"), False)
        self.bind("<Configure>", self.redraw)

    def clear(self):
        self.characters = [" "] * 4
        self.points = [False] * 4
        self.indicators = dict.fromkeys(self.indicators, False)
        self.redraw()

    def set_reading(self, reading):
        led_bits = {"AC": 2, "DC": 3, "Ω": 15, "FREQ": 1, "hFE": 0,
                    "diode": 4, "V": 13, "mV": 10, "A": 12, "mA": 9,
                    "kΩ": 11, "MΩ": 14, "kHz": 1, "HOLD": 5}
        self.indicators = {name: bool(reading.leds & (1 << bit))
                           for name, bit in led_bits.items()}
        self.indicators["negative"] = reading.negative
        if reading.overrange:
            self.characters = list("OVER")
            self.points = [False] * 4
        else:
            digits = str(reading.magnitude).zfill(5)
            decimal_index = reading.decimal_position
            # Omit an extra leading zero only when the decimal remains visible.
            # Keep five numeric blocks for 0.xxxx and five-digit magnitudes.
            if digits[0] == "0" and decimal_index > 0:
                digits = digits[1:]
                decimal_index -= 1
            self.characters = list(digits)
            self.points = [index == decimal_index for index in range(len(digits))]
        self.redraw()

    def draw_diode(self, x, y, scale):
        icon_scale = 96 * scale / DIODE_WIDTH
        left = x - DIODE_WIDTH * icon_scale / 2
        top = y - DIODE_HEIGHT * icon_scale / 2
        color = ACTIVE if self.indicators["diode"] else INACTIVE
        for kind, points, stroke in DIODE_PARTS:
            coordinates = [value * icon_scale + (left if i % 2 == 0 else top)
                           for i, value in enumerate(points)]
            options = dict(fill=color, tags="indicator:diode")
            if kind == "line":
                self.create_line(coordinates, width=max(0.7, stroke * icon_scale),
                                 capstyle="round", joinstyle="round", **options)
            elif kind == "polygon":
                self.create_polygon(coordinates, outline=color,
                                    width=max(0.7, stroke * icon_scale), **options)
            else:
                self.create_oval(coordinates, outline=color,
                                 width=max(0.7, stroke * icon_scale), **options)

    def redraw(self, event=None):
        self.delete("all")
        width = self.winfo_width()
        height = self.winfo_height()
        extra_width = (len(self.characters) - 4) * 173
        design_width = 1353 + extra_width
        scale = min(width / design_width, height / 310)
        if scale <= 0:
            return
        left = (width - design_width * scale) / 2
        top = (height - 310 * scale) / 2
        self.create_text(left + 38 * scale, top + 25 * scale, anchor="w",
                         text="EZ DM-441B", fill="#ffff00",
                         font=("DejaVu Sans", max(8, int(13 * scale)), "bold"))
        glyph_scale = 182 / GLYPH_HEIGHT * scale
        sign = "-" if self.indicators["negative"] else " "
        characters = [sign] + self.characters
        points = [False] + self.points
        for index, (char, point) in enumerate(zip(characters, points)):
            tag = "indicator:negative" if index == 0 else f"display:{index-1}"
            x = left + (160 + index * 173) * scale
            y = top + 78 * scale
            active = CHARACTERS[char]
            for name, vertices in SEGMENTS.items():
                coordinates = [coordinate for px, py in vertices
                               for coordinate in (x + px * glyph_scale, y + py * glyph_scale)]
                self.create_polygon(coordinates, fill=ACTIVE if name in active else INACTIVE,
                                    outline="", tags=tag)
            cx = x + DECIMAL["cx"] * glyph_scale
            cy = y + DECIMAL["cy"] * glyph_scale
            rx = DECIMAL["rx"] * glyph_scale
            ry = DECIMAL["ry"] * glyph_scale
            self.create_oval(cx - rx, cy - ry, cx + rx, cy + ry,
                             fill=ACTIVE if point else INACTIVE, outline="", tags=tag)
        columns = ((62, 65, 40, ("AC", "DC", "Ω", "FREQ", "hFE", "diode")),
                   (1098 + extra_width, 90, 50, ("V", "mV", "A", "mA")),
                   (1253 + extra_width, 65, 50, ("Ω", "kΩ", "MΩ", "kHz", "HOLD")))
        for x, first_y, spacing, labels in columns:
            for index, label in enumerate(labels):
                cx = left + x * scale
                cy = top + (first_y + index * spacing) * scale
                if label == "diode":
                    self.draw_diode(cx, cy, scale)
                    continue
                tag = "indicator:" + label
                self.create_text(cx, cy, text=label, tags=tag,
                                 fill=ACTIVE if self.indicators[label] else INACTIVE,
                                 font=("DejaVu Sans", max(6, int(25 * scale)), "bold"))



class MeterConnection(threading.Thread):
    """Own socket I/O in a worker. Send data to Tk through a queue."""
    def __init__(self, host, generation, events):
        super().__init__(daemon=True)
        self.host = host
        self.generation = generation
        self.events = events
        self.stop = threading.Event()
        self.meter = None

    def cancel(self):
        self.stop.set()
        meter = self.meter
        if meter is not None:
            try:
                meter.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            meter.close()

    def send_event(self, kind, value):
        self.events.put((self.generation, kind, value))

    def run(self):
        meter = None
        try:
            meter = Meter(self.host, 5025, 3)
            self.meter = meter
            if self.stop.is_set():
                return
            identity = meter.query("*IDN?")
            if identity != "EZ DM-441B":
                raise ValueError("Unexpected device identity: " + repr(identity))
            self.send_event("connected", self.host)
            while not self.stop.is_set():
                self.send_event("reading", meter.read())
                if self.stop.wait(0.25):
                    break
        except (OSError, ValueError, UnicodeError) as error:
            if not self.stop.is_set():
                self.send_event("error", str(error))
        finally:
            if meter is not None:
                meter.close()
            self.meter = None


class MeterApp(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("EZ DM-441B")
        self.configure(background=BACKGROUND)
        self.geometry("512x145")
        self.minsize(480, 135)
        self.events = queue.Queue()
        self.generation = 0
        self.connection = None
        self.connected = False
        self.display = MeterDisplay(self)
        self.display.pack(fill="both", expand=True, padx=12, pady=(8, 0))
        controls = tk.Frame(self, background=BACKGROUND)
        controls.pack(fill="x", padx=18, pady=(8, 4))
        tk.Label(controls, text="IP address", foreground=INACTIVE, background=BACKGROUND,
                 font=("DejaVu Sans", 10)).pack(side="left", padx=(0, 10))
        self.address = tk.StringVar(value="192.168.0.190")
        self.entry = tk.Entry(controls, textvariable=self.address, width=16,
                              background=BACKGROUND, foreground=ACTIVE,
                              insertbackground=ACTIVE, disabledbackground=BACKGROUND,
                              disabledforeground=INACTIVE, highlightthickness=1,
                              highlightbackground=INACTIVE, highlightcolor=ACTIVE,
                              relief="flat", font=("DejaVu Sans Mono", 12))
        self.entry.pack(side="left", padx=(0, 10), ipady=5)
        self.entry.bind("<Return>", lambda event: self.toggle_connection())
        self.connect_button = tk.Button(controls, text="Connect", command=self.toggle_connection,
                                        width=11, background=BACKGROUND, foreground=ACTIVE,
                                        activebackground=INACTIVE, activeforeground=ACTIVE,
                                        highlightbackground=INACTIVE, relief="flat",
                                        borderwidth=0, padx=8, pady=7,
                                        font=("DejaVu Sans", 10))
        self.connect_button.pack(side="left")
        self.protocol("WM_DELETE_WINDOW", self.close)
        self.poll_id = self.after(50, self.process_events)

    def toggle_connection(self):
        if self.connection is not None:
            self.disconnect()
            return
        host = self.address.get().strip()
        try:
            ipaddress.IPv4Address(host)
        except ipaddress.AddressValueError:
            messagebox.showerror("Invalid IP address", "Enter a valid IPv4 address.", parent=self)
            return
        self.generation += 1
        self.connected = False
        self.display.clear()
        self.entry.configure(state="disabled")
        self.connect_button.configure(text="Disconnect")
        self.connection = MeterConnection(host, self.generation, self.events)
        self.connection.start()

    def disconnect(self):
        self.generation += 1
        connection = self.connection
        self.connection = None
        self.connected = False
        if connection is not None:
            connection.cancel()
        self.connect_button.configure(text="Connect")
        self.entry.configure(state="normal")
        self.display.clear()

    def process_events(self):
        try:
            while True:
                generation, kind, value = self.events.get_nowait()
                if generation != self.generation:
                    continue
                if kind == "connected":
                    self.connected = True
                elif kind == "reading":
                    self.display.set_reading(value)
                elif kind == "error":
                    was_connected = self.connected
                    self.disconnect()
                    prefix = "Connection lost" if was_connected else "Connection failed"
                    messagebox.showerror(prefix, value, parent=self)
        except queue.Empty:
            pass
        self.poll_id = self.after(50, self.process_events)

    def close(self):
        self.after_cancel(self.poll_id)
        self.disconnect()
        self.destroy()


if __name__ == "__main__":
    MeterApp().mainloop()
