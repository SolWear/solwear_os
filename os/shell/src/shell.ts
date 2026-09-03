// The system shell: watchface host, launcher, notification tray, settings, and
// the wallet confirmation prompt.

import { AppHost } from "./bridge.js";
import { applyScreen } from "./layout.js";
import { RpcClient, discoverRpcUrl } from "./rpc.js";
import type {
  AppRecord,
  ConfirmRequest,
  NetworkStatus,
  NotificationItem,
  PowerStatus,
  Screen,
  SystemInfo,
} from "./types.js";
import { clear, el, elide, formatClock, relativeTime } from "./ui.js";

type View = "watch" | "launcher" | "tray" | "settings" | "app";

const WATCHFACE_SETTING = "solwear.watchface";

export class Shell {
  private readonly rpc = new RpcClient();
  private readonly host: AppHost;
  private readonly root: HTMLElement;
  private readonly surface = el("div", { class: "surface" });
  private readonly statusBar = el("div", { class: "status-bar" });
  private readonly overlay = el("div", { class: "overlay", hidden: true });

  private view: View = "watch";
  private screen: Screen = { width: 480, height: 480, shape: "round" };
  private info: SystemInfo | null = null;
  private apps: AppRecord[] = [];
  private notifications: NotificationItem[] = [];
  private power: PowerStatus = { percent: 100, charging: false, estimateMinutes: 0 };
  private network: NetworkStatus = { connected: false, ssid: null, signal: null };
  private brightness = 70;
  private publicKey = "";
  private walletStatus = { onboarded: true, locked: false, protected: false, name: "SolWear" };
  private online = false;
  private confirming: ConfirmRequest | null = null;
  private pointerStart: { x: number; y: number; t: number } | null = null;

  constructor(root: HTMLElement) {
    this.root = root;
    this.host = new AppHost(
      this.rpc,
      () => this.screen,
      (phase, x, y, t) => this.trackPointer(phase, x, y, t),
    );
    this.root.append(this.statusBar, this.surface, this.overlay);
    this.installGestures();
    this.installEvents();
  }

  async start(): Promise<void> {
    // The daemon knows which port it bound; ask before opening the socket.
    this.rpc.configure(await discoverRpcUrl());

    this.rpc.onStatus((connected) => {
      this.online = connected;
      if (connected) void this.refreshAll();
      this.renderStatusBar();
    });
    this.rpc.connect();

    // The clock keeps ticking whether or not the daemon is reachable.
    window.setInterval(() => this.onSecond(), 1000);
    window.setInterval(() => void this.refreshVolatile(), 15_000);
    this.render();
  }

  // --- data --------------------------------------------------------------

  private async refreshAll(): Promise<void> {
    try {
      this.info = await this.rpc.call<SystemInfo>("system.info");
      this.screen = this.info.screen;
      applyScreen(this.screen);
    } catch {
      applyScreen(this.screen);
    }
    await Promise.allSettled([
      this.refreshApps(),
      this.refreshNotifications(),
      this.refreshVolatile(),
      this.refreshWallet(),
    ]);
    this.render();
  }

  private async refreshApps(): Promise<void> {
    const result = await this.rpc.call<{ apps: AppRecord[] }>("apps.list");
    this.apps = result.apps;
  }

  private async refreshNotifications(): Promise<void> {
    const result = await this.rpc.call<{ items: NotificationItem[] }>("notifications.list");
    this.notifications = result.items;
  }

  private async refreshWallet(): Promise<void> {
    const result = await this.rpc.call<{ publicKey: string; onboarded: boolean; locked: boolean; protected: boolean; name: string }>("wallet.status");
    this.publicKey = result.publicKey;
    this.walletStatus = result;
  }

  private async refreshVolatile(): Promise<void> {
    if (!this.online) return;
    const [power, network, brightness] = await Promise.allSettled([
      this.rpc.call<PowerStatus>("power.status"),
      this.rpc.call<NetworkStatus>("system.network"),
      this.rpc.call<{ percent: number }>("display.getBrightness"),
    ]);
    if (power.status === "fulfilled") this.power = power.value;
    if (network.status === "fulfilled") this.network = network.value;
    if (brightness.status === "fulfilled") this.brightness = brightness.value.percent;
    this.renderStatusBar();
    if (this.view === "watch") this.render();
  }

