"""Program logic mirroring the C++ HMTLPrograms state machines.

Each program has:
  tick(now_ms, output_or_outputs) -> changed: bool

Single-output programs receive one output object; SequenceProgram
receives the full list of outputs.
"""


class BlinkProgram:
    """Blink between two colors at specified periods (mirrors program_blink_t)."""

    def __init__(self, on_period, on_value, off_period, off_value):
        self.on_period = on_period
        self.on_value = on_value      # (r, g, b)
        self.off_period = off_period
        self.off_value = off_value    # (r, g, b)
        self._initialized = False
        self._on = True
        self._next_change = 0

    def tick(self, now_ms, output):
        if not self._initialized:
            self._initialized = True
            self._on = True
            self._next_change = now_ms + self.on_period
            output.set_rgb(*self.on_value)
            return True
        if now_ms < self._next_change:
            return False
        if self._on:
            output.set_rgb(*self.off_value)
            self._next_change += self.off_period
        else:
            output.set_rgb(*self.on_value)
            self._next_change += self.on_period
        self._on = not self._on
        return True


class TimedChangeProgram:
    """Set start color, wait change_period, then set stop color (mirrors program_timed_change_t)."""

    def __init__(self, change_period, start_value, stop_value):
        self.change_period = change_period
        self.start_value = start_value  # (r, g, b)
        self.stop_value = stop_value    # (r, g, b)
        self._initialized = False
        self._change_time = 0

    def tick(self, now_ms, output):
        if not self._initialized:
            self._initialized = True
            self._change_time = now_ms + self.change_period
            output.set_rgb(*self.start_value)
            return True
        if now_ms < self._change_time:
            return False
        output.set_rgb(*self.stop_value)
        self._change_time = now_ms + self.change_period  # repeat
        return True


class FadeProgram:
    """Interpolate between two colors over a period (mirrors program_fade_t)."""

    FLAG_CYCLE = 0x1

    def __init__(self, period, start_value, stop_value, flags=0):
        self.period = period
        self.start_value = start_value  # (r, g, b)
        self.stop_value = stop_value    # (r, g, b)
        self.flags = flags
        self._start_time = None

    def tick(self, now_ms, output):
        if self._start_time is None:
            self._start_time = now_ms
        elapsed = now_ms - self._start_time
        if self.period <= 0:
            fraction = 255
        else:
            fraction = min(255, int(elapsed * 255 / self.period))

        if self.flags & self.FLAG_CYCLE:
            # Reverse at end
            if elapsed >= self.period * 2:
                self._start_time = now_ms
                elapsed = 0
                fraction = 0
            elif elapsed >= self.period:
                fraction = 255 - min(255, int((elapsed - self.period) * 255 / self.period))

        r = (self.stop_value[0] * fraction + self.start_value[0] * (255 - fraction)) // 255
        g = (self.stop_value[1] * fraction + self.start_value[1] * (255 - fraction)) // 255
        b = (self.stop_value[2] * fraction + self.start_value[2] * (255 - fraction)) // 255
        output.set_rgb(r, g, b)
        return True


class SequenceProgram:
    """Cycle through (output_index, duration_ms, value) steps.

    Mirrors program_sequence_t — manages multiple outputs internally.
    tick() receives the full outputs list.
    """

    def __init__(self, steps):
        """steps: list of (output_index, duration_ms, value) tuples."""
        if not steps:
            raise ValueError("SequenceProgram requires at least one step")
        self.steps = steps
        self._current = 0
        self._next_change = None

    def tick(self, now_ms, outputs):
        if self._next_change is None:
            # First tick: start step 0
            out_idx, duration, value = self.steps[0]
            outputs[out_idx].set_rgb(value, value, value)
            self._next_change = now_ms + duration
            return True
        if now_ms < self._next_change:
            return False
        # Turn off current step's output
        prev_idx, _, _ = self.steps[self._current]
        outputs[prev_idx].set_rgb(0, 0, 0)
        # Advance to next step
        self._current = (self._current + 1) % len(self.steps)
        out_idx, duration, value = self.steps[self._current]
        outputs[out_idx].set_rgb(value, value, value)
        self._next_change = now_ms + duration
        return True


class NoneProgram:
    """Sentinel used to cancel a running program."""
    pass
