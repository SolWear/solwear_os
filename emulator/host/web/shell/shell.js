const state = await fetch("/emulator/state.json").then((response) => response.json());
const stage = document.querySelector("#stage");
const offline = document.querySelector("#offline");
const frame = document.createElement("iframe");
frame.sandbox = "allow-scripts";
frame.src = state.app.url ?? "/app/";
frame.title = state.app.name;
stage.append(frame);

let socket;
let nextId = 1;
const pending = new Map();
function connect() {
  socket = new WebSocket(state.rpcUrl + "rpc");
  socket.addEventListener("open", () => { offline.hidden = true; });
  socket.addEventListener("close", () => { offline.hidden = false; setTimeout(connect, 500); });
  socket.addEventListener("message", ({ data }) => {
    const message = JSON.parse(data);
    if (message.method === "wallet.confirmRequest") {
      const approved = confirm(`Allow ${message.params.appId} to sign ${message.params.summary.byteLength} bytes?`);
      void call("shell.confirmResponse", { requestId: message.params.requestId, approved });
      return;
    }
    const callback = pending.get(Number(message.id));
    if (callback) { pending.delete(Number(message.id)); callback(message); }
  });
}
function call(method, params, appId) {
  return new Promise((resolve) => {
    const id = nextId++;
    pending.set(id, resolve);
    socket.send(JSON.stringify({ jsonrpc: "2.0", id, method, params, ...(appId ? { appId } : {}) }));
  });
}
connect();

function post(message) { frame.contentWindow?.postMessage({ solwear: 1, ...message }, "*"); }
frame.addEventListener("load", () => post({ kind: "init", appId: state.app.id, capabilities: state.app.capabilities, screen: state.profile.screen, device: state.profile.id, osVersion: "0.1.0-emulator", visible: true }));
window.addEventListener("message", async (event) => {
  if (event.source !== frame.contentWindow) return;
  const message = event.data;
  if (message?.solwear !== 1 || message.kind !== "rpc") return;
  const capability = String(message.method).split(".")[0];
  if (!state.app.capabilities.includes(capability)) return post({ kind: "error", id: message.id, error: { code: -32001, message: `capability ${capability} denied` } });
  const response = await call(message.method, message.params ?? {}, state.app.id);
  post(response.error ? { kind: "error", id: message.id, error: response.error } : { kind: "result", id: message.id, result: response.result });
});
setInterval(() => post({ kind: "event", event: "tick", payload: { epochMs: Date.now() } }), 1000);
