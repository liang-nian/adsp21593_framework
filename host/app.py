"""Serial front end for the ADSP-21593 link port.

Run from the repo root, after the venv exists:

    .venv\\Scripts\\python host\\app.py
"""

import queue
import threading
import tkinter as tk
from tkinter import ttk

import serial
from serial.tools import list_ports

from protocol import (
    CMD_A2B_MON,
    CMD_CORE2_CPU_MON,
    CMD_CPU_MON,
    CMD_LOG,
    CMD_UART_MON,
    CMD_WB,
    Parser,
    build_frame,
)

DEFAULT_BAUD = "921600"
LOG_CHAR_CAP = 12000
LAMP_IDLE = "#9e9e9e"
LAMP_OK = "#2e7d32"
LAMP_BAD = "#c62828"
CRC_ON = "#f9a825"
ECHO_WAIT_MS = 8000


class App:
    def __init__(self, root):
        self.root = root
        self.root.title("ADSP-21593 link")
        self.root.geometry("980x640")
        self.ser = None
        self.reader = None
        self.stop = threading.Event()
        self.rxq = queue.Queue()
        self.parser = Parser()
        self.pending = None
        self.got = b""
        self.wait_id = None
        self.crc_test = False

        top = ttk.Frame(root, padding=8)
        top.grid(row=0, column=0, sticky="ew")
        root.columnconfigure(0, weight=1)
        root.rowconfigure(2, weight=1)

        ttk.Label(top, text="串口").grid(row=0, column=0, sticky="w")
        self.port = ttk.Combobox(top, width=28, state="readonly")
        self.port.grid(row=0, column=1, padx=4)
        ttk.Button(top, text="刷新", command=self.refresh_ports).grid(row=0, column=2)

        ttk.Label(top, text="波特率").grid(row=0, column=3, padx=(12, 0))
        self.baud = ttk.Entry(top, width=10)
        self.baud.insert(0, DEFAULT_BAUD)
        self.baud.grid(row=0, column=4, padx=4)
        self.connect_btn = ttk.Button(top, text="打开", command=self.toggle)
        self.connect_btn.grid(row=0, column=5)

        flags = ttk.Frame(root, padding=(8, 0, 8, 4))
        flags.grid(row=1, column=0, sticky="ew")
        self.flag_btns = [
            ttk.Button(flags, text="A2B 监视", command=lambda: self.send_flag(CMD_A2B_MON)),
            ttk.Button(flags, text="UART 监视", command=lambda: self.send_flag(CMD_UART_MON)),
            ttk.Button(flags, text="Core1 负载", command=lambda: self.send_flag(CMD_CPU_MON)),
            ttk.Button(flags, text="Core2 负载", command=lambda: self.send_flag(CMD_CORE2_CPU_MON)),
        ]
        for i, btn in enumerate(self.flag_btns):
            btn.grid(row=0, column=i, padx=(0 if i == 0 else 6, 0))
        self.crc_btn = tk.Button(
            flags, text="CRC 测试", command=self.toggle_crc_test, relief=tk.RAISED
        )
        self.crc_btn.grid(row=0, column=4, padx=(12, 0))

        panes = ttk.Panedwindow(root, orient="vertical")
        panes.grid(row=2, column=0, sticky="nsew", padx=8, pady=(0, 8))

        in_box = ttk.Frame(panes, padding=(0, 0, 0, 4))
        log_box = ttk.Frame(panes, padding=(0, 4, 0, 0))
        in_box.rowconfigure(1, weight=1)
        in_box.columnconfigure(0, weight=1)
        log_box.rowconfigure(1, weight=1)
        log_box.columnconfigure(0, weight=1)

        in_head = ttk.Frame(in_box)
        in_head.grid(row=0, column=0, sticky="ew")
        ttk.Label(in_head, text="输入").grid(row=0, column=0, sticky="w")
        ttk.Button(in_head, text="发送", command=self.send_plain).grid(
            row=0, column=1, padx=(12, 0)
        )
        ttk.Button(in_head, text="回显发送", command=self.send_echo).grid(
            row=0, column=2, padx=(6, 0)
        )
        lamp_bg = ttk.Style().lookup("TFrame", "background") or root.cget("bg")
        self.lamp = tk.Canvas(
            in_head, width=22, height=22, highlightthickness=0, bd=0, bg=lamp_bg
        )
        self.lamp_id = self.lamp.create_oval(3, 3, 19, 19, fill=LAMP_IDLE, outline=LAMP_IDLE)
        self.lamp.grid(row=0, column=3, padx=(8, 0))
        self.inp = tk.Text(
            in_box, height=6, wrap="word", undo=True, font=("Consolas", 11)
        )
        self.inp.grid(row=1, column=0, sticky="nsew")
        self.inp.bind("<Control-Return>", lambda _event: self.send_echo())

        ttk.Label(log_box, text="日志").grid(row=0, column=0, sticky="w")
        self.devlog = tk.Text(
            log_box, height=16, wrap="word", undo=False, font=("Consolas", 11)
        )
        self.devlog.grid(row=1, column=0, sticky="nsew")

        panes.add(in_box, weight=1)
        panes.add(log_box, weight=2)

        self.refresh_ports()
        self.root.after(50, self.drain)
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

    def refresh_ports(self):
        names = [port.device for port in list_ports.comports()]
        self.port["values"] = names
        if names and self.port.get() not in names:
            self.port.set(names[0])

    def write_box(self, text, cap):
        box = self.devlog
        box.insert("end", text)
        extra = int(box.index("end-1c").split(".")[0])
        if extra > 400:
            box.delete("1.0", "200.0")
        if cap and len(box.get("1.0", "end-1c")) > cap:
            box.delete("1.0", "end-%dc" % cap)
        box.see("end")

    def note(self, text):
        self.write_box(text + "\n", LOG_CHAR_CAP)

    def devlog_line(self, text):
        self.write_box(text.rstrip("\r\n") + "\n", LOG_CHAR_CAP)

    def set_lamp(self, color):
        self.lamp.itemconfigure(self.lamp_id, fill=color, outline=color)

    def toggle(self):
        if self.ser is not None:
            self.close_port()
            return
        name = self.port.get().strip()
        if not name:
            self.note("没有串口")
            return
        try:
            baud = int(self.baud.get().strip(), 10)
        except ValueError:
            self.note("波特率不是整数")
            return
        try:
            self.ser = serial.Serial(name, baud, timeout=0.05)
        except serial.SerialException as exc:
            self.ser = None
            self.note("打开失败: %s" % exc)
            return
        self.stop.clear()
        self.parser = Parser()
        self.reader = threading.Thread(target=self.read_loop, daemon=True)
        self.reader.start()
        self.connect_btn.configure(text="关闭")
        self.note("已打开 %s  %d" % (name, baud))

    def close_port(self):
        self.stop.set()
        ser = self.ser
        self.ser = None
        if ser is not None:
            try:
                ser.close()
            except serial.SerialException:
                pass
        self.connect_btn.configure(text="打开")
        self.set_lamp(LAMP_IDLE)
        self.pending = None
        self.got = b""
        if self.wait_id is not None:
            self.root.after_cancel(self.wait_id)
            self.wait_id = None
        self.note("已关闭")

    def read_loop(self):
        ser = self.ser
        while not self.stop.is_set() and ser is not None:
            try:
                data = ser.read(256)
            except serial.SerialException as exc:
                self.rxq.put(("err", str(exc)))
                break
            if data:
                self.rxq.put(("rx", data))

    def drain(self):
        while True:
            try:
                kind, data = self.rxq.get_nowait()
            except queue.Empty:
                break
            if kind == "err":
                self.note("读失败: %s" % data)
                self.close_port()
                continue
            for frame in self.parser.feed(data):
                if frame.cmd == CMD_WB and self.pending is not None:
                    self.got = frame.raw
                    self.write_box(frame.text(), 0)
                    self.finish_echo(self.got == self.pending)
                elif frame.cmd == CMD_LOG:
                    self.devlog_line(frame.text())
                else:
                    body = frame.text().rstrip("\r\n")
                    self.note("<< %s  %s" % (frame.name, body))
        self.root.after(50, self.drain)

    def finish_echo(self, ok):
        if self.wait_id is not None:
            self.root.after_cancel(self.wait_id)
            self.wait_id = None
        sent = len(self.pending) if self.pending is not None else 0
        got = len(self.got)
        self.pending = None
        self.got = b""
        self.set_lamp(LAMP_OK if ok else LAMP_BAD)
        if self.devlog.get("end-2c", "end-1c") not in ("", "\n"):
            self.write_box("\n", 0)
        if ok:
            self.note("回显一致")
        else:
            self.note("回显不一致 发出 %d 字节 收回 %d 字节" % (sent, got))

    def echo_timeout(self):
        self.wait_id = None
        if self.pending is None:
            return
        self.finish_echo(self.got == self.pending)

    def send(self, frame):
        if self.ser is None:
            self.note("串口没打开")
            return False
        try:
            self.ser.write(frame)
        except serial.SerialException as exc:
            self.note("写失败: %s" % exc)
            return False
        self.note(">> %s" % frame.hex(" "))
        return True

    def toggle_crc_test(self):
        self.crc_test = not self.crc_test
        if self.crc_test:
            self.crc_btn.configure(
                bg=CRC_ON, activebackground=CRC_ON, relief=tk.SUNKEN
            )
            self.note("CRC 测试开")
        else:
            self.crc_btn.configure(
                bg="SystemButtonFace", activebackground="SystemButtonFace", relief=tk.RAISED
            )
            self.note("CRC 测试关")

    def frame(self, cmd, payload):
        return build_frame(cmd, payload, self.crc_test)

    def send_flag(self, cmd):
        self.send(self.frame(cmd, None))

    def send_plain(self):
        return

    def send_echo(self):
        if self.ser is None:
            self.note("串口没打开")
            return
        text = self.inp.get("1.0", "end-1c")
        if text == "":
            return
        raw = text.encode("utf-8")
        frame = self.frame(CMD_WB, raw)
        self.pending = frame
        self.got = b""
        self.set_lamp(LAMP_IDLE)
        if self.wait_id is not None:
            self.root.after_cancel(self.wait_id)
        self.wait_id = self.root.after(ECHO_WAIT_MS, self.echo_timeout)
        try:
            self.ser.write(frame)
        except serial.SerialException as exc:
            self.note("写失败: %s" % exc)
            self.finish_echo(False)

    def on_close(self):
        self.stop.set()
        if self.ser is not None:
            try:
                self.ser.close()
            except serial.SerialException:
                pass
        self.root.destroy()


def main():
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()
