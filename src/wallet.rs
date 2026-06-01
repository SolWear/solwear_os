pub struct Wallet {
    onboarded: bool,
    unlocked: bool,
    public_key: [u8; 32],
}

impl Wallet {
    pub fn new() -> Self {
        Self {
            onboarded: false,
            unlocked: false,
            public_key: [0; 32],
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
