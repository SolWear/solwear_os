# Your First Watchface

We are going to build a complete watchface: it shows the time, the date and the
battery level, it adapts to a round 480x480 screen and a square 320x320 one
without a single hardcoded pixel value, and it ends up as a signed `.swa`
package you could install on a real device.

Budget about twenty minutes.

## 1. Scaffold

```bash
solwear new hello-watch --template watchface
cd hello-watch
```

You get:

```
hello-watch/
  manifest.json
  index.html
  src/main.ts
  src/style.css
  assets/icon.png
```

## 2. The manifest

Open `manifest.json` and set your identity. Ids are reverse-DNS and immutable —
once an id is published to the store it belongs to that app forever, so choose a
domain you control.

```json
{
  "id": "tech.example.hello-watch",
  "name": "Hello Watch",
  "version": "0.1.0",
  "sdk": "0.1",
  "type": "watchface",
  "entry": "index.html",
  "icon": "assets/icon.png",
  "capabilities": ["system", "power"],
  "author": "Your Name",
  "description": "A minimal adaptive watchface with time, date and battery."
}
```

`type` is `watchface` rather than `app`, which tells the shell to host this in
the watchface slot rather than the launcher. We ask for `system` (screen
geometry and the clock) and `power` (battery). We do not ask for anything else,
because we do not use anything else.

Full field-by-field detail is in the
[App Manifest Reference](app-manifest-reference.md).

## 3. The entry point

`index.html` stays deliberately thin. It is loaded inside a sandboxed iframe, so
there is no point reaching for anything exotic.

```html
<!doctype html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <meta name="viewport" content="width=device-width, initial-scale=1" />
    <link rel="stylesheet" href="src/style.css" />
  </head>
  <body>
    <main id="face">
      <div id="time">--:--</div>
      <div id="date"></div>
      <div id="battery"></div>
    </main>
    <script type="module" src="src/main.ts"></script>
  </body>
</html>
```

## 4. Adaptive layout

This is the part that separates a watchface that works from one that only works
on your screen. The rules from the specification are not stylistic advice:

- The root sizes itself from `system.screen`, never from constants.
- Round screens get a safe inset so nothing lands in the clipped corners.
- All typography and spacing derive from the smaller screen dimension.
- Target 60fps on a Pi 4: no layout thrash, no heavy shadows.

We express all of that through two custom properties that the script sets once,
`--unit` (one percent of the smaller screen dimension) and `--inset` (the safe
inset for the screen shape).

`src/style.css`:

```css
:root {
  --unit: 3.2px;    /* replaced at runtime from system.screen */
  --inset: 0px;     /* replaced at runtime; larger on round screens */
}

html,
body {
  margin: 0;
  height: 100%;
  background: #000;
  color: #fff;
  font-family: system-ui, -apple-system, sans-serif;
  overflow: hidden;
}

#face {
  box-sizing: border-box;
  height: 100%;
  padding: var(--inset);
  display: flex;
  flex-direction: column;
  align-items: center;
  justify-content: center;
  gap: calc(var(--unit) * 2);
}

#time {
  font-size: calc(var(--unit) * 22);
  font-weight: 300;
  line-height: 1;
  font-variant-numeric: tabular-nums;
  letter-spacing: -0.02em;
}

#date {
  font-size: calc(var(--unit) * 6);
  color: #9aa1ae;
}

#battery {
  font-size: calc(var(--unit) * 5);
  color: #6ee7a8;
  font-variant-numeric: tabular-nums;
}
```

`font-variant-numeric: tabular-nums` matters more than it looks: without it the
clock jitters horizontally every time a digit changes width.

## 5. The code

`src/main.ts`:

```ts
import { solwear } from "@solwear/sdk";

const timeEl = document.getElementById("time")!;
const dateEl = document.getElementById("date")!;
const batteryEl = document.getElementById("battery")!;

/**
 * Derive every dimension from the screen, once. `system.screen` is synchronous
 * precisely so this can run before the first paint.
 */
function applyScreen() {
  const { width, height, shape } = solwear.system.screen;
  const min = Math.min(width, height);

  // One unit is one percent of the smaller dimension.
  document.documentElement.style.setProperty("--unit", `${min / 100}px`);

  // A round screen clips its corners. Inset the content so nothing is lost.
  // 1 - 1/sqrt(2) is the corner overshoot of a square inscribed in a circle;
  // half of that is a comfortable, not wasteful, margin.
  const inset = shape === "round" ? min * 0.146 * 0.5 : min * 0.04;
  document.documentElement.style.setProperty("--inset", `${inset}px`);
}

const two = (n: number) => String(n).padStart(2, "0");

function renderTime(epochMs: number) {
  const d = new Date(epochMs);
  timeEl.textContent = `${two(d.getHours())}:${two(d.getMinutes())}`;
  dateEl.textContent = d.toLocaleDateString(undefined, {
    weekday: "short",
    day: "numeric",
    month: "short",
  });
}

async function renderBattery() {
  const { percent, charging } = await solwear.power.status();
  batteryEl.textContent = charging ? `${percent}% charging` : `${percent}%`;
}

async function main() {
  applyScreen();

  const { epochMs } = await solwear.system.time();
  renderTime(epochMs);
  await renderBattery();

  // One tick per second, throttled by the system when the screen is off.
  let ticks = 0;
  solwear.on("tick", (t) => {
    renderTime(t.epochMs);
    // Battery moves slowly. Polling it every second wastes power for nothing.
    if (ticks++ % 30 === 0) void renderBattery();
  });

  // Stop doing work the moment we are not on screen.
  solwear.on("visibility", ({ visible }) => {
    document.body.style.display = visible ? "" : "none";
  });
}

void main();
```

Three things to take from this:

1. **The clock is driven by `tick`, not by `setInterval`.** The system throttles
   `tick` when the display is off, which is most of a watch's life.
2. **Battery is polled at a fraction of the tick rate.** It changes over
   minutes, not seconds.
3. **Layout is computed once from `system.screen`.** Nothing in the CSS knows
   how big the screen is; it only knows about `--unit`.

## 6. Run it on every screen

```bash
solwear run --profile pi-round-480
solwear run --profile pi-square-320
solwear run --profile pi-wide-800x480
```

The emulator draws the bezel, so on `pi-round-480` you can see directly whether
your content survives the clipped corners. Fix it there before you ever touch
hardware. A watchface that has only been looked at on one profile is not
finished.

## 7. Package and sign

```bash
solwear build                          # bundles TypeScript into dist/
solwear package                        # dist/tech.example.hello-watch-0.1.0.swa
solwear sign --key ~/.solwear/publisher.key.json
```

`package` produces the ZIP archive. `sign` adds per-file SHA-256 digests and an
Ed25519 signature over their canonical listing to `signature.json`. See
[Package Format and Signing](package-format-and-signing.md) for the exact bytes.
Sideloading a package works without a signature; store distribution does not.

## 8. Put it on a device

```bash
solwear install --device solwear.local
```

This pushes the `.swa` over SSH to a running watch and asks `solweard` to
install it. If you do not have hardware yet, the QEMU emulator will boot the
real image and accept the same install — see
[Using the Emulator](using-the-emulator.md).

## Next

- [App Manifest Reference](app-manifest-reference.md) — every manifest field.
- [JSON-RPC API Reference](json-rpc-api-reference.md) — everything else the
  system can do.
- [Publishing to the Store](publishing-to-the-store.md) — getting it in front of
  other people.