  private installEvents(): void {
    this.rpc.on("notifications.posted", (params) => {
      const notification = params.notification as NotificationItem | undefined;
      if (!notification) return;
      this.notifications = [notification, ...this.notifications].slice(0, 64);
      if (this.view !== "app") this.render();
      this.renderStatusBar();
    });

    this.rpc.on("apps.changed", () => {
      void this.refreshApps().then(() => this.render());
    });

    this.rpc.on("apps.launch", (params) => {
      const appId = String(params.appId ?? "");
      const app = this.apps.find((candidate) => candidate.id === appId);
      if (app) this.openApp(app);
    });

    this.rpc.on("wallet.confirmRequest", (params) => {
      this.confirming = params as unknown as ConfirmRequest;
      this.renderOverlay();
    });

    this.rpc.on("wallet.confirmCancelled", () => {
      this.confirming = null;
      this.renderOverlay();
    });

    this.rpc.on("display.brightnessChanged", (params) => {
      this.brightness = Number(params.percent ?? this.brightness);
    });
  }

  private onSecond(): void {
    if (this.view === "watch") this.renderWatchface();
    this.renderStatusBar();
  }

  // --- navigation --------------------------------------------------------

  private go(view: View): void {
    if (view === this.view) return;
    if (this.view === "app" && view !== "app") this.host.unmount();
    this.view = view;
    this.render();
  }

  private openApp(app: AppRecord): void {
    this.view = "app";
    clear(this.surface);
    const stage = el("div", { class: "app-stage" });
    this.surface.append(stage);
    this.host.mount(stage, app);
    this.renderStatusBar();
  }

  /**
   * Swipes drive the whole shell: up for the launcher, down for the tray,
   * right to go back. Arrow keys do the same thing so the emulator and a
   * desktop browser are usable without a touchscreen.
   */
  private installGestures(): void {
    this.root.addEventListener(
      "pointerdown",
      (event) => {
        const bounds = this.root.getBoundingClientRect();
        this.trackPointer(
          "down",
          (event.clientX - bounds.left) / Math.max(1, bounds.width),
          (event.clientY - bounds.top) / Math.max(1, bounds.height),
          event.timeStamp,
        );
      },
      { passive: true },
    );

    this.root.addEventListener(
      "pointerup",
      (event) => {
        const bounds = this.root.getBoundingClientRect();
        this.trackPointer(
          "up",
          (event.clientX - bounds.left) / Math.max(1, bounds.width),
          (event.clientY - bounds.top) / Math.max(1, bounds.height),
          event.timeStamp,
        );
      },
      { passive: true },
    );

    window.addEventListener("keydown", (event) => {
      const map: Record<string, "up" | "down" | "left" | "right"> = {
        ArrowUp: "up",
        ArrowDown: "down",
        ArrowLeft: "left",
        ArrowRight: "right",
        Escape: "right",
      };
      const gesture = map[event.key];
      if (gesture) {
        event.preventDefault();
        this.onGesture(gesture);
      }
    });
  }

  /** Recognise the same normalised swipe whether it starts in shell chrome or
   * inside the sandboxed app iframe. */
  private trackPointer(phase: "down" | "up", x: number, y: number, t: number): void {
    if (phase === "down") {
      this.pointerStart = { x, y, t };
      return;
    }
    const start = this.pointerStart;
    this.pointerStart = null;
    if (!start || t < start.t || t - start.t > 1500) return;

    const dx = x - start.x;
    const dy = y - start.y;
    const threshold = 0.15;
    if (Math.abs(dx) < threshold && Math.abs(dy) < threshold) return;

    if (Math.abs(dy) > Math.abs(dx)) {
      this.onGesture(dy < 0 ? "up" : "down");
    } else {
      this.onGesture(dx > 0 ? "right" : "left");
    }
  }

