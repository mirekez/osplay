import io
import json
import unittest

from benchmark_time import BenchmarkTimer


class BenchmarkTimeTests(unittest.TestCase):
    def test_split_uart_markers_and_rates(self):
        output = io.StringIO()
        ticks = iter((12.0, 20.0, 25.0, 29.0))
        timer = BenchmarkTimer(output, lambda: next(ticks))
        timer.feed(b"ordinary UART\nROCKET_BENCH_BE")
        timer.feed(b"GIN llama-step-1 input=128 output=1\r\nLlama layer 1 begin\n")
        timer.feed(b"ROCKET_BENCH_END llama-step-1\n")
        timer.feed(b"ROCKET_BENCH_BEGIN llama-step-2 input=129 output=1\n")
        timer.feed(b"ROCKET_BENCH_END llama-step-2\n")
        timer.finish()
        first, second = map(json.loads, output.getvalue().splitlines())
        self.assertEqual(first["host_seconds"], 8.0)
        self.assertEqual(first["host_input_tokens_per_second"], 16.0)
        self.assertEqual(first["host_output_tokens_per_second"], 0.125)
        self.assertEqual(second["host_seconds"], 4.0)

    def test_incomplete_and_invalid_intervals(self):
        timer = BenchmarkTimer(io.StringIO(), lambda: 1.0)
        with self.assertRaisesRegex(RuntimeError, "without start"):
            timer.feed(b"ROCKET_BENCH_END unknown\n")
        timer.feed(b"ROCKET_BENCH_BEGIN test input=128 output=1\n")
        with self.assertRaisesRegex(RuntimeError, "duplicate"):
            timer.feed(b"ROCKET_BENCH_BEGIN test input=128 output=1\n")
        with self.assertRaisesRegex(RuntimeError, "incomplete"):
            timer.finish()


if __name__ == "__main__":
    unittest.main()
