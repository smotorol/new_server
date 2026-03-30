# Unity Dummy Client First Path

## Purpose
Source-only reconnect/mobile session test client.

## Current state
- scaffold exists under `tools/DummyClientUnity`
- transport/header helper exists
- reconnect controller exists
- first-path state fields are prepared for login -> world list -> world select -> character list -> character select -> enter world
- protobuf-generated C# output is shared from `tools/proto_generated/csharp`

## Limitation in this repository state
The Unity project intentionally does not check in `Google.Protobuf.dll` or generated Unity build artifacts.
Because of that, the repository contains the source/scaffold and activation path, but not a fully build-verified Unity runtime package binding.

## Activation steps
1. Open `tools/DummyClientUnity` in Unity Hub.
2. Add a Unity-compatible `Google.Protobuf` runtime package or DLL reference.
3. Add/link generated C# files from `tools/proto_generated/csharp` into the Unity project.
4. Enable the first-path flow scripts and test connect -> login -> world list -> world select.

## Immediate next step
Complete runtime package wiring, then extend to character list/select and world enter verification.
