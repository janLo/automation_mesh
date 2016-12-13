import asyncio
import logging
from typing import Optional

import serial

from meshnet.serial.messages import SerialMessageConsumer, SerialMessage

logger = logging.getLogger(__name__)


class SerialBuffer(object):
    def __init__(self):
        self._buff = bytearray()

    def put(self, data):
        self._buff.append(data)

    def read(self, max_bytes):
        ret = self._buff[:max_bytes]
        self._buff = self._buff[max_bytes:]

        return bytes(ret)

    def available(self):
        return len(self._buff)


class AioSerial(asyncio.Protocol):
    def __init__(self):
        self._consumer = SerialMessageConsumer()
        self.transport = None
        self._buffer = SerialBuffer()

    def connection_made(self, transport):
        self.transport = transport
        logger.info('serial port opened: %s', transport)

    def data_received(self, data):
        logger.debug('data received', repr(data))
        self._buffer.put(data)
        while self._buffer.available() > 0:
            packet = self._consumer.consume(self._buffer, max_len=self._buffer.available())
            if packet is not None:
                self._on_packet(packet)

    def _on_packet(self, packet):
        # XXX call packet handlers here
        pass

    def connection_lost(self, exc):
        logger.warning("Serial port closed!")

    def pause_writing(self):
        logger.debug('pause writing, buffer=%d', self.transport.get_write_buffer_size())

    def resume_writing(self):
        logger.debug('resume writing, buffer=%d', self.transport.get_write_buffer_size())


class LegacyConnection(object):
    def __init__(self, device):
        self._device = device
        self._conn = None

    def connect(self):
        logger.info("Connect to %s", self._device)
        self._conn = serial.Serial(self._device, 115200, timeout=1)

    def read(self, consumer: SerialMessageConsumer) -> Optional[SerialMessage]:
        if self._conn.in_waiting == 0:
            return None
        return consumer.consume(self._conn, self._conn.in_waiting)

    def write(self, message: SerialMessage, key: bytes):
        out = message.framed(key)
        self._conn.write(out)
        self._conn.flush()