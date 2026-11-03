from importlib.metadata import PackageNotFoundError, version

try:
    __version__ = version("TracerLL")
except PackageNotFoundError:
    __version__ = "0.0.0"

try:
    from . import Tracer
except ImportError:
    Tracer = None

if Tracer is not None:
    from .EmbeddedTracer import EmbeddedTracer, TraceValues
from .TraceMessageType import TraceMessageType
from .TraceValueType import TraceValueType
