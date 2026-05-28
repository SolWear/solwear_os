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

    pub fn lock(&mut self) {
        self.unlocked = false;
        crate::protocol::emit_result("wallet", "locked");
    }
}
