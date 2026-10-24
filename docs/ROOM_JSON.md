# ROOM JSON (schematic / symbol)

Semantic JSON for **schematic** and **symbol** views. Intended for **git-friendly** storage and review.

## One file name, two encodings

The path is always `*.schematic.room` / `*.symbol.room`. Content is either Cap’n Proto binary or room-json text. Detect with `RoomJsonIo::isJsonRoom(path)` (file starts with `{`).

| Content | `isJsonRoom` | DOOR menu |
|---------|--------------|-----------|
| Cap’n Proto binary | false | **Convert to JSON** |
| room-json text | true | **Convert to ROOM** |

Convert **overwrites the same `*.room` path** in place. There is no live `*.symbol.json` / `*.schematic.json` twin for editors.

Editors:

- **Xschem** — `xschem_bridge` / `loadRoomOrJson` (already content-aware)
- **Qucs** — `loadRoomOrJson` on open / LibComp symbol pull-in / save keeps encoding via `saveViewWithJsonTwin`

Layout, abstract, and emmodel stay Cap’n Proto only.

## Tools

```text
room_to_json  <input.room>  <output.json>  [--cell name]
json_to_room  <input.json>  <output.room>
```

In-place API (`utils/room_json_io.{h,cpp}`):

```text
RoomJsonIo::isJsonRoom(path)
RoomJsonIo::convertInPlaceToJson(path)
RoomJsonIo::convertInPlaceToBinary(path)
RoomJsonIo::loadRoomOrJson(path, db)
```

## Document shape (outline)

```json
{
  "format": "room-json",
  "version": "1.0",
  "view": "schematic",
  "lib": "EMTest",
  "cell": "testbench",
  "dbuPerMicron": 1000,
  "dbuPerEditorUnit": 1,
  "layers": [ { "id": 0, "layerNum": 1, "dataType": 0, "name": "wire", "purpose": "wire" } ],
  "properties": [ { "name": "key", "value": "value" } ],
  "block": {
    "bbox": { "llx": 0, "lly": 0, "urx": 0, "ury": 0 },
    "shapes": [],
    "instances": [],
    "nets": []
  }
}
```

## Tests

| Test | What it checks |
|------|----------------|
| `room_json_roundtrip` | Synthetic schematic → JSON → ROOM + in-place convert / `isJsonRoom` |
| `qucs_room_json_roundtrip` | `rc_lowpass.sch` → ROOM → JSON → ROOM → Qucs |
