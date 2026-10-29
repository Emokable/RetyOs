"""Exercise the launcher's actual cleanup insertion against runtime fixtures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASH = shutil.which('bash') or r'C:\Program Files\Git\bin\bash.exe'


@unittest.skipUnless(Path(BASH).is_file(), 'Git Bash is required')
class CleanupHookTest(unittest.TestCase):
    def test_indentation_and_missing_anchor(self):
        launcher = (ROOT / 'device/kindle-android/start.sh').read_text(encoding='utf-8')
        begin = launcher.index("    sed '/^[[:space:]]*rmdir")
        end = launcher.index('\nfi\nif [ "$system_cache"', begin)
        block = launcher[begin:end]
        runner = (ROOT / 'runtime/run.sh').read_text(encoding='utf-8')
        anchor = ' rmdir "$stage"/rootfs/system '
        self.assertEqual(runner.count(anchor), 1)
        build = ROOT / 'build'
        build.mkdir(exist_ok=True)
        for indent in (' ', '  ', '\t', None):
            with self.subTest(indent=indent), tempfile.TemporaryDirectory(dir=build) as work:
                stage = Path(work) / 'stage'
                stage.mkdir()
                replacement = (indent + 'rmdir "$stage"/rootfs/system '
                               if indent is not None else ' rmdir "$stage"/rootfs/renamed ')
                (stage / 'run.sh').write_bytes(runner.replace(anchor, replacement).encode())
                result = subprocess.run([BASH, '-s'],
                                        input=('stage="$PWD/stage"\n' + block + '\n').encode(),
                                        cwd=work, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
                self.assertEqual(result.returncode, 8 if indent is None else 0,
                                 result.stdout.decode(errors='replace'))
                if indent is not None:
                    output = (stage / 'run.sh').read_text(encoding='utf-8')
                    self.assertEqual(output.count('then . "$stage/overlay-cleanup.sh"; fi'), 1)


if __name__ == '__main__':
    unittest.main()
