# SolWear for VS Code

The extension exposes the SolWear developer loop in the Command Palette:

- **SolWear: New App** creates an app, watchface or signer project.
- **SolWear: Build** bundles the active workspace app.
- **SolWear: Run in Emulator** builds and starts the host emulator.
- **SolWear: Package .swa** produces an installable archive.
- **SolWear: Doctor** checks Node, Rust, QEMU, SSH, browser and signing tools.

The same actions are available as `type: "solwear"` tasks:

```json
{
  "version": "2.0.0",
  "tasks": [
    { "type": "solwear", "command": "build", "group": "build" },
    { "type": "solwear", "command": "run", "args": ["--profile", "pi-round-240"] }
  ]
}
```

Commands use VS Code `ProcessExecution`, not shell command strings. Project
names, paths and task arguments therefore remain individual process arguments.
The build task includes the `$solwear` problem matcher and all commands run in
the selected workspace folder.

## CLI selection

The extension tries, in order:

1. `solwear.cliPath` (absolute or relative to the workspace);
2. `sdk/cli/dist/bin.js` in the workspace;
3. the CLI next to this extension in a SolWear checkout;
4. `solwear` on `PATH`.

JavaScript CLI paths are launched through `node`; installed executable paths
are invoked directly. `solwear.defaultProfile` controls the Run command's
emulator profile.

## Development

```sh
npm ci
npm run typecheck
npm run build
```

Press F5 from an Extension Development Host configuration, or package `dist/`
with your preferred VS Code extension packaging tool.