  private onGesture(direction: "up" | "down" | "left" | "right"): void {
    if (this.confirming) return;
    if (this.view === "app") {
      // Apps see gestures too, but a swipe back always belongs to the system.
      if (direction === "right") {
        this.go("watch");
      } else {
        this.host.emit("gesture", { direction });
      }
      return;
    }

    switch (this.view) {
      case "watch":
        if (direction === "up") this.go("launcher");
        if (direction === "down") this.go("tray");
        if (direction === "left") this.go("settings");
        break;
      default:
        if (direction === "right" || direction === "down") this.go("watch");
        break;
    }
  }

  // --- rendering ---------------------------------------------------------

  private render(): void {
    this.renderStatusBar();
    if (this.view !== "app") clear(this.surface);
    switch (this.view) {
      case "watch":
        this.renderWatchface();
        break;
      case "launcher":
        this.surface.append(this.launcherView());
        break;
      case "tray":
        this.surface.append(this.trayView());
        break;
      case "settings":
        this.surface.append(this.settingsView());
        break;
      case "app":
        break;
    }
    this.renderOverlay();
  }

  private renderStatusBar(): void {
    clear(this.statusBar);
    const { time } = formatClock(new Date());
    // The watchface already carries the time and the date, so the status bar
    // leaves its label empty there rather than printing either of them twice.
    // Every other view gets the clock, because nothing else on screen has it.
    const label =
      this.view === "app" && this.host.current
        ? this.host.current.name
        : this.view === "watch"
          ? ""
          : time;

    this.statusBar.append(
      el("span", { class: "status-label", text: label }),
      el(
        "span",
        { class: "status-right" },
        el("span", {
          class: `dot ${this.online ? "dot-live" : "dot-dead"}`,
          title: this.online ? "connected to solweard" : "reconnecting",
        }),
        el("span", {
          class: `battery ${this.power.charging ? "charging" : ""}`,
          text: `${Math.round(this.power.percent)}%`,
        }),
      ),
    );
  }

  /** Built-in watchface, used unless an installed watchface is selected. */
  private renderWatchface(): void {
    const selected = this.selectedWatchface();
    if (selected) {
      if (this.host.current?.id !== selected.id) {
        clear(this.surface);
        const stage = el("div", { class: "app-stage watchface-stage" });
        this.surface.append(stage);
        this.host.mount(stage, selected);
      }
      return;
    }

    clear(this.surface);
    const now = new Date();
    const { time, seconds, date } = formatClock(now);
    const unread = this.notifications.length;

    this.surface.append(
      el(
        "div",
        { class: "watchface" },
        el(
          "div",
          { class: "clock" },
          el("span", { class: "clock-time", text: time }),
          el("span", { class: "clock-seconds", text: seconds }),
        ),
        el("div", { class: "clock-date", text: date }),
        el(
          "div",
          { class: "watchface-meta" },
          this.meter("Battery", `${Math.round(this.power.percent)}%`, this.power.percent),
          this.meter(
            "Network",
            this.network.connected ? (this.network.ssid ?? "online") : "offline",
            // Only draw a bar when the HAL actually reports a strength.
            // Painting a full bar for "connected" would be an invented reading.
            this.network.signal ?? undefined,
          ),
        ),
        unread > 0
          ? el("button", {
              class: "pill",
              text: `${unread} notification${unread === 1 ? "" : "s"}`,
              onclick: () => this.go("tray"),
            })
          : el("div", { class: "hint", text: "swipe up for apps" }),
      ),
    );
  }

  private meter(label: string, value: string, percent?: number): HTMLElement {
    const meter = el(
      "div",
      { class: "meter" },
      el("div", { class: "meter-label", text: label }),
      el("div", { class: "meter-value", text: value }),
    );
    if (percent !== undefined) {
      const clamped = Math.max(0, Math.min(100, percent));
      meter.append(
        el(
          "div",
          { class: "meter-track" },
          el("div", { class: "meter-fill", style: `width:${clamped}%` }),
        ),
      );
    }
    return meter;
  }

