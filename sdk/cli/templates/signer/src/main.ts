/**
 * __NAME__ — a SolWear transaction signer.
 *
 * The one rule that matters: this app never sees a private key. It hands a
 * serialised message to `wallet.signTransaction` and gets a signature back.
 * The key stays inside `solweard`, and the daemon raises the confirmation
 * prompt itself, so a malicious app cannot sign anything on its own.
 */

import { layout, solwear, SolwearRpcError, ERR_USER_REJECTED } from "@solwear/sdk";

const publicKeyLabel = document.getElementById("publicKey")!;
const signButton = document.getElementById("sign") as HTMLButtonElement;
const status = document.getElementById("status")!;
const signatureLabel = document.getElementById("signature")!;

function setStatus(text: string, kind: "" | "error" | "done" = ""): void {
  status.textContent = text;
  status.className = `status${kind ? ` ${kind}` : ""}`;
}

function shorten(value: string, keep = 8): string {
  return value.length <= keep * 2 + 3 ? value : `${value.slice(0, keep)}...${value.slice(-keep)}`;
}

async function main(): Promise<void> {
  await solwear.ready();
  layout(solwear.system.screen);
  window.addEventListener("resize", () => layout(solwear.system.screen));

  try {
    const publicKey = await solwear.wallet.publicKey();
    publicKeyLabel.textContent = shorten(publicKey, 10);
    publicKeyLabel.title = publicKey;
    signButton.disabled = false;
  } catch (error) {
    publicKeyLabel.textContent = "no wallet on this device";
    setStatus(error instanceof Error ? error.message : String(error), "error");
    return;
  }

  signButton.addEventListener("click", () => void onSign());
}

async function onSign(): Promise<void> {
  signButton.disabled = true;
  signatureLabel.textContent = "";
  setStatus("Confirm on the watch...");

  try {
    // Replace this with a real serialised Solana transaction message. It is
    // base64 on the wire; the daemon shows the wearer what it is about to sign.
    const message = btoa(`solwear demo transfer ${Date.now()}`);
    const signature = await solwear.wallet.signTransaction(message);

    signatureLabel.textContent = signature;
    setStatus("Signed.", "done");
    await solwear.notifications.post({ title: "Transaction signed", body: shorten(signature, 12) });
  } catch (error) {
    if (error instanceof SolwearRpcError && error.code === ERR_USER_REJECTED) {
      setStatus("Declined on the watch.", "error");
    } else {
      setStatus(error instanceof Error ? error.message : String(error), "error");
    }
  } finally {
    signButton.disabled = false;
  }
}

void main().catch((error: unknown) => {
  setStatus(error instanceof Error ? error.message : String(error), "error");
});
