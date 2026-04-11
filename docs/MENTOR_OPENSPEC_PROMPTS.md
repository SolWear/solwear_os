# SolWear OpenSpec Prompt Pack for Mentor AI

Date: 2026-04-11
Purpose: These prompts are designed for an external AI that has NO access to our private repositories. Each prompt includes full context, constraints, architecture expectations, contracts, deliverables, and acceptance criteria.

How to use:
- Run one prompt at a time.
- Provide the AI with only that prompt.
- Ask the AI to return a complete project artifact bundle (code + docs + tests).

---

## 0) Shared Context Prompt (Run First)

Use this before any project-specific prompt to align architecture and naming.

```text
You are a principal software architect working on the SolWear ecosystem.
You do not have access to our private repos. Treat this prompt as source of truth.

Ecosystem components:
1) SolWearOS firmware (RP2040 smartwatch)
1b) SolWearOS firmware (ESP32-S3 Mini prototype branch)
2) SolWear Service Tool desktop app (Python + Tkinter)
3) SolWear Android companion app (Kotlin + Compose)
4) SolWear website (new project)

Product direction:
- SolWear is a smartwatch ecosystem with health tracking, diagnostics, wallet flows, and NFC/POS experimentation.
- Development is currently mock/sandbox-first, but must be migration-ready for production integrations.

Current hardware baseline:
- Board: Waveshare RP2040-Touch-LCD-1.69
- Display: ST7789V2 240x280
- Touch: CST816S
- IMU: QMI8658
- NFC: PN532 (lazy-powered)
- Battery ADC: GP29
- Buzzer: GP20
- USB CDC serial heartbeat every 5s

New prototype targets to include in planning:
- Board family: ESP32-S3 Mini
- Prototype A display: Waveshare e-ink module 1.54-inch, 200x200
- Prototype B display: 1.3-inch TFT IPS ST7789, 240x240, 4 physical buttons

Cross-project contracts to preserve:
- Firmware heartbeat line format:
  [STATUS] batt=<pct> volt=<volts> heap=<bytes> steps=<n> uptime=<sec> charging=<0|1>
- Service tool parses these lines and displays freshness/health.
- Mobile app remains mock-first by default, with clear live-mode gates.
- Sensitive data must be redacted in logs everywhere.

Engineering policy:
- No pseudo code.
- Production-oriented architecture and tests.
- Explicit assumptions logged in ASSUMPTIONS.md.
- Security, observability, and maintainability are first-class.

Return format policy for all tasks:
1) architecture summary
2) module tree
3) implementation
4) tests
5) docs
6) acceptance checklist with done/partial/blocked
7) production hardening TODOs (only if out of scope)
```

---

## 1) Prompt: SolWearOS Firmware Phase-2 (Security + BLE + OTA Foundation)

```text
You are a principal embedded engineer for RP2040 firmware.
Build the next production-oriented phase of SolWearOS firmware.
You do not have repository access, so this prompt is the full specification.

Project summary:
- PlatformIO + Arduino core (earlephilhower)
- RP2040 smartwatch firmware with app framework (watchface, home, settings, wallet, NFC, health, game, alarm, charging, stats)
- Event-driven architecture with lock-free ring buffer
- LittleFS persistent settings/wallet/history
- NFC PN532 should remain lazy-powered/off until needed
- USB serial monitor speed 115200

Already available conceptually:
- App launcher + watchfaces
- Health/steps
- Wallet + NFC URI handling stubs
- Charging screen and status heartbeat

Primary objective for this phase:
Implement secure wallet foundations + BLE sync transport + OTA preparation while preserving runtime stability and power behavior.

Mandatory feature scope:
A) Wallet security foundation
- Add BIP-39 mnemonic generation/import (12 words minimum)
- Add wallet setup wizard screens and persistent encrypted-at-rest strategy suitable for RP2040 constraints
- Implement Ed25519 signing flow abstraction (real signing path, test vectors, and mock mode)
- Add explicit user confirmation UX for signing actions
- Never print seed/private key in logs

B) BLE transport foundation
- Add BLE service design and packet protocol for:
  - status sync
  - settings sync
  - simple command/ack channel
- Implement reconnection strategy, timeout policy, and power-aware behavior
- Preserve USB CDC heartbeat for service tool compatibility

C) OTA preflight architecture
- Implement OTA metadata verifier scaffolding:
  - firmware version
  - checksum/signature placeholder contract
  - rollback-safe state machine design doc + code hooks
- No fake OTA flashing if unsupported by boot path; instead implement robust preflight validators and state transitions

D) System hardening
- Add watchdog-safe loop instrumentation
- Add memory pressure guards for display sprite fallback path
- Ensure app transitions cannot deadlock event queue
- Extend diagnostics counters exposed over serial (non-sensitive)

Non-functional constraints:
- Keep binary and RAM usage disciplined for RP2040 limits
- No blocking operations in render-critical paths
- Preserve 30fps target where feasible
- Keep NFC powered off unless in NFC/Wallet flows

Required architecture outputs:
- Updated module diagram
- Data flow for wallet setup/signing
- BLE packet schema
- OTA state machine
- Threat model summary (seed, signing, transport)

Deliverables:
- Full source implementation
- Build instructions with PlatformIO
- Test strategy:
  - host/unit tests for parsers/state machines
  - firmware-side integration test plan
- README updates
- SECURITY.md
- ASSUMPTIONS.md
- ACCEPTANCE_CHECKLIST.md
- OPENSPEC_TRACEABILITY.md

Acceptance criteria:
- Firmware builds cleanly
- Wallet setup + signing confirmation flow is operational
- BLE sync prototype channel exchanges deterministic packets
- OTA preflight state machine runs through valid and invalid metadata cases
- Heartbeat remains backward-compatible
- No sensitive secrets in logs

If ambiguity exists, make the safest minimal assumption, document it, and continue.
```

