# Turn TracerLL into a package and attach submodules

try:
    from . import SevenBitEncoding
except ImportError:
    SevenBitEncoding = None

try:
    from . import Tracer
except ImportError:
    Tracer = None
