from __future__ import print_function

from socket import socket, AF_INET, SOCK_DGRAM

import json
import time

listen = ('127.0.0.1', 2242)


def from_message(content):
    try:
        return json.loads(content)
    except ValueError:
        return {}


def to_message(typ, value='', params=None):
    if params is None:
        params = {}
    return json.dumps({'type': typ, 'value': value, 'params': params})


class Server(object):
    first = True
    def process(self, message):
        typ = message.get('type', '')
        value = message.get('value', '')
        params = message.get('params', {})
        if not typ:
            return

        print('->', typ)

        if value:
            print('-> value', value)

        if params:
            print('-> params: ', params)

        if typ == 'PING':
            if self.first:
                self.send('MODE.SET_SPEED', '', params={'SPEED':3})
                self.first = False

        #### if typ == 'PING':
        ####     self.send('STATION.GET_GRID')
        ####     self.send('RIG.GET_FREQ')
        ####     self.send('STATION.GET_CALLSIGN')
        ####     self.send('RX.GET_CALL_ACTIVITY')

        #### elif typ == 'STATION.GRID':
        ####     if value != 'EM73TU49TQ':
        ####         self.send('STATION.SET_GRID', 'EM73TU49TQ')

        #### elif typ == 'RIG.FREQ':
        ####     if params.get('DIAL', 0) != 14064000:
        ####         self.send('RIG.SET_FREQ', '', {"DIAL": 14064000, "OFFSET": 977})
        ####         self.send('TX.SEND_MESSAGE', 'HELLO WORLD')

        elif typ == 'CLOSE':
            self.close()

    def send(self, *args, **kwargs):
        params = kwargs.get('params', {})
        if '_ID' not in params:
            params['_ID'] = int(time.time()*1000)
            kwargs['params'] = params
        message = to_message(*args, **kwargs)
        print('outgoing message:', message)
        self.sock.sendto(message, self.reply_to)

    def listen(self):
        print('listening on', ':'.join(map(str, listen)))
        self.sock = socket(AF_INET, SOCK_DGRAM)
        self.sock.bind(listen)
        self.listening = True
        try:
            while self.listening:
                content, addr = self.sock.recvfrom(65500)
                print('incoming message:', ':'.join(map(str, addr)))

                try:
                    message = json.loads(content)
                except ValueError:
                    message = {}

                if not message:
                    continue

                self.reply_to = addr
                self.process(message)

        finally:
            self.sock.close()

    def close(self):
        self.listening = False



def main():
    s = Server()
    s.listen()

if __name__ == '__main__':
    main()
