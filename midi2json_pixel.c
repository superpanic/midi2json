#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#define DEBUG 0
#define TRACK_READ 0

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

typedef unsigned char  t_1byte;
typedef unsigned short t_2byte;
typedef unsigned int   t_4byte;
typedef unsigned long  t_8byte;

void die(const char *message);
void print_type_lengths();
void generate_frequencies(float *m, int len);

int get_16_step(float t);

unsigned int reverse_endian_int(unsigned int x);
unsigned short reverse_endian_short(unsigned short x);
unsigned int ticks_per_second(unsigned int ticks_per_beat, unsigned int beats_per_minute);
unsigned char get_low_bits(unsigned char c);
unsigned char get_high_bits(unsigned char c);

const int FILE_NAME_LEN = 128;

const int META_EVENT      = 0xFF;
const int SYSEX_EVENT     = 0xF0;
const int SYSEX_EVENT_END = 0xF7;
const int END_OF_TRACK    = 0x2F;
const int NOTE_ON         = 0x9;
const int NOTE_OFF 	  = 0x8;
const int INSTRUMENT_NAME  = 0x03;

const int BAR = 3840;
const int STEP = 240;    // 3840/16 = 240

float MIDI[127];

const char *FILE_HEADER = "MThd";
const char *TRACK_HEADER = "MTrk";

const int DELTA_TIME_MAX_BYTES = 4;

const int MIDI_EVENT_COMMAND_ARR[7] = {
	0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE
};


const char MIDI_EVENT_NAME_ARR[7][19] = {
	"Note OFF",
	"Note ON",
	"Note Aftertouch",
	"Controller",
	"Program Change",
	"Channel Aftertouch",
	"Pitch Bend"
};

const char MIDI_EVENT_LENGTH_ARR[7] = {
	2, 2, 2, 2, 1, 1, 2
};

const int META_EVENT_TYPE_ARR[15] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 
	0x05, 0x06, 0x07, 0x20, 0x2F, 
	0x51, 0x54, 0x58, 0x59, 0x7F
};

const int META_EVENT_LENGTH_ARR[15] = {
	 2, -1, -1, -1, -1,
	-1, -1, -1,  1,  0,
	 3,  5,  4,  2, -2
}; // -1 == string, -2 == variable length

const char META_EVENT_NAME_ARR[15][20] = {
	"Sequence Number",
	"Text Event",
	"Copyright Notice",
	"Sequence/Track Name",
	"Instrument Name",

	"Lyrics",
	"Marker",
	"Cue Point",
	"Midi Channel Prefix",
	"End of Track",

	"Set tempo",
	"SMPTE Offset",
	"Time Signature",
	"Key Signature",
	"Sequencer Specific"
};