  private launcherView(): HTMLElement {
    const launchable = this.apps.filter((app) => app.type === "app");
    const grid = el("div", { class: "grid" });

    for (const app of launchable) {
      grid.append(
        el(
          "button",
          {
            class: "tile",
            onclick: () => {
              void this.rpc.call("apps.launch", { appId: app.id }).catch(() => this.openApp(app));
            },
          },
          app.icon
            ? el("img", { class: "tile-icon", src: `/apps/${app.id}/${app.icon}`, alt: "" })
            : el("span", { class: "tile-glyph", text: initials(app.name) }),
          el("span", { class: "tile-name", text: app.name }),
        ),
      );
    }

    grid.append(
      el(
        "button",
        { class: "tile tile-system", onclick: () => this.go("settings") },
        el("span", { class: "tile-glyph", text: "⚙" }),
        el("span", { class: "tile-name", text: "Settings" }),
      ),
    );

    return this.page(
      "Apps",
      launchable.length === 0
        ? el("p", { class: "empty", text: "No apps installed yet. Sideload one with the solwear CLI." })
        : grid,
    );
  }

  private trayView(): HTMLElement {
    if (this.notifications.length === 0) {
      return this.page("Notifications", el("p", { class: "empty", text: "Nothing new." }));
    }
    const list = el("div", { class: "list" });
    for (const item of this.notifications) {
      list.append(
        el(
          "div",
          { class: "card" },
          el(
            "div",
            { class: "card-head" },
            el("span", { class: "card-title", text: item.title }),
            el("span", { class: "card-time", text: relativeTime(item.timestampMs) }),
          ),
          item.body ? el("p", { class: "card-body", text: item.body }) : null,
          el("span", { class: "card-source", text: item.appId }),
        ),
      );
    }
    return this.page("Notifications", list);
  }

  private settingsView(): HTMLElement {
    const watchfaces = this.apps.filter((app) => app.type === "watchface");
    const selectedId = localStorage.getItem(WATCHFACE_SETTING) ?? "";

    const brightness = el("input", {
      class: "slider",
      type: "range",
      min: "5",
      max: "100",
      step: "5",
      value: String(this.brightness),
      oninput: (event: Event) => {
        const percent = Number((event.target as HTMLInputElement).value);
        this.brightness = percent;
        void this.rpc.call("display.setBrightness", { percent }).catch(() => undefined);
      },
    });

    const faceSelect = el("select", {
      class: "select",
      onchange: (event: Event) => {
        const value = (event.target as HTMLSelectElement).value;
        if (value) localStorage.setItem(WATCHFACE_SETTING, value);
        else localStorage.removeItem(WATCHFACE_SETTING);
        this.host.unmount();
        this.go("watch");
        this.render();
      },
    });
    faceSelect.append(el("option", { value: "", text: "Built-in", selected: selectedId === "" }));
    for (const face of watchfaces) {
      faceSelect.append(
        el("option", { value: face.id, text: face.name, selected: face.id === selectedId }),
      );
    }

    const passphrase = el("input", { class: "select mono", type: "password", placeholder: "8+ character passphrase" });
    const walletAction = el("button", {
      class: "mini-button",
      text: this.walletStatus.locked ? "Unlock" : this.walletStatus.protected ? "Lock" : "Protect",
      onclick: async () => {
        try {
          if (this.walletStatus.locked) await this.rpc.call("wallet.unlock", { passphrase: passphrase.value });
          else if (this.walletStatus.protected) await this.rpc.call("wallet.lock");
          else await this.rpc.call("wallet.setPassphrase", { passphrase: passphrase.value, name: "SolWear" });
          await this.refreshWallet();
          this.render();
        } catch (error) {
          passphrase.value = "";
          passphrase.placeholder = error instanceof Error ? error.message : "Wallet action failed";
        }
      },
    });
    const walletControls = el("span", { class: "wallet-controls" }, passphrase, walletAction);

    return this.page(
      "Settings",
      el(
        "div",
        { class: "list" },
        this.settingRow("Brightness", brightness),
        this.settingRow("Watchface", faceSelect),
        this.settingRow(
          "Network",
          el("span", {
            class: "value",
            text: this.network.connected ? (this.network.ssid ?? "connected") : "offline",
          }),
        ),
        this.settingRow(
          "Battery",
          el("span", {
            class: "value",
            text: `${Math.round(this.power.percent)}% · ${
              this.power.charging ? "charging" : `${this.power.estimateMinutes} min left`
            }`,
          }),
        ),
        this.settingRow(
          "Wallet",
          el("span", { class: "value mono", text: `${this.walletStatus.locked ? "locked · " : ""}${this.publicKey ? elide(this.publicKey, 6, 6) : "—"}` }),
        ),
        this.settingRow("Security", walletControls),
        this.settingRow(
          "Device",
          el("span", {
            class: "value",
            text: this.info ? `${this.info.device} · ${this.screen.width}×${this.screen.height}` : "—",
          }),
        ),
        this.settingRow(
          "solweard",
          el("span", { class: "value mono", text: this.info?.version ?? "—" }),
        ),
      ),
    );
  }

