# __NAME__

A SolWear watchface, generated with `solwear new --template watchface`.

```sh
solwear run --profile pi-round-480
solwear package
```

## What a watchface must get right

- It is always on screen, so per-second work has to stay tiny. Repaint only the
  nodes whose value changed.
- It runs on every shipped screen. Size from `--sw-base` and keep content inside
  `--sw-safe`; check `pi-square-320` and `pi-wide-800x480` as well as the round
  profile.
- It keeps showing the time when a system call fails. Catch, degrade, carry on.