---

## 2) Prompt: SolWear Service Tool v2 (Cross-Platform + BLE + Diagnostics Depth)

```text
You are a principal desktop engineer.
Build SolWear Service Tool v2 as a production-grade developer utility.
No repository access is available. Use this prompt as authoritative.

Current baseline behavior:
- Python 3.9+ desktop app
- Tkinter UI
- pyserial dependency
- tabs: console, status, settings, firmware
- parses firmware heartbeat lines:
  [STATUS] batt=<pct> volt=<volts> heap=<bytes> steps=<n> uptime=<sec> charging=<0|1>
- can flash UF2 by copying to RPI-RP2 mounted drive on Windows

Primary objective:
Evolve tool into multi-transport diagnostics hub (USB serial + BLE) with better analysis, export, and stability.

Also support multi-hardware profiles so the same tool can handle RP2040 watch firmware and ESP32-S3 Mini prototype firmware.

Mandatory feature scope:
A) Transport layer
- Abstract transport interface for USB serial and BLE
- Keep serial as default
- Add BLE discovery/connect/disconnect flow and transport health indicator
- Auto-reconnect policy and stale heartbeat detection

A2) Device profile layer
- Add device profile abstraction (RP2040 watch, ESP32-S3 Mini e-ink prototype, ESP32-S3 Mini TFT+buttons prototype)
- Per-profile parsers/capabilities (supported telemetry fields, control commands, firmware artifact types)
- Profile auto-detection where possible, manual override if uncertain
- UI badge showing active profile and protocol version

B) Diagnostics depth
- Structured log viewer with filters, search, tags, and severity coloring
- Timeline mode for status metrics (battery, voltage, heap, steps)
- Session recording and export (JSON + plaintext)
- Device snapshot report (firmware version, uptime, recent errors, config)

C) Firmware operations
- Keep UF2 flashing
- Add pre-checks (file extension, size sanity, device mount detection)
- Add explicit result messages with failure reasons

C2) Multi-target firmware flow
- Keep RP2040 UF2 path
- Add ESP32-S3 flashing workflow hook points (serial/USB bootloader command path abstraction)
- Validate artifact-target compatibility before flashing

D) Settings and safety
- Versioned settings schema migration
- Reset to defaults and import/export settings bundles
- Redaction policy for sensitive values in logs/exports

E) Packaging and compatibility
- Windows build remains first-class
- Add cross-platform path for macOS/Linux where possible
- Keep one-command build script for standalone binaries

Architecture constraints:
- Python + Tkinter allowed
- Optional small, justified deps are acceptable
- Thread-safe shared state
- UI must never freeze during transport operations

Required deliverables:
- complete source code
- module tree and transport abstraction diagram
- test suite for heartbeat parser, transport failover logic, and export serialization
- README.md, SETUP.md, TESTING.md, SECURITY.md, ASSUMPTIONS.md
- ACCEPTANCE_CHECKLIST.md and OPENSPEC_TRACEABILITY.md

Acceptance criteria:
- app can run without watch hardware in mock mode
- serial mode parses and renders heartbeat correctly
- BLE mode performs discovery/connect and receives equivalent status payloads (or documented simulation path)
- diagnostics export produces deterministic files
- UI remains responsive under high log volume
- profile switching and profile-aware parsing work for RP2040 and both ESP32-S3 prototype profiles

Decision rule:
If BLE stack details are OS-specific, implement a clean abstraction + at least one concrete path and one mock path; document assumptions clearly.
```

---

## 2.5) Prompt: SolWearOS ESP32-S3 Mini for 2 New Prototypes (E-Ink + TFT Buttons)

