#!/usr/bin/env python3
"""Exercise the actual Makefile's soft-float rules in an isolated build tree."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
NAMES = ("comparesf2", "divsf3", "mulsf3", "fixunssfsi", "floatunsisf")


class SoftfloatMakeTest(unittest.TestCase):
    def test_dependency_files_are_not_compiled_as_sources(self):
        compiler = shutil.which(os.environ.get("HOST_CC", "cc"))
        self.assertIsNotNone(compiler, "a host C compiler is required")
        with tempfile.TemporaryDirectory(prefix="rocket-make-") as tmp:
            directory = Path(tmp)
            build = directory / "build"
            source = directory / "src"
            source.mkdir()
            ready = directory / ".ready"
            ready.touch()
            header = source / "value.h"
            header.write_text("#define VALUE 1\n")
            for name in NAMES:
                (source / (name + ".c")).write_text(
                    '#include "value.h"\nint ' + name + '(void) { return VALUE; }\n')
            objects = [build / "kernel/softfloat" / (name + ".o") for name in NAMES]
            command = ["make", "--no-print-directory", "-j2", "-f", str(ROOT / "Makefile"),
                       "BUILD=" + str(build), "KERNEL_CXX_READY=" + str(ready),
                       "KERNEL_CXX_BUILTINS=" + str(source), "CXX=" + compiler,
                       "TRIBE_RV_FLAGS=-MMD -MP", *map(str, objects)]
            env = dict(os.environ, MAKEFLAGS="", MFLAGS="", MAKEOVERRIDES="")

            def run():
                result = subprocess.run(command, cwd=ROOT, env=env,
                                        capture_output=True, text=True, timeout=30)
                output = result.stdout + result.stderr
                self.assertEqual(result.returncode, 0, output)
                # Failed optional makefile rebuilds can still return success.
                self.assertNotIn(".d.c", output)
                self.assertNotIn(".d.o", output)
                self.assertNotIn("error:", output)
                return [obj.stat().st_mtime_ns for obj in objects]

            initial = run()
            for obj in objects:
                self.assertTrue(obj.with_suffix(".d").is_file())
            self.assertEqual(run(), initial, "an unchanged build must not recompile")
            header.write_text("#define VALUE 2\n")
            rebuilt = run()
            self.assertTrue(all(new > old for new, old in zip(rebuilt, initial)),
                            "included dependency files must still track headers")
            self.assertEqual(run(), rebuilt)


if __name__ == "__main__":
    unittest.main()
