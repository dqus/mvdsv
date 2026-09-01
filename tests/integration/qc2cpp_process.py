"""Small, bounded helpers for qc2cpp process acceptance tests."""

import json
import os
import select
import subprocess
import time


class ProcessFailure(RuntimeError):
    """The observed server process did not follow the required lifecycle."""


def parse_json_observation(line, key):
    """Decode a JSON observation after MVDSV's timestamp prefix."""
    start = line.find("{")
    if start < 0:
        raise ProcessFailure(f"missing JSON observation: {key}")
    try:
        payload = json.loads(line[start:])
    except json.JSONDecodeError as error:
        raise ProcessFailure(f"invalid JSON observation: {key}") from error
    if key not in payload or not isinstance(payload[key], dict):
        raise ProcessFailure(f"missing JSON observation: {key}")
    return payload[key]


class RunningProcess:
    """A line-oriented server process with bounded command observations."""

    def __init__(self, command, output_path):
        self._output = output_path.open("w", encoding="utf-8")
        self._process = subprocess.Popen(command, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self._buffer = b""

    def observe(self, command, key, *, timeout):
        if self._process.stdin is None or self._process.stdout is None:
            raise ProcessFailure("server process has no console I/O")
        self.send(command)
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProcessFailure(f"timed out waiting for observation: {key}")
            line = self._read_line(remaining, key)
            self._record_output(line)
            if key in line:
                return parse_json_observation(line, key)

    def observe_until(self, command, key, predicate, *, timeout):
        """Poll a server-owned observation until its real state satisfies *predicate*."""
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise ProcessFailure(f"timed out waiting for matching observation: {key}")
            observation = self.observe(command, key, timeout=remaining)
            if predicate(observation):
                return observation

    def _record_output(self, text):
        self._output.write(text)
        self._output.flush()

    def _drain_available_output(self):
        if self._process.stdout is None:
            return
        while True:
            readable, _, _ = select.select([self._process.stdout], [], [], 0)
            if not readable:
                break
            chunk = os.read(self._process.stdout.fileno(), 4096)
            if not chunk:
                break
            self._buffer += chunk
        while b"\n" in self._buffer:
            line, self._buffer = self._buffer.split(b"\n", 1)
            self._record_output(line.decode("utf-8", errors="replace") + "\n")
        if self._process.poll() is not None and self._buffer:
            self._record_output(self._buffer.decode("utf-8", errors="replace"))
            self._buffer = b""

    def _read_line(self, timeout, key):
        if self._process.stdout is None:
            raise ProcessFailure("server process has no console output")
        while b"\n" not in self._buffer:
            readable, _, _ = select.select([self._process.stdout], [], [], timeout)
            if not readable:
                raise ProcessFailure(f"timed out waiting for observation: {key}")
            chunk = os.read(self._process.stdout.fileno(), 4096)
            if not chunk:
                code = self._process.poll()
                raise ProcessFailure(f"server early exit while waiting for {key}: {code}")
            self._buffer += chunk
        line, self._buffer = self._buffer.split(b"\n", 1)
        return line.decode("utf-8", errors="replace") + "\n"

    def send(self, command):
        if self._process.stdin is None:
            raise ProcessFailure("server process has no console input")
        try:
            self._process.stdin.write((command + "\n").encode("utf-8"))
            self._process.stdin.flush()
        except BrokenPipeError as error:
            self._drain_available_output()
            raise ProcessFailure(
                f"server early exit while sending {command!r}: {self._process.poll()}"
            ) from error

    def wait_for_exit(self, *, timeout):
        try:
            returncode = self._process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            raise ProcessFailure(f"server did not exit within {timeout}s") from error
        self._drain_available_output()
        return returncode

    def close(self):
        try:
            if self._process.poll() is None and self._process.stdin is not None:
                try:
                    self._process.stdin.write(b"quit\n")
                    self._process.stdin.flush()
                except BrokenPipeError:
                    pass
            self._process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self._process.terminate()
            try:
                self._process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                self._process.kill()
                self._process.wait()
        finally:
            self._drain_available_output()
            if self._process.stdin is not None:
                try:
                    self._process.stdin.close()
                except BrokenPipeError:
                    pass
            if self._process.stdout is not None:
                self._process.stdout.close()
            self._output.close()


def run_process(command, output_path, *, timeout):
    """Run one command, always reap it, and preserve its combined output."""
    with output_path.open("w", encoding="utf-8") as output:
        process = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT,
            text=True)
        try:
            returncode = process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as error:
            process.terminate()
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
            raise ProcessFailure(f"process timed out after {timeout}s") from error
    if returncode != 0:
        raise ProcessFailure(f"process early exit with return code {returncode}")


def run_client_acceptance(command, output_path, *, timeout):
    """Require FTE's enabled post-parse observations after a clean exit."""
    run_process(command, output_path, timeout=timeout)
    wait_for_events(output_path.read_text(encoding="utf-8").splitlines(), [
        "[qc2cpp-acceptance] headless-renderer",
        "[qc2cpp-acceptance] parsed-qw-server",
        "[qc2cpp-acceptance] received-qw-frame",
    ])


def run_client_network_acceptance(command, output_path, *, timeout):
    """Require one real client to complete the bounded gameplay progression."""
    run_process(command, output_path, timeout=timeout)
    wait_for_events(output_path.read_text(encoding="utf-8").splitlines(), [
        "[qc2cpp-network] active",
        "[qc2cpp-network] forward",
        "[qc2cpp-network] kill",
        "[qc2cpp-network] disconnect",
    ])


def wait_for_events(events, expected):
    """Require *expected* to occur in order in an already captured event stream."""
    event_index = 0
    for expected_event in expected:
        try:
            found_at = events.index(expected_event, event_index)
        except ValueError as error:
            if expected_event in events:
                raise ProcessFailure(
                    f"event order violation: expected {expected_event!r} after "
                    f"{events[event_index - 1]!r}"
                ) from error
            raise ProcessFailure(f"missing event: {expected_event!r}") from error
        event_index = found_at + 1
