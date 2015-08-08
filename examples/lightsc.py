#!/usr/bin/env python

import json
import pprint
import socket
import sys
import time
import uuid

from IPython.terminal.embed import InteractiveShellEmbed


def jsonrpc_call(socket, method, params):
    payload = {
        "method": method,
        "params": params,
        "jsonrpc": "2.0",
        "id": str(uuid.uuid4()),
    }
    socket.send(json.dumps(payload).encode("utf-8"))
    response = socket.recv(2048).decode("utf-8")
    try:
        response = json.loads(response)
    except ValueError:
        print("received invalid json: {}".format(response))
        return None
    return response


def set_light_from_hsbk(socket, target, h, s, b, k, t):
    return jsonrpc_call(socket, "set_light_from_hsbk", [
        target, h, s, b, k, t
    ])


def set_waveform(socket, target, waveform,
                 h, s, b, k,
                 period, cycles, skew_ratio, transient):
    return jsonrpc_call(socket, "set_waveform", [
        target, waveform, h, s, b, k, period, cycles, skew_ratio, transient
    ])


def saw(socket, target, h, s, b, k, period, cycles, transient=True):
    return set_waveform(
        socket, target, "SAW", h, s, b, k,
        cycles=cycles,
        period=period,
        skew_ratio=0.5,
        transient=transient
    )


def sine(socket, target, h, s, b, k, period, cycles, peak=0.5, transient=True):
    return set_waveform(
        socket, target, "SINE", h, s, b, k,
        cycles=cycles,
        period=period,
        skew_ratio=peak,
        transient=transient
    )


def power_on(socket, target):
    return jsonrpc_call(socket, "power_on", {"target": target})


def power_off(socket, target):
    return jsonrpc_call(socket, "power_off", {"target": target})


def get_light_state(socket, target):
    return jsonrpc_call(socket, "get_light_state", [target])

if __name__ == "__main__":
    s = socket.create_connection(("localhost", 1234))
    h = 0
    id = 0
    nb = "d073d501a0d5"
    fugu = "d073d500603b"
    neko = "d073d5018fb6"
    middle = "d073d502e530"
    target = "*"
    try:
        if len(sys.argv) == 2 and sys.argv[1] == "shell":
            ipshell = InteractiveShellEmbed()
            ipshell()
            sys.exit(0)
        power_on(s, target)
        while True:
            h = (h + 5) % 360
            id += 1
            set_light_from_hsbk(s, target, h, 0.8, 0.1, 2500, 450)
            time.sleep(0.5)
        power_off(s, target)
    finally:
        s.close()