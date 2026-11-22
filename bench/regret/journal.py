"""Resume completed measurements from an exclusive, per-case JSONL journal."""

from contextlib import contextmanager
import fcntl
import json
import os


class Journal:
    def __init__(self, file, config):
        self.file = file
        self.rows = {}
        file.seek(0)
        while True:
            start = file.tell()
            line = file.readline()
            if not line:
                break
            if not line.endswith(b"\n"):
                # An interrupted append cannot represent a completed trial.
                file.truncate(start)
                break
            row = json.loads(line)
            key = row["key"]
            if key in self.rows:
                raise ValueError(f"duplicate journal key {key!r} in {file.name}")
            self.rows[key] = row
        if self.rows:
            if self.rows.get("config") != {"key": "config", **config}:
                raise ValueError(f"benchmark inputs or settings changed for {file.name}; use a new results directory")
        else:
            self.append("config", config)

    def append(self, key, row):
        row = {"key": key, **row}
        self.file.write((json.dumps(row, allow_nan=False) + "\n").encode("ascii"))
        self.file.flush()
        os.fsync(self.file.fileno())
        self.rows[key] = row
        return row

    def measure(self, key, run):
        if key in self.rows:
            return self.rows[key]
        return self.append(key, run())


@contextmanager
def open_journal(path, config):
    with path.open("a+b") as file:
        fcntl.flock(file.fileno(), fcntl.LOCK_EX)
        yield Journal(file, config)
