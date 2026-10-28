#!/usr/bin/env python3
"""Check the Windows application manifest, and check that it is what's in the exe.

Why this exists: the first real Windows run of VLCFlix Studio failed before main()
with "The application has failed to start because its side-by-side configuration is
incorrect" (ERROR_SXS_CANT_GEN_ACTCTX). The manifest carried a `description`
attribute on <assemblyIdentity>, which is not in Microsoft's manifest schema; the
side-by-side parser rejects the entire manifest for it and the process never runs.
Nothing on this machine could see that: Wine does not validate manifests, and a
cross build does not either.

So the rules are enforced here instead, and both the Windows build and
tools/preflight.sh run this script:

  1. the file is well-formed XML;
  2. the root is <assembly xmlns="urn:schemas-microsoft-com:asm.v1"
     manifestVersion="1.0">;
  3. every <assemblyIdentity> uses only attributes the schema defines
     (type, name, version, processorArchitecture, publicKeyToken, language) - this
     is the exact rule the old manifest broke;
  4. there are no <dependency> elements: nothing in the app imports comctl32, and a
     dependent assembly is the only thing side-by-side has to resolve, so leaving the
     list empty removes the failure mode rather than trusting it;
  5. <description> is a child element, and <requestedExecutionLevel> is asInvoker;
  6. a comment contains no double hyphen (also illegal in XML, and silently fatal
     for Windows' parser);
  7. the manifest is under 64 KB, which is the documented manifest resource limit;
  8. when an executable is given, the manifest bytes are embedded in it exactly once
     and byte for byte - a stale or duplicated resource fails the build.

Usage:
  check-win-manifest.py <app.manifest> [exe-to-inspect ...]

Exit code 0 only when every rule holds.
"""

import re
import sys
import xml.etree.ElementTree as ET

ASM_V1 = "urn:schemas-microsoft-com:asm.v1"
IDENTITY_ATTRIBUTES = {
    "type",
    "name",
    "version",
    "processorArchitecture",
    "publicKeyToken",
    "language",
}

failures = []


def local(element):
    """Element name without its namespace: trustInfo lives in asm.v3, identity in asm.v1."""
    return element.tag.rsplit("}", 1)[-1]


def find_all(root, name):
    return [element for element in root.iter() if local(element) == name]


def bad(message):
    failures.append(message)


def check_manifest(path):
    with open(path, "rb") as handle:
        raw = handle.read()

    if len(raw) > 64 * 1024:
        bad(f"{path}: {len(raw)} bytes is over the 64 KB manifest limit")

    try:
        root = ET.fromstring(raw)
    except ET.ParseError as error:
        bad(f"{path}: not well-formed XML: {error}")
        return raw

    if root.tag != f"{{{ASM_V1}}}assembly":
        bad(f"{path}: root element is <{root.tag}>, expected <assembly> in {ASM_V1}")
    if root.get("manifestVersion") != "1.0":
        bad(f"{path}: manifestVersion is {root.get('manifestVersion')!r}, expected '1.0'")

    identities = find_all(root, "assemblyIdentity")
    seen_identity = False
    for identity in identities:
        seen_identity = True
        unknown = set(identity.attrib) - IDENTITY_ATTRIBUTES
        if unknown:
            bad(
                f"{path}: <assemblyIdentity> carries attribute(s) that Microsoft's schema "
                f"does not define: {', '.join(sorted(unknown))} - this is what makes "
                f"Windows refuse to start the process with a side-by-side error"
            )
        if not identity.get("name") or not identity.get("version") or not identity.get("type"):
            bad(f"{path}: <assemblyIdentity> needs type, name and version")
    if not seen_identity:
        bad(f"{path}: no <assemblyIdentity> - Windows will not treat this as an application")

    descriptions = find_all(root, "description")
    if not descriptions:
        bad(f"{path}: <description> is a child element of <assembly>, and is missing")
    elif not (descriptions[0].text or "").strip():
        bad(f"{path}: <description> is empty")

    dependencies = find_all(root, "dependency")
    if dependencies:
        bad(
            f"{path}: {len(dependencies)} <dependency> element(s). Nothing here imports "
            f"comctl32, so a dependent assembly only gives side-by-side something to fail on"
        )

    level = None
    for element in find_all(root, "requestedExecutionLevel"):
        level = element.get("level")
    if level != "asInvoker":
        bad(f"{path}: requestedExecutionLevel is {level!r}, expected 'asInvoker'")

    for comment in re.findall(rb"<!--(.*?)-->", raw, re.S):
        if b"--" in comment:
            bad(f"{path}: a comment contains a double hyphen, which XML forbids")

    return raw


def check_embedded(exe, manifest_bytes):
    with open(exe, "rb") as handle:
        image = handle.read()
    count = image.count(manifest_bytes)
    if count == 0:
        bad(f"{exe}: does not contain the manifest from app.manifest")
    elif count > 1:
        bad(f"{exe}: contains the manifest {count} times - duplicate manifest resources "
            f"confuse side-by-side resolution")
    else:
        print(f"  ok    {exe}: one embedded manifest, byte for byte")


def main(argv):
    if len(argv) < 2:
        print(__doc__.strip().splitlines()[0])
        return 2
    manifest_path = argv[1]
    raw = check_manifest(manifest_path)
    if raw:
        print(f"  ok    {manifest_path}: schema-clean, no dependent assemblies")
    for exe in argv[2:]:
        check_embedded(exe, raw)

    if failures:
        print()
        for message in failures:
            print(f"FAIL  {message}")
        print(f"\n{len(failures)} manifest problem(s)")
        return 1
    print("manifest: 0 problem(s)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
