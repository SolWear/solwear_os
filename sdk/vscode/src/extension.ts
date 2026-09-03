import { existsSync } from "node:fs";
import { isAbsolute, join, resolve } from "node:path";
import * as vscode from "vscode";

type SolwearCommand = "build" | "run" | "package" | "doctor";

interface SolwearTaskDefinition extends vscode.TaskDefinition {
  type: "solwear";
  command: SolwearCommand;
  args?: string[];
}

interface CliInvocation {
  executable: string;
  prefix: string[];
  description: string;
}

const TASK_SOURCE = "SolWear";
const TASK_TYPE = "solwear";
const COMMANDS: readonly SolwearCommand[] = ["build", "run", "package", "doctor"];

export function activate(context: vscode.ExtensionContext): void {
  const provider = new SolwearTaskProvider(context.extensionUri);
  context.subscriptions.push(
    vscode.tasks.registerTaskProvider(TASK_TYPE, provider),
    vscode.commands.registerCommand("solwear.new", () => newProject(context.extensionUri)),
    ...COMMANDS.map((command) =>
      vscode.commands.registerCommand(`solwear.${command}`, () => runCommand(context.extensionUri, command)),
    ),
  );
}

export function deactivate(): void {
  // VS Code owns task processes and prompts before terminating a running task.
}

class SolwearTaskProvider implements vscode.TaskProvider {
  constructor(private readonly extensionUri: vscode.Uri) {}

  provideTasks(): vscode.Task[] {
    const folders = vscode.workspace.workspaceFolders ?? [];
    return folders.flatMap((folder) => COMMANDS.map((command) => createTask(this.extensionUri, folder, command)));
  }

  resolveTask(task: vscode.Task): vscode.Task | undefined {
    const definition = task.definition as SolwearTaskDefinition;
    if (!isCommand(definition.command)) return undefined;
    const folder = isWorkspaceFolder(task.scope) ? task.scope : pickWorkspaceFolder(false);
    if (!folder) return undefined;
    return createTask(this.extensionUri, folder, definition.command, definition.args ?? []);
  }
}

async function newProject(extensionUri: vscode.Uri): Promise<void> {
  const folder = pickWorkspaceFolder(true);
  if (!folder) return;
  const name = await vscode.window.showInputBox({
    title: "Create a SolWear project",
    prompt: "Project directory name",
    placeHolder: "my-watchface",
    validateInput: (value) => value.trim() ? undefined : "Enter a project name.",
  });
  if (!name) return;
  const template = await vscode.window.showQuickPick(
    [
      { label: "Watchface", value: "watchface", description: "Clock-focused, power-efficient surface" },
      { label: "App", value: "app", description: "General adaptive SolWear application" },
      { label: "Signer", value: "signer", description: "Wallet confirmation flow example" },
    ],
    { title: "Select a SolWear template" },
  );
  if (!template) return;

  const task = createTask(extensionUri, folder, "build", [], {
    label: `SolWear: New ${name}`,
    cliCommand: "new",
    cliArgs: [name.trim(), "--template", template.value],
  });
  await vscode.tasks.executeTask(task);
}

async function runCommand(extensionUri: vscode.Uri, command: SolwearCommand): Promise<void> {
  const folder = pickWorkspaceFolder(true);
  if (!folder) return;
  const args = command === "run" ? ["--profile", configuredProfile(folder)] : [];
  await vscode.tasks.executeTask(createTask(extensionUri, folder, command, args));
}

function createTask(
  extensionUri: vscode.Uri,
  folder: vscode.WorkspaceFolder,
  command: SolwearCommand,
  args: string[] = [],
  override?: { label: string; cliCommand: string; cliArgs: string[] },
): vscode.Task {
  const invocation = resolveCli(extensionUri, folder);
  const cliCommand = override?.cliCommand ?? command;
  const cliArgs = override?.cliArgs ?? args;
  const definition: SolwearTaskDefinition = { type: TASK_TYPE, command, args };
  const execution = new vscode.ProcessExecution(
    invocation.executable,
    [...invocation.prefix, cliCommand, ...cliArgs],
    { cwd: folder.uri.fsPath },
  );
  const task = new vscode.Task(
    definition,
    folder,
    override?.label ?? `SolWear: ${title(command)}`,
    TASK_SOURCE,
    execution,
    command === "build" ? ["$solwear"] : [],
  );
  task.detail = `${invocation.description} ${cliCommand} ${cliArgs.join(" ")}`.trim();
  task.presentationOptions = {
    reveal: vscode.TaskRevealKind.Always,
    panel: command === "run" ? vscode.TaskPanelKind.Dedicated : vscode.TaskPanelKind.Shared,
    clear: command === "run",
    showReuseMessage: false,
  };
  if (command === "build") task.group = vscode.TaskGroup.Build;
  return task;
}

function resolveCli(extensionUri: vscode.Uri, folder: vscode.WorkspaceFolder): CliInvocation {
  const configured = vscode.workspace.getConfiguration("solwear", folder.uri).get<string>("cliPath", "").trim();
  if (configured) {
    const path = isAbsolute(configured) ? configured : resolve(folder.uri.fsPath, configured);
    return invocationForPath(path, "configured SolWear CLI");
  }

  const candidates = [
    join(folder.uri.fsPath, "sdk", "cli", "dist", "bin.js"),
    vscode.Uri.joinPath(extensionUri, "..", "cli", "dist", "bin.js").fsPath,
  ];
  const local = candidates.find(existsSync);
  if (local) return invocationForPath(local, "workspace SolWear CLI");
  return { executable: "solwear", prefix: [], description: "solwear from PATH" };
}

function invocationForPath(path: string, description: string): CliInvocation {
  if (/\.[cm]?js$/i.test(path)) return { executable: "node", prefix: [path], description };
  return { executable: path, prefix: [], description };
}

function configuredProfile(folder: vscode.WorkspaceFolder): string {
  return vscode.workspace.getConfiguration("solwear", folder.uri).get<string>("defaultProfile", "pi-round-480");
}

function pickWorkspaceFolder(showError: boolean): vscode.WorkspaceFolder | undefined {
  const active = vscode.window.activeTextEditor?.document.uri;
  const folder = active ? vscode.workspace.getWorkspaceFolder(active) : vscode.workspace.workspaceFolders?.[0];
  if (!folder && showError) void vscode.window.showErrorMessage("Open a folder containing a SolWear app first.");
  return folder;
}

function isCommand(value: unknown): value is SolwearCommand {
  return typeof value === "string" && (COMMANDS as readonly string[]).includes(value);
}

function isWorkspaceFolder(value: vscode.WorkspaceFolder | vscode.TaskScope | undefined): value is vscode.WorkspaceFolder {
  return typeof value === "object" && value !== null && "uri" in value && "index" in value;
}

function title(command: SolwearCommand): string {
  return command === "run" ? "Run in Emulator" : command[0].toUpperCase() + command.slice(1);
}
