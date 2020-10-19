# midi2json
Simple C tool for reading notes from a midi-file and writing them to a json-file.

Each read note (on) and (off) looks like this when converted to json:

`{"c":"Note ON", "n":41, "d":0, "f":87.307076, "v":80},  `  
`{"c":"Note OFF", "n":41, "d":120, "f":87.307076, "v":64},`

[c] command  
[n] midi-note  
[d] delta-time  
[f] frequency  
[v] velocity

Used for personal project, not tested on a wide variety of midi-files, but should handle type 0 (single track) and type 1 (multi track) midi-files. Not tested on type 2.

usage:  
`midi2json [FILENAME_IN] [FILENAME_OUT]`

Free to use, modify and/or include in any personal or commercial project.
