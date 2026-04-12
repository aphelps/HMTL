"""Output state classes mirroring the C++ config_*_t structs."""


class ValueOutput:
    TYPE = "value"

    def __init__(self, config):
        self.pin = config.get("pin", 0)
        self.value = config.get("value", 0)

    def set_value(self, v):
        self.value = int(v) & 0x1FFF  # 13-bit

    def set_rgb(self, r, g, b):
        self.value = int(r)  # mirrors hmtl_set_output_rgb VALUE case

    def reset(self):
        self.value = 0

    def state(self):
        return {"type": self.TYPE, "value": self.value}


class RGBOutput:
    TYPE = "rgb"

    def __init__(self, config):
        self.pins = list(config.get("pins", [0, 0, 0]))
        self.values = list(config.get("values", [0, 0, 0]))

    def set_value(self, v):
        self.values = [int(v), int(v), int(v)]

    def set_rgb(self, r, g, b):
        self.values = [int(r), int(g), int(b)]

    def reset(self):
        self.values = [0, 0, 0]

    def state(self):
        return {"type": self.TYPE, "values": list(self.values)}


class PixelOutput:
    TYPE = "pixels"

    def __init__(self, config):
        self.num_pixels = config.get("numpixels", 0)
        self.pixels = [(0, 0, 0)] * self.num_pixels

    def set_value(self, v):
        self.pixels = [(int(v), int(v), int(v))] * self.num_pixels

    def set_rgb(self, r, g, b):
        self.pixels = [(int(r), int(g), int(b))] * self.num_pixels

    def reset(self):
        self.pixels = [(0, 0, 0)] * self.num_pixels

    def state(self):
        return {"type": self.TYPE, "pixels": list(self.pixels)}


class RS485Output:
    TYPE = "rs485"

    def __init__(self, config):
        pass

    def set_value(self, v):
        pass

    def set_rgb(self, r, g, b):
        pass

    def reset(self):
        pass

    def state(self):
        return {"type": self.TYPE}


class MPR121Output:
    TYPE = "mpr121"

    def __init__(self, config):
        self.touched = [False] * 12

    def set_value(self, v):
        pass

    def set_rgb(self, r, g, b):
        pass

    def reset(self):
        self.touched = [False] * 12

    def state(self):
        return {"type": self.TYPE, "touched": list(self.touched)}


def make_output(config):
    """Create an Output object from a JSON config dict."""
    t = config.get("type")
    if t == "value":
        return ValueOutput(config)
    elif t == "rgb":
        return RGBOutput(config)
    elif t == "pixels":
        return PixelOutput(config)
    elif t == "rs485":
        return RS485Output(config)
    elif t == "mpr121":
        return MPR121Output(config)
    else:
        raise ValueError("Unknown output type: %s" % t)
