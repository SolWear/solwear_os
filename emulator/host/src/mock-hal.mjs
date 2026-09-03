/**
 * The mock hardware abstraction layer.
 *
 * This mirrors `MockHal` in `solweard`: every HAL call has to work here, and
 * the values have to be deterministic so a test can assert on them. Anything
 * that would drift on real hardware is a pure function of the elapsed time and
 * a seed taken from the device profile.
 */

export class MockHal {
  /**
   * @param {object} profile the device profile JSON
   * @param {object} [overrides] scripted values, from a --mock file
   */
  constructor(profile, overrides = {}) {
    this.profile = profile;
    this.script = { ...(profile.mock ?? {}), ...overrides };
    this.startedAt = Date.now();
    this.brightness = this.script.brightness ?? 80;
    this.seed = this.script.seed ?? 0x5ec0ffee;
  }

  /** Minutes since the emulator started; the basis for everything that drifts. */
  elapsedMinutes() {
    return (Date.now() - this.startedAt) / 60000;
  }

  /**
   * A small deterministic generator. Given the same bucket it always returns
   * the same number, so a sensor read twice in the same second is stable while
   * still looking alive over time.
   */
  noise(channel, bucket) {
    let x = (this.seed ^ (channel * 0x9e3779b1) ^ (bucket * 0x85ebca6b)) >>> 0;
    x ^= x << 13;
    x >>>= 0;
    x ^= x >> 17;
    x ^= x << 5;
    x >>>= 0;
    return x / 0xffffffff;
  }

  info() {
    return {
      version: "0.1.0-emulator",
      device: this.profile.id ?? "mock",
      screen: this.profile.screen,
    };
  }

  time() {
    return {
      epochMs: Date.now(),
      timezone: Intl.DateTimeFormat().resolvedOptions().timeZone ?? "UTC",
    };
  }

  power() {
    const charging = this.script.charging ?? false;
    const start = this.script.batteryPercent ?? 76;
    const ratePerHour = this.script.batteryDrainPerHour ?? 4;
    const drift = (this.elapsedMinutes() / 60) * ratePerHour;
    const percent = charging
      ? Math.min(100, Math.round(start + drift * 3))
      : Math.max(0, Math.round(start - drift));
    // A rough runtime estimate, which is all a real gauge gives you either.
    const estimateMinutes = charging
      ? Math.round(((100 - percent) / Math.max(1, ratePerHour * 3)) * 60)
      : Math.round((percent / Math.max(0.1, ratePerHour)) * 60);
    return { percent, charging, estimateMinutes };
  }

  setBrightness(percent) {
    this.brightness = Math.max(0, Math.min(100, Math.round(percent)));
    return {};
  }

  /** @param {string} sensor */
  read(sensor) {
    const scripted = this.script.sensors?.[sensor];
    const timestampMs = Date.now();
    if (typeof scripted === "number") {
      return { sensor, value: scripted, unit: UNITS[sensor] ?? "", timestampMs };
    }

    const minutes = this.elapsedMinutes();
    switch (sensor) {
      case "heartRate": {
        // A resting rate that wanders a few beats, resampled every 5 seconds.
        const bucket = Math.floor(timestampMs / 5000);
        return { sensor, value: Math.round(62 + this.noise(1, bucket) * 14), unit: "bpm", timestampMs };
      }
      case "steps": {
        // Steps only ever increase, at a believable walking cadence.
        const base = this.script.steps ?? 3120;
        return { sensor, value: Math.round(base + minutes * 22), unit: "steps", timestampMs };
      }
      case "accelerometer": {
        const bucket = Math.floor(timestampMs / 200);
        return {
          sensor,
          value: Number((0.98 + this.noise(2, bucket) * 0.08).toFixed(3)),
          unit: "g",
          timestampMs,
        };
      }
      case "temperature": {
        const bucket = Math.floor(timestampMs / 30000);
        return {
          sensor,
          value: Number((31.4 + this.noise(3, bucket) * 1.2).toFixed(1)),
          unit: "C",
          timestampMs,
        };
      }
      case "ambientLight": {
        const bucket = Math.floor(timestampMs / 10000);
        return { sensor, value: Math.round(40 + this.noise(4, bucket) * 380), unit: "lux", timestampMs };
      }
      default:
        return null;
    }
  }
}

const UNITS = {
  heartRate: "bpm",
  steps: "steps",
  accelerometer: "g",
  temperature: "C",
  ambientLight: "lux",
};

export const KNOWN_SENSORS = Object.keys(UNITS);
