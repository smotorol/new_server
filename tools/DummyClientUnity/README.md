# DummyClientUnity

Source-only Unity project for reconnect/session recovery tests.

## Included
- simple TCP wrapper
- runtime state holder
- reconnect test controller
- first-path flow scaffold for connect/login/world list/select/character select/world enter activation

## Not included
- Library/
- Temp/
- obj/
- Build/
- checked-in protobuf runtime binaries

## Current limitation
The project intentionally does not commit `Google.Protobuf.dll` or other generated binary artifacts.
To activate the protobuf first-path runtime flow, add a Unity-compatible protobuf runtime package and wire the generated C# files from `tools/proto_generated/csharp` into the Unity project.
