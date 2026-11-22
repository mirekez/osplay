"""Host wall-clock timings bounded by guest UART benchmark markers."""
import json
import re
import time


class BenchmarkTimer:
    def __init__(self, output, clock=time.monotonic):
        self.output = output
        self.clock = clock
        self.pending = b""
        self.active = {}

    def feed(self, chunk):
        self.pending += chunk
        while b"\n" in self.pending:
            line, self.pending = self.pending.split(b"\n", 1)
            line = line.rstrip(b"\r")
            begin = re.fullmatch(rb"ROCKET_BENCH_BEGIN ([\w-]+) input=(\d+) output=(\d+)", line)
            end = re.fullmatch(rb"ROCKET_BENCH_END ([\w-]+)", line)
            if begin:
                name = begin[1].decode()
                if name in self.active:
                    raise RuntimeError(f"duplicate benchmark start: {name}")
                self.active[name] = (self.clock(), int(begin[2]), int(begin[3]))
            elif end:
                name = end[1].decode()
                if name not in self.active:
                    raise RuntimeError(f"benchmark end without start: {name}")
                started, inputs, outputs = self.active.pop(name)
                elapsed = self.clock() - started
                result = dict(name=name, host_seconds=elapsed, input_tokens=inputs, output_tokens=outputs)
                if elapsed > 0:
                    result.update(host_input_tokens_per_second=inputs / elapsed,
                                  host_output_tokens_per_second=outputs / elapsed)
                self.output.write(json.dumps(result) + "\n")
                self.output.flush()
                print(f"Host simulation benchmark {name}: {elapsed:.3f}s, "
                      f"input={inputs} output={outputs} (UART marker interval)", flush=True)
        # Ordinary output without a newline must not grow memory indefinitely.
        if len(self.pending) > 65536:
            self.pending = self.pending[-65536:]

    def finish(self):
        if self.active:
            raise RuntimeError(f"incomplete benchmark intervals: {', '.join(self.active)}")
