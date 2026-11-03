"""Thread-safe storage for discovered trace variables and their latest values."""

import threading


class TraceValues:
    """Thread-safe snapshot of the latest values, accessed by variable name."""

    def __init__(self):
        self._lock = threading.Lock()
        self._config = {}
        self._values = {}

    def _set_config(self, config):
        with self._lock:
            self._config = dict(config)
            self._values.clear()

    def _set(self, name, value):
        with self._lock:
            self._values[name] = value

    def get_variable_config(self):
        with self._lock:
            return list(self._config)

    def get_variable_ids(self):
        with self._lock:
            return [entry[0] for entry in self._config.values()]

    def get_id_from_name(self, name):
        with self._lock:
            entry = self._config.get(name)
            return entry[0] if entry else None

    def __getitem__(self, name):
        with self._lock:
            if name not in self._config:
                raise KeyError(name)
            return self._values.get(name)
