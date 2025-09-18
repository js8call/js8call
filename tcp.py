from __future__ import print_function

from socket import socket, AF_INET, SOCK_STREAM

import json
import time

server = ('127.0.0.1', 2442)


def from_message(content):
    try:
        return json.loads(content)
    except ValueError:
        return {}


def to_message(typ, value='', params=None):
    if params is None:
        params = {}
    return json.dumps({'type': typ, 'value': value, 'params': params})


class Client(object):
    first = True
    def process(self, message):
        typ = message.get('type', '')
        value = message.get('value', '')
        params = message.get('params', {})
        if not typ:
            return

        if typ in ('RX.ACTIVITY',):
            # skip
            return

        print('->', typ)

        if value:
            print('-> value', value)

        if params:
            print('-> params: ', params)


    def send(self, *args, **kwargs):
        params = kwargs.get('params', {})
        if '_ID' not in params:
            params['_ID'] = '{}'.format(int(time.time()*1000))
            kwargs['params'] = params
        message = to_message(*args, **kwargs)
        print('outgoing message:', message)
        self.sock.send(message + '\n') # remember to send the newline at the end :)

    def connect(self):
        print('connecting to', ':'.join(map(str, server)))
        self.sock = socket(AF_INET, SOCK_STREAM)
        self.sock.connect(server)
        self.connected = True
        try:
            # send a simple example query after connected
            self.send("STATION.GET_STATUS")

            while self.connected:
                content = self.sock.recv(65500)
                if not content:
                    break
                print('incoming message')

                try:
                    message = json.loads(content)
                except ValueError:
                    message = {}

                if not message:
                    continue

                self.process(message)

        finally:
            self.sock.close()

    def close(self):
        self.connected = False



def main():
    s = Client()
    s.connect()

if __name__ == '__main__':
    main()
