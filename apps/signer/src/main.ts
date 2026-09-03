/**
 * SolWear Signer.
 *
 * The original SolWear product, now an ordinary SolWear OS app: it asks the
 * daemon to sign a message with the device wallet. The app never sees the
 * private key and cannot sign anything on its own. Every signature is a
 * separate `wallet.signTransaction` call, and the daemon refuses to answer it
 * until the person wearing the watch confirms the prompt the shell puts on
 * screen.
 */

import { ERR_USER_REJECTED, layout, solwear, SolwearRpcError } from "@solwear/sdk";

const keyView = document.querySelector<HTMLElement>("#key")!;
const messageInput = document.querySelector<HTMLTextAreaElement>("#message")!;
const signButton = document.querySelector<HTMLButtonElement>("#sign")!;
const statusView = document.querySelector<HTMLElement>("#status")!;
const signatureView = document.querySelector<HTMLElement>("#signature")!;

/** Show the ends of a base58 key, which is what a person compares by eye. */
function abbreviate(value: string, keep = 6): string {
  if (value.length <= keep * 2 + 1) return value;
  return `${value.slice(0, keep)}…${value.slice(-keep)}`;
}

/** UTF-8 safe base64, since the daemon takes the message as base64 bytes. */
function toBase64(text: string): string {
  const bytes = new TextEncoder().encode(text);
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary);
}

function setStatus(text: string, kind: "info" | "error" = "info"): void {
  statusView.textContent = text;
  statusView.classList.toggle("error", kind === "error");
}

async function requestSignature(): Promise<void> {
  const message = messageInput.value;
  if (message.trim().length === 0) {
    setStatus("Nothing to sign", "error");
    return;
  }

  signButton.disabled = true;
  signatureView.textContent = "";
  setStatus("Confirm on the watch…");

  try {
    const signature = await solwear.wallet.signTransaction(toBase64(message), {
      label: message.slice(0, 40),
    });
    signatureView.textContent = abbreviate(signature, 8);
    signatureView.title = signature;
    setStatus("Signed");
    await solwear.notifications.post({
      title: "Message signed",
      body: message.slice(0, 80),
    });
  } catch (error) {
    const declined = error instanceof SolwearRpcError && error.code === ERR_USER_REJECTED;
    setStatus(declined ? "Declined" : String(error), "error");
  } finally {
    signButton.disabled = false;
  }
}

async function start(): Promise<void> {
  await solwear.ready();
  layout(solwear.system.screen);

  const publicKey = await solwear.wallet.publicKey();
  keyView.textContent = abbreviate(publicKey);
  keyView.title = publicKey;

  signButton.disabled = false;
  signButton.addEventListener("click", () => void requestSignature());
}

void start().catch((error) => setStatus(String(error), "error"));
