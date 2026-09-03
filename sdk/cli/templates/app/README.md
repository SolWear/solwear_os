# __NAME__

A SolWear app, generated with `solwear new --template app`.

```sh
solwear run       # open it in the host emulator
solwear build     # bundle into dist/
solwear package   # produce dist/__ID__-1.0.0.swa
solwear sign --key ~/.solwear/publisher.key.json
```

`solwear run --profile pi-round-480` picks a device profile; run
`solwear run --list-profiles` to see them all. Check the layout on the smallest
and the largest profile before you ship: the display is adaptive, so a fixed
pixel size is a bug.

For editor autocompletion, run `npm install` once. The build does not need it;
inside the monorepo the CLI links `@solwear/sdk` straight from `sdk/runtime`.

## Capabilities

`manifest.json` lists the namespaces this app may call. Asking for less is
better: `solweard` refuses anything outside the list, and the store shows the
list to the wearer before they install.
