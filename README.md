# midi2json
Simple C tool for converting notes in a midi-file to json.

Each read note (on) and (off) looks like this when converted to json:

`{"c":"Note ON", "n":41, "d":0, "f":87.307076, "v":80},  `  
`{"c":"Note OFF", "n":41, "d":120, "f":87.307076, "v":64},`

Used for personal project, not tested on a wide variety of midi-files, but should handle type 1 (single track) and type 2 (multi track) midi-files.

usage:
`midi2json [FILENAME_IN] [FILENAME_OUT]`
