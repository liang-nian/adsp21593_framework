"""Serial front end for the ADSP-21593 tune port.

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


class App:
    def __init__(self, root):
        self.root = root
        self.root.title("ADSP-21593 tune")
        self.root.geometry("980x640")
        self.ser = None
        self.reader = None
        self.stop = threading.Event()
        self.rxq = queue.Queue()
        self.parser = Parser()
        self.wb_on = False
        self.tx_buf = b""
        self.rx_buf = b""
        self.waiting = False
        self.wait_id = None
        self.view_id = None
        self.settle_id = None

        top = ttk.Frame(root, padding=8)
        top.grid(row=0, column=0, sticky="ew")
        root.columnconfigure(0, weight=1)
        root.rowconfigure(3, weight=1)

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
            ttk.Button(flags, text="CPU 负载", command=lambda: self.send_flag(CMD_CPU_MON)),
        ]
        for i, btn in enumerate(self.flag_btns):
            btn.grid(row=0, column=i, padx=(0 if i == 0 else 6, 0))

        wb = ttk.Frame(root, padding=(8, 0, 8, 4))
        wb.grid(row=2, column=0, sticky="ew")
        self.wb_btn = ttk.Button(wb, text="开启回写", command=self.toggle_writeback)
        self.wb_btn.grid(row=0, column=0, sticky="w")
        lamp_bg = ttk.Style().lookup("TFrame", "background") or root.cget("bg")
        self.lamp = tk.Canvas(
            wb, width=22, height=22, highlightthickness=0, bd=0, bg=lamp_bg
        )
        self.lamp_id = self.lamp.create_oval(3, 3, 19, 19, fill=LAMP_IDLE, outline=LAMP_IDLE)
        self.lamp.grid(row=0, column=1, padx=(8, 0))
        self.send_btn = ttk.Button(wb, text="发送", command=self.send_echo)
        self.send_btn.grid(row=0, column=2, padx=(8, 0))

        panes = ttk.Panedwindow(root, orient="horizontal")
        panes.grid(row=3, column=0, sticky="nsew", padx=8, pady=(0, 8))

        echo_box = ttk.Frame(panes)
        log_box = ttk.Frame(panes)
        panes.add(echo_box, weight=3)
        panes.add(log_box, weight=2)
        echo_box.rowconfigure(1, weight=1)
        echo_box.columnconfigure(0, weight=1)
        log_box.rowconfigure(1, weight=1)
        log_box.columnconfigure(0, weight=1)

        ttk.Label(echo_box, text="回显").grid(row=0, column=0, sticky="w")
        self.echo = tk.Text(
            echo_box, height=24, wrap="word", undo=False, font=("Consolas", 11)
        )
        self.echo.grid(row=1, column=0, sticky="nsew")
        self.echo.tag_configure("in_h", foreground="#1565c0")
        self.echo.tag_configure("out_h", foreground="#6a1b9a")
        self.echo.configure(state="disabled")
        self.echo.bind("<Control-Return>", lambda _event: self.send_echo())

        ttk.Label(log_box, text="日志").grid(row=0, column=0, sticky="w")
        self.devlog = tk.Text(log_box, height=24, wrap="word", undo=False)
        self.devlog.grid(row=1, column=0, sticky="nsew")
        self.devlog.configure(state="disabled")

        self.refresh_ports()
        self.root.after(50, self.drain)
        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

    def refresh_ports(self):
        names = [port.device for port in list_ports.comports()]
        self.port["values"] = names
        if names and self.port.get() not in names:
            self.port.set(names[0])

    def write_box(self, box, text, cap):
        box.configure(state="normal")
        box.insert("end", text)
        extra = int(box.index("end-1c").split(".")[0])
        if extra > 400:
            box.delete("1.0", "200.0")
        if cap and len(box.get("1.0", "end-1c")) > cap:
            box.delete("1.0", "end-%dc" % cap)
        box.see("end")
        box.configure(state="disabled")

    def note(self, text):
        self.write_box(self.devlog, text + "\n", LOG_CHAR_CAP)

    def devlog_line(self, text):
        self.write_box(self.devlog, text.rstrip("\r\n") + "\n", LOG_CHAR_CAP)

    def set_lamp(self, color):
        self.lamp.itemconfigure(self.lamp_id, fill=color, outline=color)

    def cancel_wait(self):
        self.waiting = False
        if self.wait_id is not None:
            self.root.after_cancel(self.wait_id)
            self.wait_id = None
        if self.view_id is not None:
            self.root.after_cancel(self.view_id)
            self.view_id = None
        if self.settle_id is not None:
            self.root.after_cancel(self.settle_id)
            self.settle_id = None

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
        self.cancel_wait()
        self.tx_buf = b""
        self.rx_buf = b""
        self.set_lamp(LAMP_IDLE)
        if self.wb_on:
            self.wb_on = False
            self.echo.configure(state="disabled")
            for btn in self.flag_btns:
                btn.configure(state="normal")
            self.wb_btn.configure(text="开启回写")
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
        rx = bytearray()
        while True:
            try:
                kind, data = self.rxq.get_nowait()
            except queue.Empty:
                break
            if kind == "err":
                self.note("读失败: %s" % data)
                self.close_port()
                continue
            if self.wb_on and self.waiting:
                rx.extend(data)
            elif self.wb_on:
                continue
            else:
                for frame in self.parser.feed(data):
                    if frame.cmd == CMD_LOG:
                        self.devlog_line(frame.text())
                    else:
                        body = frame.text().rstrip("\r\n")
                        self.note("<< %s  %s" % (frame.name, body))
        if rx:
            self.rx_buf += bytes(rx)
            self.schedule_view()
            self.judge()
        self.root.after(50, self.drain)

    def schedule_view(self):
        if self.view_id is not None:
            return
        self.view_id = self.root.after(80, self.flush_view)

    def flush_view(self):
        self.view_id = None
        if self.waiting or self.tx_buf:
            self.render_exchange()

    def render_exchange(self):
        sent = self.tx_buf.decode("utf-8", errors="replace")
        got = self.rx_buf.decode("utf-8", errors="replace")
        box = self.echo
        box.configure(state="normal")
        box.delete("1.0", "end")
        box.insert("end", "输入\n", "in_h")
        box.insert("end", sent)
        if sent and not sent.endswith("\n"):
            box.insert("end", "\n")
        box.insert("end", "输出\n", "out_h")
        box.insert("end", got)
        box.see("end")
        if self.waiting or not self.wb_on:
            box.configure(state="disabled")

    def judge(self):
        if not self.waiting:
            return
        n = min(len(self.rx_buf), len(self.tx_buf))
        if self.rx_buf[:n] != self.tx_buf[:n] or len(self.rx_buf) > len(self.tx_buf):
            self.finish(False)
            return
        if len(self.rx_buf) == len(self.tx_buf):
            self.arm_settle()

    def arm_settle(self):
        if self.settle_id is not None:
            self.root.after_cancel(self.settle_id)
        self.settle_id = self.root.after(200, self.on_settle)

    def on_settle(self):
        self.settle_id = None
        if self.waiting and self.rx_buf == self.tx_buf:
            self.finish(True)

    def finish(self, ok):
        self.cancel_wait()
        self.render_exchange()
        self.set_lamp(LAMP_OK if ok else LAMP_BAD)
        if self.wb_on:
            self.echo.configure(state="normal")
            self.echo.focus_set()

    def arm_timeout(self):
        try:
            baud = int(self.baud.get().strip(), 10)
        except ValueError:
            baud = 921600
        if baud < 1:
            baud = 921600
        seconds = max(2.0, (len(self.tx_buf) * 20.0 / baud) + 1.0)
        self.wait_id = self.root.after(int(seconds * 1000), self.on_timeout)

    def on_timeout(self):
        self.wait_id = None
        if self.waiting:
            self.finish(self.rx_buf == self.tx_buf)

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

    def send_flag(self, cmd):
        if self.wb_on:
            self.note("回写开着，监视命令不会被解析")
            return
        self.send(build_frame(cmd, None))

    def toggle_writeback(self):
        if self.ser is None:
            self.note("串口没打开")
            return
        if not self.send(build_frame(CMD_WB, None)):
            return
        self.wb_on = not self.wb_on
        self.parser = Parser()
        self.cancel_wait()
        self.tx_buf = b""
        self.rx_buf = b""
        self.set_lamp(LAMP_IDLE)
        for btn in self.flag_btns:
            btn.configure(state="disabled" if self.wb_on else "normal")
        self.wb_btn.configure(text="关闭回写" if self.wb_on else "开启回写")
        self.echo.configure(state="normal")
        self.echo.delete("1.0", "end")
        if self.wb_on:
            self.echo.focus_set()
        else:
            self.echo.configure(state="disabled")
            self.note("回写已关")

    def payload_text(self):
        text = self.echo.get("1.0", "end-1c")
        if text.startswith("输入\n"):
            rest = text[len("输入\n"):]
            mark = rest.find("\n输出\n")
            if mark >= 0:
                return rest[:mark]
            mark = rest.find("\n输出")
            if mark >= 0:
                return rest[:mark]
        return text

    def send_echo(self):
        if not self.wb_on or self.ser is None or self.waiting:
            return
        text = self.payload_text()
        if text == "":
            return
        raw = text.encode("utf-8")
        if not raw.endswith(b"\n"):
            raw += b"\n"
        try:
            self.ser.write(raw)
        except serial.SerialException as exc:
            self.note("写失败: %s" % exc)
            self.set_lamp(LAMP_BAD)
            return
        self.tx_buf = raw
        self.rx_buf = b""
        self.waiting = True
        self.set_lamp(LAMP_IDLE)
        self.render_exchange()
        self.echo.configure(state="disabled")
        self.arm_timeout()

    def on_close(self):
        self.cancel_wait()
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