int main(int argc, char *argv[]) {
	if (DEBUG) print_type_lengths();
	if (argc < 3) die("Please provide [FILENAME_IN] and [FILENAME_OUT].");
	
	char filename_in[FILE_NAME_LEN];
	strncpy(filename_in, argv[1], FILE_NAME_LEN);
	filename_in[FILE_NAME_LEN-1] = '\0';

	char filename_out[FILE_NAME_LEN];
	strncpy(filename_out, argv[2], FILE_NAME_LEN);
	filename_out[FILE_NAME_LEN-1] = '\0';

	generate_frequencies(MIDI, 128);

	FILE *file_ptr;
	printf("Opening file %s\n", filename_in);

	file_ptr = fopen(filename_in, "rb");
	if(file_ptr == NULL) die("File not found.");
	printf("File \"%s\" open for reading.\n", filename_in);

	FILE *file_write_ptr;
	file_write_ptr = fopen(filename_out, "w");
	if(file_write_ptr == NULL) die("Failed to create new file.");
	printf("Created file \"%s\" for output.\n", filename_out);
		
	// read file header MThd
	int read_len = 4;
	t_1byte file_header[read_len];
	fread(file_header, sizeof(file_header[0]), read_len, file_ptr);
	bool is_a_midi_file = true;
	for(int i=0; i<read_len; i++) {
		printf("%c", file_header[i]);
		if(FILE_HEADER[i] != file_header[i]) {
			is_a_midi_file = false;
		}
	}
	printf("\n");
	if (!is_a_midi_file) die("Not a midi-file.");
	printf("Midi file header signature ok.\n");

	// read the file header size (big endian)
	printf("Header info:\n");

	t_4byte file_header_size;
	fread(&file_header_size, sizeof(t_4byte), 1, file_ptr);
	file_header_size = reverse_endian_int(file_header_size);
	printf("\tFile header size: %u\n", file_header_size);

	t_2byte file_format;
	fread(&file_format, sizeof(file_format), 1, file_ptr);
	file_format = reverse_endian_short(file_format);
	if(file_format == 0) {
		printf("\tFile format: 0 (single track)\n");
	} else if(file_format == 1) {
		printf("\tFile format: 1 (multiple tracks)\n");
	} else if(file_format == 2) {
		printf("\tFile format: 2 (independent tracks)\n");
	} else {
		die("Ending program, unknown Midi-file format: %u");
	}

	t_2byte number_of_tracks;
	fread(&number_of_tracks, sizeof(number_of_tracks), 1, file_ptr);
	number_of_tracks = reverse_endian_short(number_of_tracks);
	printf("\tNumber of tracks: %u\n", number_of_tracks);

	t_2byte delta_time_ticks;
	fread(&delta_time_ticks, sizeof(delta_time_ticks), 1, file_ptr);
	delta_time_ticks = reverse_endian_short(delta_time_ticks);
	printf("\tDelta time ticks: %u\n", delta_time_ticks);

	printf("\tTicks per second: %u\n", ticks_per_second(delta_time_ticks, 60));
	
	fprintf(file_write_ptr, "{\n\t\"info\": \"converted using midi2json by superpanic, https://github.com/superpanic\",\n");
	fprintf(file_write_ptr, "\t\"name\": \"%s\",\n", filename_in);
	fprintf(file_write_ptr, "\t\"patterns\": [\n");

	int absolute_track_time = 0;

	// read midi tracks
	for(int track = 0; track < number_of_tracks; track++) {
		read_len = 4;
		t_1byte track_header[read_len + 1];
		fread(track_header, sizeof(track_header[0]), read_len, file_ptr);
		bool is_a_midi_track = true;
		for(int i=0; i<read_len; i++) {
			//printf("%c", track_header[i]);
			if(TRACK_HEADER[i] != track_header[i]) {
				is_a_midi_track = false;
			}
		}
		track_header[read_len] = '\0';
		
		if (!is_a_midi_track) die("Could not find midi-track.");
		if(DEBUG) printf("\tMidi track header signature: %s\n",track_header);

		t_4byte number_of_events;
		fread(&number_of_events, sizeof(number_of_events), 1, file_ptr);
		number_of_events = reverse_endian_int(number_of_events);
		if(DEBUG) printf("\tTrack length: %u\n", number_of_events);

		bool is_first_midi_event = true; // used below for json padding chars before the midi events loop
		bool is_first_note = true;
		absolute_track_time = 0;

		// start of json file
		fprintf(file_write_ptr, "\t\t{");
		fprintf(file_write_ptr, "\n\t\t\t\"track number\":%u,\n", track);

		int step = 0;

		// ### TRACK 0 IS SPECIAL, AND CONTAINS GLOBAL SETUP INFO! ###
		if(track==0) fprintf(file_write_ptr, "\t\t\t\"className\": \"%s\",\n", "SETUP");
		// event loop:
		for(int event = 0; event < number_of_events; event++) {
			// reading delta time	
			t_1byte delta_time_buffer[DELTA_TIME_MAX_BYTES];
			t_1byte delta_time_byte=0xFF;
			int byte_counter = 0;
			while(delta_time_byte>=0x80) {
				if( fread(&delta_time_byte, sizeof(delta_time_byte), 1, file_ptr) == EOF ) die("Reached end of file.");
				if( byte_counter >= DELTA_TIME_MAX_BYTES ) die("Delta time byte count read exceeds limit.");
				delta_time_buffer[byte_counter] = delta_time_byte;
				byte_counter++;
			}
			
			// calculate delta time
			int delta_time_value = 0;
			for(int i = 0; i<byte_counter; i++) {
				int dt = delta_time_buffer[i] & 127; // set msb to 0
				delta_time_value = delta_time_value << 7; // leave room for 7 bits
				delta_time_value = delta_time_value + dt; // add new value
			}
			
			// read until command type is found
			t_1byte command_byte = 0;
			int jump_byte_counter = 0;
			while( command_byte < 0x80) {
				if(fread(&command_byte, sizeof(command_byte), 1, file_ptr) == EOF) die("Reached end of file.");
				jump_byte_counter++;
			}
			if(jump_byte_counter > 1) printf("\tFast forward %u bytes.\n", jump_byte_counter-1);
			
			if(command_byte == META_EVENT) {
				t_1byte meta_event_type;
				fread(&meta_event_type, sizeof(meta_event_type), 1, file_ptr);
				bool event_is_unknown_type = true;
				int meta_event_length = 0;
				for(int i = 0; i<15; i++) {
					if(meta_event_type == META_EVENT_TYPE_ARR[i]) {
						if(track==TRACK_READ) {
							printf("\tMeta command found at track: %i, event %i: %s\n", track, event, META_EVENT_NAME_ARR[i]);
							printf("\tLength: %d\n", META_EVENT_LENGTH_ARR[i]);
						}
						meta_event_length = META_EVENT_LENGTH_ARR[i];
						event_is_unknown_type = false;
						break;
					}
				}
				if(event_is_unknown_type) {
					printf("Unknown meta event found: %i\n", meta_event_type);
					die("Unknown Midi Meta event type:");
				}
				if(meta_event_type == END_OF_TRACK) {
					t_1byte jump_data_len_byte;
					fread(&jump_data_len_byte, sizeof(jump_data_len_byte), 1, file_ptr);
					if(jump_data_len_byte != 0) die("End of track event has data length > 0. Should be 0. Exiting program.");
					
					if(track>0) { // track 0 do not contain notes
						if(step<16) fprintf(file_write_ptr, ",\n");
						while(step<16) {
							fprintf(file_write_ptr, "\t\t\t\t{\"on\": 0, \"key\": \"\", \"step\": \"%i\"}", step+1);
							if(step<15) fprintf(file_write_ptr, ",");
							if(step<15) fprintf(file_write_ptr, "\n");
							step++;
						}
					}

					if(is_first_note) { // no notes
						fprintf(file_write_ptr, "\t\t\t\"steps\":[]\n\t\t},\n");
						break;
					} else if(track >= number_of_tracks-1) {
						fprintf(file_write_ptr, "\n\t\t\t]\n\t\t}\n\t]\n}");
						goto quit;
					} else {
						fprintf(file_write_ptr, "\n\t\t\t]\n\t\t},\n");						
						break;
					}
					
				}
				
				if(meta_event_length == -1) {
					// undefined length string
					t_1byte string_length;
					fread(&string_length, sizeof(string_length), 1, file_ptr);
					t_1byte string_buffer[string_length+1];
					fread(string_buffer, sizeof(string_buffer[0]), string_length, file_ptr);
					string_buffer[string_length] = '\0';
					if(track==TRACK_READ) printf("\t%s\n", string_buffer);
					if(meta_event_type == INSTRUMENT_NAME) {
						fprintf(file_write_ptr, "\t\t\t\"className\": \"%s\",\n", string_buffer);
					}
				} else if(meta_event_length == -2) {
					// variable length
					t_1byte meta_event_data_len;
					fread(&meta_event_data_len, sizeof(meta_event_data_len), 1, file_ptr);
					t_1byte jump_bytes[meta_event_data_len];
					fread(jump_bytes, sizeof(jump_bytes[0]), meta_event_data_len, file_ptr);
				} else {
					t_1byte meta_event_data_len;
					fread(&meta_event_data_len, sizeof(meta_event_data_len), 1, file_ptr);
					t_1byte meta_event_value[meta_event_length];
					fread(meta_event_value, sizeof(meta_event_value[0]), meta_event_length, file_ptr);
				}

			} else if(command_byte == SYSEX_EVENT) {
				printf("\tSYSEX EVENT\n\tjumping forward to end of event.\n");
				t_1byte jump_byte=0;
				while( jump_byte != SYSEX_EVENT_END ) {
					if( fread(&jump_byte, sizeof(jump_byte), 1, file_ptr) == EOF ) die("Searched for end of SysEx event, but reached end of file.");
				}

			} else if(track>0) { // TRACK 0 IS SPECIAL AND SHOULD NOT CONTAIN ANY ACTUAL NOTES

				if(is_first_note) {
					fprintf(file_write_ptr, "\t\t\t\"steps\":[\n");
					is_first_note = false;
				}

				int midi_command = get_high_bits(command_byte);
				int midi_channel = get_low_bits(command_byte);
				if(midi_channel>=16) die("Read midi event with channel above limit 16.");

				bool event_is_unknown_type = true;
				int midi_data_len = 0;
				//int midi_event_number = 0;
				for(int i=0; i<7; i++) {
					if(midi_command == MIDI_EVENT_COMMAND_ARR[i]) {
						midi_data_len = MIDI_EVENT_LENGTH_ARR[i];
						//midi_event_number = i;
						event_is_unknown_type = false;
					}
				}

				if(event_is_unknown_type) die("Unknown midi event type.");
				
				t_1byte midi_data[midi_data_len];
				fread(midi_data, sizeof(midi_data[0]), midi_data_len, file_ptr);

				absolute_track_time = absolute_track_time + delta_time_value;
				if(midi_command == NOTE_ON) {
					if(!is_first_midi_event) fprintf(file_write_ptr, ",\n");
					else is_first_midi_event = false;
					step++;
					int current_step = absolute_track_time/STEP+1;
					while(step<current_step) {
						//fprintf(file_write_ptr, "\t\t\t\t{\"on\": 0, \"key\": \"\", \"step\": \"%i\"},\n", step);
						//TODO: is this edit correct?
						fprintf(file_write_ptr, "\t\t\t\t{\"on\": 1, \"key\": \"\", \"step\": \"%i\"},\n", step);
						step++;
					}
					fprintf(file_write_ptr, "\t\t\t\t{\"on\": 1, \"key\": \"%u\", \"step\": \"%i\"}", (unsigned char)midi_data[0], current_step);
					

/*
					fprintf(file_write_ptr, "\t\t\t\t{\"on\": 1, \"n\":%u, \"delta-time\":%u, \"absolute-time\":%u, \"step\":%u, \"f\":%f, \"v\":%u}", 
						MIDI_EVENT_NAME_ARR[midi_event_number], 
						(unsigned char)midi_data[0], 
						delta_time_value, 
						absolute_track_time,
						current_step,
						MIDI[(unsigned char)midi_data[0]], 
						(unsigned char)midi_data[1]
					);
*/

					step = current_step;
				}

			}

		} // end event for loop

	} // end track for loop

	quit:
		fclose(file_write_ptr);
		fclose(file_ptr);
		die("End of program.");
		return 0;
}