```text
You are a principal embedded engineer for ESP32-S3.
Create a new SolWearOS prototype branch targeting ESP32-S3 Mini hardware.
You do not have repository access. This prompt is the full specification.

Mission:
Port/adapt SolWearOS concepts from RP2040 to ESP32-S3 Mini and deliver two working firmware prototypes.

Target prototypes:
Prototype A:
- MCU: ESP32-S3 Mini
- Display: Waveshare e-ink module, 1.54-inch, 200x200
- Input: physical buttons (define minimal mapping)
- Goal: ultra-low-power watch-like UX and status dashboards

Prototype B:
- MCU: ESP32-S3 Mini
- Display: 1.3-inch TFT IPS, ST7789, 240x240
- Input: 4 physical buttons
- Goal: responsive app-switching UI and diagnostics controls

Primary objective:
Deliver a shared firmware architecture with hardware-specific HAL drivers and feature profiles so both prototypes can ship from one codebase.

Mandatory feature scope:
A) Platform architecture
- Build target structure for ESP32-S3 Mini using PlatformIO (or ESP-IDF-compatible structure if justified)
- Split into core, ui, apps, hal, assets, and config layers
- Shared app framework with compile-time and runtime capability flags

B) Display/input HAL
- E-ink driver path with partial/full refresh strategy and ghosting mitigation hooks
- ST7789 TFT driver path with optimized redraw regions
- Button input abstraction with debouncing and long-press/repeat support
- Unified input event model used by app framework

C) Core apps (prototype-ready)
- Home/launcher
- Stats/status
- Settings
- Health steps summary (mock-capable if sensor unavailable)
- Minimal wallet status screen (no seed exposure)

D) Power and reliability
- Deep sleep / low-power policy per prototype
- Wake source strategy (buttons/timer)
- Watchdog integration and crash-safe reboot reason logging
- Memory usage instrumentation and startup diagnostics

E) Connectivity and contracts
- USB serial logs and heartbeat compatibility with existing SolWear format:
  [STATUS] batt=<pct> volt=<volts> heap=<bytes> steps=<n> uptime=<sec> charging=<0|1>
- BLE telemetry/control channel scaffolding aligned with SolWear contracts

F) Security and data
- Persistent storage for settings and non-sensitive state
- Redaction policy for logs
- Never print mnemonic/private material in logs

Non-functional constraints:
- E-ink prototype must prioritize low refresh/power behavior
- TFT prototype must prioritize UI responsiveness
- No blocking loops in render/input critical paths
- Maintain deterministic state transitions under repeated button inputs

Required outputs:
- Full source code with board/display profile separation
- Hardware capability matrix
- Build/flash instructions for both prototypes
- Test plan (unit + hardware-in-the-loop checklist)
- README.md, ARCHITECTURE.md, SECURITY.md, ASSUMPTIONS.md, TESTING.md
- ACCEPTANCE_CHECKLIST.md and OPENSPEC_TRACEABILITY.md

Acceptance criteria:
- both ESP32-S3 prototype targets build successfully
- each prototype boots into a usable launcher/status flow
- buttons are stable and debounced
- heartbeat output is parseable by service tools
- e-ink refresh behavior and TFT redraw behavior are documented and validated

Decision policy:
If exact vendor driver details are missing, implement with the safest known library path and document assumptions without blocking delivery.
```

---

## 3) Prompt: SolWear Android App Phase-2 (Complete Partial Areas + Release Readiness)

```text
You are a principal Android engineer.
Continue SolWear Android app from a mostly implemented mock-first baseline.
No repo access is available. This prompt is the full source of truth.

Tech constraints:
- Kotlin, Coroutines, Flow
- Jetpack Compose
- Clean Architecture + MVVM + repositories
- Hilt DI
- Room
- DataStore
- WorkManager
- Secure storage with Android Keystore

Feature baseline already exists conceptually:
- device management screens
- health dashboards/history
- wallet create/send/receive/history/detail
- contract simulation/submission scaffolding
- NFC payload parser/tooling
- POS debug lab with failure injection
- diagnostics and settings

Known gaps to close in this phase:
1) Device management depth is partial
2) Wallet import/backup verification UX is partial
3) Offline queue replay policy is partial
4) Full compile/test verification blocked previously by environment issues

Primary objective:
Deliver a release-candidate architecture pass that closes partial features and upgrades test confidence.

Mandatory implementation scope:
A) Device management completion
- robust multi-device lifecycle (pair, switch active, remove, reconnect)
- richer firmware update UX (state machine, validation messaging)
- clearer sync-state and stale telemetry indicators

B) Wallet completion
- import flow with validation and explicit risk warnings
- backup phrase verification challenge flow
- biometric protection option for sensitive actions
- stronger anti-phishing cues for destination addresses

C) Sync engine completion
- queued writes with deterministic replay policy
- conflict resolution policy documented and implemented
- retry/backoff tuning + user-visible sync diagnostics

D) Testing and CI readiness
- unit tests for critical use cases and validators
- repository tests for replay/conflict paths
- view model tests for wallet and sync states
- at least core UI navigation/integration tests
- Gradle tasks documented for local and CI runs

E) Production hardening
- redact sensitive logs
- tighten error taxonomy and user-facing messaging
- enforce debug-vs-release guardrails for raw protocol views

Required outputs:
- complete project code
- architecture update
- migration notes
- README.md, SETUP.md, TESTING.md, SECURITY.md, ASSUMPTIONS.md
- ACCEPTANCE_CHECKLIST.md with done/partial/blocked mapped to files
- OPENSPEC_TRACEABILITY.md

Acceptance criteria:
- project compiles in standard Android environment
- all critical screens navigable
- wallet import + backup verification works end-to-end in mock mode
- queued sync replay behaves deterministically in tests
- test suite covers critical flows and passes

Decision policy:
If external integrations are unavailable, keep deterministic mock adapters and clearly define swap points for live infrastructure.
```

