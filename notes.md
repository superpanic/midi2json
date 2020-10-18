Exports midi events from midi file to json in the following format:
{ 
	"loadmessage": "# loaded music.json",
	"description": "# music.json - converted notes generated from midi file: Look to your Orb.mid",
	"keys":"[c] command, [n] midi-note, [d] delta-time, [f] frequency, [v] velocity",
	"title":"main track",
	"loop":true,
	"delay": 2.0, 
	"notes":[
		{"c":"Note OFF", "n":41, "d":120, "f":87.307076, "v":64},
		{"c":"Note ON", "n":41, "d":0, "f":87.307076, "v":80},
		{"c":"Note OFF", "n":41, "d":120, "f":87.307076, "v":64},
		{"c":"Note ON", "n":43, "d":0, "f":97.998878, "v":93}
	]
}