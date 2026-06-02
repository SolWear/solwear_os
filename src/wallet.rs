pub struct Wallet {
    onboarded: bool,
    unlocked: bool,
    public_key: [u8; 32],
    name: String,
}

impl Wallet {
    pub fn new() -> Self {
        Self {
            onboarded: false,
            unlocked: false,
            public_key: [0; 32],
            name: String::new(),
        }
    }

    pub fn load_public(&mut self) {
        crate::protocol::emit_line("[CORE] wallet public state loaded");
    }

    pub fn is_onboarded(&self) -> bool {
        self.onboarded
    }

    pub fn public_key(&self) -> &[u8; 32] {
        &self.public_key
    }

    pub fn public_key_short(&self) -> String {
        format!(
            "{:02X}{:02X}...{:02X}{:02X}",
            self.public_key[0], self.public_key[1], self.public_key[30], self.public_key[31]
        )
    }

    pub fn name(&self) -> &str {
        if self.name.is_empty() {
            "PROTO V2"
        } else {
            &self.name
        }
    }

    pub fn sign(&self, tx_bytes: &[u8], sig: &mut [u8; 64]) -> bool {
        if !self.unlocked {
            crate::protocol::emit_error("wallet-locked", "cannot sign NFC transaction");
            return false;
        }

        // The C firmware signs exactly the transaction bytes received over NFC.
        // Full Ed25519/NVS seed unlock parity is implemented in the next wallet pass.
        let _ = (tx_bytes, sig);
        crate::protocol::emit_error("wallet-sign", "ed25519 signer not yet ported");
        false
    }

    pub fn lock(&mut self) {
        self.unlocked = false;
        crate::protocol::emit_result("wallet", "locked");
    }
}
