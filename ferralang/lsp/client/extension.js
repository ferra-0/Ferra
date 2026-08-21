const path = require("path");
const vscode = require("vscode");
const { LanguageClient } = require("vscode-languageclient/node");

const clients = [];

function startLanguageClient(context, options) {
  const fallbackPython = vscode.workspace
    .getConfiguration("ferra.lsp")
    .get("pythonPath", "python3");
  const python = vscode.workspace
    .getConfiguration(options.configuration)
    .get("pythonPath", fallbackPython);
  const serverPath = context.asAbsolutePath(
    path.join("server", options.server)
  );
  const watcher = vscode.workspace.createFileSystemWatcher(options.files);
  context.subscriptions.push(watcher);

  const client = new LanguageClient(
    options.id,
    options.name,
    {
      command: python,
      args: [serverPath],
      options: { env: { ...process.env } },
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
  client.start();
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