unsigned char get_low_bits(unsigned char c) {
	return c & 0x0F;
}

unsigned char get_high_bits(unsigned char c) {
	return c >> 4;
}

unsigned short reverse_endian_short(unsigned short x) {
	return (
		( (x >> 8) & 0x00ff ) | 
		( (x << 8) & 0xff00 )
	);
}

unsigned int reverse_endian_int(unsigned int x) {
	return (
		( (x >> 24) & 0x000000ff ) | 
		( (x >>  8) & 0x0000ff00 ) | 
		( (x <<  8) & 0x00ff0000 ) | 
		( (x << 24) & 0xff000000 )
	);
}

void die(const char *message) {
	if (errno) {
		perror(message);
	} else {
		printf("PROGRAM END: %s\n", message);
	}
	exit(errno);
}

void print_type_lengths() {
	/* print some byte lengths of types */
	printf("================\n");
	printf("  %lu bytes (unsigned char)  : t_1byte\n", sizeof(t_1byte));
	printf("  %lu bytes (unsigned short) : t_2byte\n", sizeof(t_2byte));
	printf("  %lu bytes (unsigned int)   : t_4byte\n", sizeof(t_4byte));
	printf("  %lu bytes (unsigned long)  : t_8byte\n", sizeof(t_8byte));
	printf("================\n");
}

void generate_frequencies(float *mi, int len) {
	float C1 = 8.1757989156; // first C frequency
	mi[0] = C1;
	for (int x = 1; x < len; x++) {
		mi[x] = mi[x-1] * 1.05946309436; // twelfth root of 2
	}
}

unsigned int ticks_per_second(unsigned int ticks_per_beat, unsigned int beats_per_minute) {
	int ticks_per_minute = beats_per_minute * ticks_per_beat;
	int ticks_per_second = ticks_per_minute / 60;
	return ticks_per_second;
}

int get_16_step(float t) {
	return 0;
}
