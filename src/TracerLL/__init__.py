from importlib.metadata import PackageNotFoundError, version

try:
    __version__ = version("TracerLL")
except PackageNotFoundError:
    __version__ = "0.0.0"

try:
    from . import Tracer
except ImportError:
    Tracer = None
