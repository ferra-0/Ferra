const { spawnSync } = require("child_process");
const fs = require("fs");
const path = require("path");
const vscode = require("vscode");
const { LanguageClient } = require("vscode-languageclient/node");

const clients = [];
let pythonLauncherAvailable;

function canUsePythonLauncher() {
  if (pythonLauncherAvailable === undefined) {
    const result = spawnSync("py", ["-3", "--version"], {
      stdio: "ignore",
      windowsHide: true,
    });
    pythonLauncherAvailable = !result.error && result.status === 0;
  }
  return pythonLauncherAvailable;
}

function hasExplicitPythonPath(configuration) {
  const setting = configuration.inspect("pythonPath");
  if (!setting) return false;
  return [
    setting.globalValue,
    setting.workspaceValue,
    setting.workspaceFolderValue,
    setting.globalLanguageValue,
    setting.workspaceLanguageValue,
    setting.workspaceFolderLanguageValue,
  ].some((value) => value !== undefined);
}

function resolvePythonCommand(configuration, python, serverPath) {
  if (
    process.platform === "win32" &&
    python === "python" &&
    !hasExplicitPythonPath(configuration) &&
    canUsePythonLauncher()
  ) {
    return { command: "py", args: ["-3", serverPath] };
  }
  return { command: python, args: [serverPath] };
}
function serverEnvironment(context) {
  const env = { ...process.env };
  try {
    const rootFile = context.asAbsolutePath(
      path.join("server", "ferra-root.txt")
    );
    const ferraRoot = fs.readFileSync(rootFile, "utf8").trim();
    if (ferraRoot) env.FERRA_PATH = ferraRoot;
  } catch {
    // Keep an explicitly configured FERRA_PATH when an older extension has no metadata.
  }
  return env;
}


function startLanguageClient(context, options) {
  const fallbackPython = vscode.workspace
    .getConfiguration("ferra.lsp")
    .get("pythonPath", "python3");
  const configuration = vscode.workspace.getConfiguration(options.configuration);
  const python = configuration.get("pythonPath", fallbackPython);
  const serverPath = context.asAbsolutePath(
    path.join("server", options.server)
  );
  const server = resolvePythonCommand(configuration, python, serverPath);
  const watcher = vscode.workspace.createFileSystemWatcher(options.files);
  context.subscriptions.push(watcher);

  const client = new LanguageClient(
    options.id,
    options.name,
    {
      command: server.command,
      args: server.args,
      options: { env: serverEnvironment(context) },
    },
    {
      documentSelector: [
        { scheme: "file", language: options.language },
        { scheme: "untitled", language: options.language },
      ],
      synchronize: { fileEvents: watcher },
      outputChannelName: options.name,
    }
  );

  clients.push(client);
  try {
    const startup = client.start();
    if (startup && typeof startup.catch === "function") {
      startup.catch((error) => {
        const detail = error && error.message ? `: ${error.message}` : "";
        vscode.window.showErrorMessage(
          `${options.name} could not start${detail}. Configure ${options.configuration}.pythonPath to a Python 3 executable.`
        );
      });
    }
  } catch (error) {
    const detail = error && error.message ? `: ${error.message}` : "";
    vscode.window.showErrorMessage(
      `${options.name} could not start${detail}. Configure ${options.configuration}.pythonPath to a Python 3 executable.`
    );
  }
}

function activate(context) {
  startLanguageClient(context, {
    id: "ferraLanguageServer",
    name: "Ferra Language Server",
    configuration: "ferra.lsp",
    server: "ferra_lsp.py",
    files: "**/*.fe",
    language: "ferra",
  });
  startLanguageClient(context, {
    id: "eferraLanguageServer",
    name: "eFerra Language Server",
    configuration: "eferra.lsp",
    server: "eferra_lsp.py",
    files: "**/*.efe",
    language: "eferra",
  });
}

async function deactivate() {
  await Promise.all(clients.map((client) => client.stop()));
}

module.exports = { activate, deactivate };
