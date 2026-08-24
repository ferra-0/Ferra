#!/usr/bin/env python3
"""Create an offline-installable VS Code VSIX from a prepared extension."""

from __future__ import annotations

import json
import pathlib
import shutil
import sys
import tempfile
import zipfile


CONTENT_TYPES = """<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="json" ContentType="application/json" />
  <Default Extension="js" ContentType="application/javascript" />
  <Default Extension="py" ContentType="text/x-python" />
  <Default Extension="png" ContentType="image/png" />
  <Default Extension="md" ContentType="text/markdown" />
  <Default Extension="txt" ContentType="text/plain" />
  <Default Extension="vsixmanifest" ContentType="text/xml" />
</Types>
"""


def xml_escape(value: str) -> str:
    return (
        value.replace("&", "&amp;")
        .replace('"', "&quot;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
    )


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: make-vsix.py EXTENSION_DIR OUTPUT.vsix", file=sys.stderr)
        return 2

    extension_dir = pathlib.Path(sys.argv[1]).resolve()
    output_path = pathlib.Path(sys.argv[2]).resolve()
    manifest_path = extension_dir / "package.json"
    if not manifest_path.is_file():
        raise SystemExit(f"missing extension manifest: {manifest_path}")

    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    for field in ("name", "publisher", "version", "displayName", "engines"):
        if field not in manifest:
            raise SystemExit(f"extension manifest is missing '{field}'")
    vscode_engine = manifest["engines"].get("vscode")
    if not vscode_engine:
        raise SystemExit("extension manifest is missing 'engines.vscode'")

    identity = {
        key: xml_escape(str(manifest[key]))
        for key in ("name", "publisher", "version", "displayName")
    }
    engine = xml_escape(str(vscode_engine))
    vsix_manifest = f"""<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="{identity['name']}" Version="{identity['version']}" Publisher="{identity['publisher']}" />
    <DisplayName>{identity['displayName']}</DisplayName>
    <Description xml:space="preserve">Ferra and eFerra language support</Description>
    <Categories>Programming Languages</Categories>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="{engine}" />
      <Property Id="Microsoft.VisualStudio.Services.Content.Pricing" Value="Free" />
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
  </Assets>
</PackageManifest>
"""

    output_path.parent.mkdir(parents=True, exist_ok=True)
    if output_path.exists():
        output_path.unlink()

    with tempfile.TemporaryDirectory(prefix="ferra-vsix-") as temp_dir_name:
        temp_dir = pathlib.Path(temp_dir_name)
        shutil.copytree(extension_dir, temp_dir / "extension")
        (temp_dir / "[Content_Types].xml").write_text(
            CONTENT_TYPES, encoding="utf-8", newline="\n"
        )
        (temp_dir / "extension.vsixmanifest").write_text(
            vsix_manifest, encoding="utf-8", newline="\n"
        )

        with zipfile.ZipFile(
            output_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9
        ) as archive:
            for path in sorted(temp_dir.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(temp_dir).as_posix())

    print(output_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
