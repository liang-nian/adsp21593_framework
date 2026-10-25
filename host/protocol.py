"""Tune UART frames used by sharc1/src/algo/tune/tune.c.

Host request:
  5A A5 | cmd | [len] | [payload] | crc_lo | crc_hi
CRC is Modbus CRC16, init 0xFFFF, and covers cmd, the length byte, and the payload.
0x01, 0x02, 0x03 and 0x04 have no length byte.
0x01 turns device echo on. The same frame turns it off.
While echo is on, typed bytes are raw UART, not this frame.
Device text (0x02, 0x03, 0x04, 0x0C) is sent back with a length byte.
"""

CMD_WB = 0x01
CMD_A2B_MON = 0x02
CMD_UART_MON = 0x03
CMD_CPU_MON = 0x04
CMD_LOG = 0x0C

HEADER = b"\x5A\xA5"
MAX_PAYLOAD = 255

CMD_NAME = {
    CMD_WB: "writeback",
    CMD_A2B_MON: "a2b mon",
    CMD_UART_MON: "uart mon",
    CMD_CPU_MON: "cpu load",
    CMD_LOG: "log",
}

# Device-to-host frames that carry a 1-byte length.
_RX_WITH_LEN = {CMD_A2B_MON, CMD_UART_MON, CMD_CPU_MON, CMD_LOG}


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 1:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
            crc &= 0xFFFF
    return crc


def build_frame(cmd: int, payload: bytes | None) -> bytes:
    """payload is None when the command has no length byte."""
    body = bytes([cmd & 0xFF])
    if payload is not None:
        if len(payload) > MAX_PAYLOAD:
            raise ValueError("payload longer than 255")
        body += bytes([len(payload)]) + payload
    crc = crc16_modbus(body)
    return HEADER + body + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


class Frame:
    def __init__(self, cmd: int, payload: bytes, ok: bool):
        self.cmd = cmd
        self.payload = payload
        self.ok = ok

    @property
    def name(self) -> str:
        return CMD_NAME.get(self.cmd, "cmd 0x%02X" % self.cmd)

    def text(self) -> str:
        return self.payload.decode("utf-8", errors="replace")


class Parser:
    def __init__(self):
        self.buf = b""

    def feed(self, data: bytes) -> list:
        self.buf += data
        frames = []
        while True:
            start = self.buf.find(HEADER)
            if start < 0:
                self.buf = self.buf[-1:] if self.buf.endswith(b"\x5A") else b""
                break
            if start > 0:
                self.buf = self.buf[start:]
            frame = self._try_one()
            if frame is None:
                break
            if frame is False:
                self.buf = self.buf[1:]
                continue
            frames.append(frame)
        return frames

    def _try_one(self):
        if len(self.buf) < 5:
            return None
        cmd = self.buf[2]
        if cmd not in _RX_WITH_LEN:
            return False
        if len(self.buf) < 4:
            return None
        length = self.buf[3]
        need = 6 + length
        if len(self.buf) < need:
            return None
        body = self.buf[2:4 + length]
        crc_rx = self.buf[4 + length] | (self.buf[5 + length] << 8)
        if crc16_modbus(body) != crc_rx:
            return False
        payload = self.buf[4:4 + length]
        self.buf = self.buf[need:]
        return Frame(cmd, payload, True)