  private settingRow(label: string, control: HTMLElement): HTMLElement {
    return el(
      "div",
      { class: "row" },
      el("span", { class: "row-label", text: label }),
      el("span", { class: "row-control" }, control),
    );
  }

  private page(title: string, body: HTMLElement): HTMLElement {
    return el(
      "div",
      { class: "page" },
      el("h1", { class: "page-title", text: title }),
      el("div", { class: "page-body" }, body),
      el("button", { class: "back", text: "Back", onclick: () => this.go("watch") }),
    );
  }

  /** The wallet confirmation prompt: the only path to a signature. */
  private renderOverlay(): void {
    clear(this.overlay);
    const request = this.confirming;
    if (!request) {
      this.overlay.hidden = true;
      return;
    }
    this.overlay.hidden = false;

    const summary = request.summary ?? ({} as ConfirmRequest["summary"]);
    const answer = (approved: boolean) => {
      this.confirming = null;
      this.renderOverlay();
      void this.rpc
        .call("shell.confirmResponse", { requestId: request.requestId, approved })
        .catch(() => undefined);
    };

    this.overlay.append(
      el(
        "div",
        { class: "confirm" },
        el("h2", { class: "confirm-title", text: "Sign transaction?" }),
        el("p", { class: "confirm-app", text: request.appId }),
        el(
          "dl",
          { class: "confirm-detail" },
          el("dt", { text: "Account" }),
          el("dd", { class: "mono", text: elide(summary.publicKey ?? "", 6, 6) }),
          el("dt", { text: "Payload" }),
          el("dd", { class: "mono", text: `${summary.byteLength ?? 0} bytes` }),
          el("dt", { text: "Digest" }),
          el("dd", { class: "mono", text: elide(summary.digest ?? "", 8, 8) }),
        ),
        el(
          "div",
          { class: "confirm-actions" },
          el("button", { class: "button ghost", text: "Reject", onclick: () => answer(false) }),
          el("button", { class: "button primary", text: "Approve", onclick: () => answer(true) }),
        ),
      ),
    );
  }

  private selectedWatchface(): AppRecord | null {
    const id = localStorage.getItem(WATCHFACE_SETTING);
    if (!id) return null;
    return this.apps.find((app) => app.id === id && app.type === "watchface") ?? null;
  }
}

/**
 * A two-letter glyph for an app with no icon.
 *
 * Word initials are the obvious choice and the wrong one here: apps from the
 * same publisher share a prefix, so "SolWear Signer" and "SolWear Store" both
 * collapse to "SS". The distinguishing word is the last one, so that is what
 * the glyph is taken from.
 */
function initials(name: string): string {
  const words = name.trim().split(/\s+/).filter(Boolean);
  const distinctive = words[words.length - 1] ?? "";
  const letters = distinctive.replace(/[^\p{L}\p{N}]/gu, "");
  if (letters.length === 0) return "?";
  return (letters[0]?.toUpperCase() ?? "") + (letters[1]?.toLowerCase() ?? "");
}