---

## 4) Prompt: SolWear Website (Brand + Product + Docs + Download Hub)

```text
You are a principal full-stack web engineer and product designer.
Create the first production-ready SolWear website.
No private repository access is available.

Website mission:
- Public identity for SolWear ecosystem
- Explain the 3 products: firmware, service tool, mobile app
- Provide docs, download links, release notes, and support channel pathways
- Be ready to add account/device cloud features later

Primary IA (information architecture):
1) Home
2) Products
3) Developers
4) Downloads
5) Security
6) Changelog / Releases
7) Contact / Support

Mandatory content sections:
A) Home
- Clear value proposition
- Visual explanation of watch + app + service tool integration
- CTA to download and developer docs

B) Products
- SolWearOS firmware overview
- Service Tool overview
- Mobile App overview
- capability matrix and roadmap status

C) Developers
- setup quickstarts
- API/protocol notes (heartbeat format and transport expectations)
- mock mode guidance

D) Downloads
- latest firmware package links
- service tool binaries
- mobile app build/install channel notes
- checksum display and integrity guidance

E) Security page
- wallet security principles
- disclosure policy
- privacy/log redaction statement

F) Releases page
- semantic versioned release notes
- known issues
- migration notes

Technical constraints:
- Use Next.js (App Router) + TypeScript
- modern responsive design, fast performance, accessible UI
- SEO-ready metadata and social cards
- MD/MDX content pipeline for docs and release notes
- analytics-ready abstraction (privacy conscious)

Design direction:
- premium, high-contrast, modern, trustworthy
- avoid generic template feel
- readable on mobile first

Deliverables:
- full web project code
- content model and route map
- reusable design system tokens/components
- sample release entries and docs pages
- test/lint/build setup
- deployment guide
- README.md, ARCHITECTURE.md, SECURITY.md, TESTING.md, ASSUMPTIONS.md
- ACCEPTANCE_CHECKLIST.md and OPENSPEC_TRACEABILITY.md

Acceptance criteria:
- site builds and runs locally
- all main routes functional
- docs/release content is editable via markdown/mdx
- downloads/security/release pages are complete and coherent
- Lighthouse-oriented performance and accessibility baseline is documented

Decision policy:
If real binary hosting is unavailable, implement a clear placeholder artifact registry structure and instructions to connect a real storage backend.
```

---

## 5) Prompt: End-to-End Integration Contract Pack (Optional but High Value)

```text
You are a systems integration architect.
Produce an integration contract pack that aligns firmware, service tool, mobile app, and website docs.
No repository access is available.

Goal:
Define stable contracts and versioning policy to reduce cross-team breakage.

Produce:
1) Status heartbeat spec v1 with grammar, examples, and compatibility policy
2) BLE transport message envelope spec (request/response/event)
3) Error code catalog shared across firmware/tool/mobile
4) Log redaction policy and examples
5) Contract test vectors and JSON fixtures
6) Migration/versioning strategy (v1 -> v2)
7) Conformance test plan for each client

Deliverables:
- CONTRACTS.md
- VERSIONING.md
- ERROR_CODES.md
- REDACTION_POLICY.md
- TEST_VECTORS/...
- ACCEPTANCE_CHECKLIST.md

Acceptance criteria:
- each contract has normative examples
- backward compatibility rules are explicit
- test vectors are machine-consumable
- each SolWear component has an implementation checklist
```

---

## Recommended Run Order

1) Shared Context Prompt
2) SolWearOS Firmware Phase-2
3) SolWearOS ESP32-S3 Mini prototypes
4) Service Tool v2
5) Android App Phase-2
6) Website project
7) Integration Contract Pack

This order prevents contract drift and keeps all teams building without blocking each other.
