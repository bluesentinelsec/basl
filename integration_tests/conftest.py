import os
import shutil
import tempfile
import uuid
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
LOCAL_TMP_ROOT = Path(os.environ.get("VIGIL_TEST_TMPDIR", REPO_ROOT / ".tmp" / "pytest"))

LOCAL_TMP_ROOT.mkdir(parents=True, exist_ok=True)

os.environ["VIGIL_TEST_TMPDIR"] = str(LOCAL_TMP_ROOT)
os.environ["TMPDIR"] = str(LOCAL_TMP_ROOT)
os.environ["TEMP"] = str(LOCAL_TMP_ROOT)
os.environ["TMP"] = str(LOCAL_TMP_ROOT)
tempfile.tempdir = str(LOCAL_TMP_ROOT)


class LocalTemporaryDirectory:
    def __init__(self, suffix=None, prefix=None, dir=None, ignore_cleanup_errors=False):
        self.name = local_mkdtemp(suffix=suffix, prefix=prefix, dir=dir)
        self._ignore_cleanup_errors = ignore_cleanup_errors

    def __enter__(self):
        return self.name

    def __exit__(self, exc_type, exc, tb):
        shutil.rmtree(self.name, ignore_errors=self._ignore_cleanup_errors)
        return False

    def cleanup(self):
        shutil.rmtree(self.name, ignore_errors=self._ignore_cleanup_errors)


def local_mkdtemp(suffix=None, prefix=None, dir=None):
    base = Path(dir or LOCAL_TMP_ROOT)
    base.mkdir(parents=True, exist_ok=True)
    name = f"{prefix or ''}{uuid.uuid4().hex}{suffix or ''}"
    path = base / name
    path.mkdir()
    return str(path)


tempfile.TemporaryDirectory = LocalTemporaryDirectory
tempfile.mkdtemp = local_mkdtemp
