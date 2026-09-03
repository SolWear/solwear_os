# __NAME__

A Solana transaction signer for SolWear, generated with
`solwear new --template signer`.

```sh
solwear run
solwear package
```

## The security model

This app never handles a private key. It calls `wallet.signTransaction` with a
base64 message and receives a signature. The key lives in `solweard`, which
raises the confirmation prompt on the watch itself and refuses to sign without
an affirmative action from the wearer. A declined prompt comes back as JSON-RPC
error `-32002`, which this app reports as "Declined on the watch."

If you ever find yourself wanting the key material in app code, the design is
wrong. Ask the daemon for the operation instead.

Under the host emulator the mock wallet signs with a throwaway key, so a
signature produced there is for testing only and means nothing on chain.
