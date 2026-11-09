#!/usr/bin/env python3
"""Publish the current 3x3 map over stdout when Hyprland state changes."""

import json
import os
import socket
import sys
from pathlib import Path

EVENTS = {
    b"workspace",
    b"workspacev2",
    b"focusedmon",
    b"focusedmonv2",
    b"monitoradded",
    b"monitoraddedv2",
    b"monitorremoved",
    b"openwindow",
    b"closewindow",
    b"movewindow",
    b"movewindowv2",
    b"moveworkspace",
    b"moveworkspacev2",
    b"createworkspace",
    b"createworkspacev2",
    b"destroyworkspace",
    b"destroyworkspacev2",
    b"activespecial",
    b"activespecialv2",
}


def request(directory, command):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(2)
        connection.connect(str(directory / ".socket.sock"))
        connection.sendall(f"j/{command}".encode())
        chunks = []
        while chunk := connection.recv(65536):
            chunks.append(chunk)
    return json.loads(b"".join(chunks))


def cell_number(workspace):
    if not isinstance(workspace, dict) or workspace.get("type") not in (None, "normal"):
        return None
    number = workspace.get("id")
    if type(number) is not int or not 1 <= number <= 9:
        return None
    if workspace.get("type") == "normal":
        if workspace.get("address") != str(number):
            return None
    elif workspace.get("name", str(number)) != str(number):
        return None
    return number


def snapshot(directory):
    monitors = request(directory, "monitors")
    clients = request(directory, "clients")
    counts = [0] * 9
    for client in clients:
        number = cell_number(client.get("workspace"))
        if client.get("mapped", True) and number is not None:
            counts[number - 1] += 1
    outputs = {
        monitor["name"]: cell_number(monitor.get("activeWorkspace"))
        for monitor in monitors
    }
    focused = next(
        (outputs[monitor["name"]] for monitor in monitors if monitor["focused"]), None
    )
    return {"counts": counts, "outputs": outputs, "focused": focused}


def main():
    signature = os.environ.get("HYPRLAND_INSTANCE_SIGNATURE")
    if not signature:
        raise ValueError("HyprToroid minimap requires a Hyprland session")
    directory = Path(os.environ["XDG_RUNTIME_DIR"]) / "hypr" / signature
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as events:
        events.settimeout(2)
        events.connect(str(directory / ".socket2.sock"))
        events.settimeout(None)
        previous = None

        def publish():
            nonlocal previous
            encoded = json.dumps(snapshot(directory), separators=(",", ":"))
            if encoded != previous:
                print(encoded, flush=True)
                previous = encoded

        # Subscribe first, so changes during the initial snapshot are not lost.
        publish()
        pending = b""
        while data := events.recv(65536):
            lines = (pending + data).split(b"\n")
            pending = lines.pop()
            if any(line.partition(b">>")[0] in EVENTS for line in lines):
                publish()
    raise ConnectionError("Hyprland event connection closed")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError) as error:
        print(json.dumps({"error": str(error)}), flush=True)
        sys.exit(1)
