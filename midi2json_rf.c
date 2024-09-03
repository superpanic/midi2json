#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stdbool.h>

#define DEBUG 0
#define TRACK_READ 0

#define MIN(X, Y) (((X) < (Y)) ? (X) : (Y))
#define MAX(X, Y) (((X) > (Y)) ? (X) : (Y))

typedef unsigned char  t_1byte;
typedef unsigned short t_2byte;
typedef unsigned int   t_4byte;

void die(const char *message);
void print_type_lengths();
void generate_frequencies(float *m, int len);

unsigned int reverse_endian_int(unsigned int x);
unsigned short reverse_endian_short(unsigned short x);
unsigned int ticks_per_second(unsigned int ticks_per_beat, unsigned int beats_per_minute);
unsigned char get_low_bits(unsigned char c);
unsigned char get_high_bits(unsigned char c);

const int FILE_NAME_LEN = 128;

const int META_EVENT = 0xFF;
const int SYSEX_EVENT = 0xF0;
const int SYSEX_EVENT_END = 0xF7;
const int END_OF_TRACK = 0x2F;
const int NOTE_ON = 0x9;
const int INSTRUMENT_NAME = 0x03;

const int BAR = 3840;
const int STEP = 240;    // 3840/16 = 240

float MIDI[127];

const char *FILE_HEADER = "MThd";
const char *TRACK_HEADER = "MTrk";

const int DELTA_TIME_MAX_BYTES = 4;

const int MIDI_EVENT_COMMAND_ARR[7] = { 0x8, 0x9, 0xA, 0xB, 0xC, 0xD, 0xE };

const char MIDI_EVENT_NAME_ARR[7][19] = {
    "Note OFF", "Note ON", "Note Aftertouch", "Controller", 
    "Program Change", "Channel Aftertouch", "Pitch Bend"
};

const char MIDI_EVENT_LENGTH_ARR[7] = { 2, 2, 2, 2, 1, 1, 2 };

const int META_EVENT_TYPE_ARR[15] = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 
    0x20, 0x2F, 0x51, 0x54, 0x58, 0x59, 0x7F
};

const int META_EVENT_LENGTH_ARR[15] = {
    2, -1, -1, -1, -1, -1, -1, -1, 
    1, 0, 3, 5, 4, 2, -2
}; // -1 == string, -2 == variable length

const char META_EVENT_NAME_ARR[15][20] = {
    "Sequence Number", "Text Event", "Copyright Notice", 
    "Sequence/Track Name", "Instrument Name", "Lyrics", 
    "Marker", "Cue Point", "Midi Channel Prefix", "End of Track", 
    "Set tempo", "SMPTE Offset", "Time Signature", 
    "Key Signature", "Sequencer Specific"
};

int main(int argc, char *argv[]) {
    if (DEBUG) print_type_lengths();
    if (argc < 3) die("Please provide [FILENAME_IN] and [FILENAME_OUT].");

    char filename_in[FILE_NAME_LEN];
    char filename_out[FILE_NAME_LEN];
    strncpy(filename_in, argv[1], FILE_NAME_LEN);
    strncpy(filename_out, argv[2], FILE_NAME_LEN);

    generate_frequencies(MIDI, 128);

    FILE *file_ptr = fopen(filename_in, "rb");
    if (!file_ptr) die("File not found.");
    printf("File \"%s\" open for reading.\n", filename_in);

    FILE *file_write_ptr = fopen(filename_out, "w");
    if (!file_write_ptr) die("Failed to create new file.");
    printf("Created file \"%s\" for output.\n", filename_out);

    // Read file header
    t_1byte file_header[4];
    fread(file_header, sizeof(t_1byte), 4, file_ptr);
    if (strncmp((char *)file_header, FILE_HEADER, 4) != 0) {
        die("Not a midi-file.");
    }
    printf("Midi file header signature ok.\n");

    // Read file header size, format, tracks, and delta time ticks
    t_4byte file_header_size;
    t_2byte file_format, number_of_tracks, delta_time_ticks;

    fread(&file_header_size, sizeof(t_4byte), 1, file_ptr);
    file_header_size = reverse_endian_int(file_header_size);

    fread(&file_format, sizeof(t_2byte), 1, file_ptr);
    file_format = reverse_endian_short(file_format);

    fread(&number_of_tracks, sizeof(t_2byte), 1, file_ptr);
    number_of_tracks = reverse_endian_short(number_of_tracks);

    fread(&delta_time_ticks, sizeof(t_2byte), 1, file_ptr);
    delta_time_ticks = reverse_endian_short(delta_time_ticks);

    printf("Header info:\n");
    printf("\tFile header size: %u\n", file_header_size);
    printf("\tFile format: %u\n", file_format);
    printf("\tNumber of tracks: %u\n", number_of_tracks);
    printf("\tDelta time ticks: %u\n", delta_time_ticks);
    printf("\tTicks per second: %u\n", ticks_per_second(delta_time_ticks, 60));

    fprintf(file_write_ptr, "{\n\t\"info\": \"converted using midi2json by superpanic, https://github.com/superpanic\",\n");
    fprintf(file_write_ptr, "\t\"name\": \"%s\",\n", filename_in);
    fprintf(file_write_ptr, "\t\"patterns\": [\n");

    int absolute_track_time = 0;

    // Read each track
    for (int track = 0; track < number_of_tracks; track++) {
        // Read track header
        t_1byte track_header[4];
        fread(track_header, sizeof(t_1byte), 4, file_ptr);
        if (strncmp((char *)track_header, TRACK_HEADER, 4) != 0) {
            die("Could not find midi-track.");
        }

        t_4byte number_of_events;
        fread(&number_of_events, sizeof(t_4byte), 1, file_ptr);
        number_of_events = reverse_endian_int(number_of_events);

        absolute_track_time = 0;

        // Start of JSON output for this track
        fprintf(file_write_ptr, "\t\t{");
        fprintf(file_write_ptr, "\n\t\t\t\"track number\":%u,\n", track);
        if (track == 0) fprintf(file_write_ptr, "\t\t\t\"className\": \"%s\",\n", "SETUP");

        int step = 0;
        bool is_first_note = true;

        // Process events in the track
        for (int event = 0; event < number_of_events; event++) {
            // Read delta time
            t_1byte delta_time_buffer[DELTA_TIME_MAX_BYTES];
            int delta_time_value = 0;
            int byte_counter = 0;

            while (true) {
                fread(&delta_time_buffer[byte_counter], sizeof(t_1byte), 1, file_ptr);
                delta_time_value = (delta_time_value << 7) | (delta_time_buffer[byte_counter] & 0x7F);
                if (!(delta_time_buffer[byte_counter] & 0x80)) break;
                byte_counter++;
            }

            // Read event type
            t_1byte command_byte;
            fread(&command_byte, sizeof(t_1byte), 1, file_ptr);

            if (command_byte == META_EVENT) {
                t_1byte meta_event_type, meta_event_data_len;
                fread(&meta_event_type, sizeof(t_1byte), 1, file_ptr);

                int meta_event_length = 0;
                for (int i = 0; i < 15; i++) {
                    if (meta_event_type == META_EVENT_TYPE_ARR[i]) {
                        meta_event_length = META_EVENT_LENGTH_ARR[i];
                        break;
                    }
                }

                if (meta_event_type == END_OF_TRACK) {
                    fread(&meta_event_data_len, sizeof(t_1byte), 1, file_ptr);
                    if (meta_event_data_len != 0) die("End of track event has data length > 0.");

                    if (track > 0) {
                        if (step < 16) fprintf(file_write_ptr, ",\n");
                        while (step < 16) {
                            fprintf(file_write_ptr, "\t\t\t\t{\"on\": 0, \"key\": \"\", \"step\": \"%i\"}", step + 1);
                            if (step < 15) fprintf(file_write_ptr, ",\n");
                            step++;
                        }
                    }

                    if (is_first_note) {
                        fprintf(file_write_ptr, "\t\t\t\"steps\":[]\n\t\t},\n");
                    } else if (track >= number_of_tracks - 1) {
                        fprintf(file_write_ptr, "\n\t\t\t]\n\t\t}\n\t]\n}");
                        goto quit;
                    } else {
                        fprintf(file_write_ptr, "\n\t\t\t]\n\t\t},\n");
                    }
                    break;
                }

                // Handle other meta events
                if (meta_event_length == -1) {
                    fread(&meta_event_data_len, sizeof(t_1byte), 1, file_ptr);
                    char string_buffer[meta_event_data_len + 1];
                    fread(string_buffer, sizeof(char), meta_event_data_len, file_ptr);
                    string_buffer[meta_event_data_len] = '\0';
                    if (meta_event_type == INSTRUMENT_NAME) {
                        fprintf(file_write_ptr, "\t\t\t\"className\": \"%s\",\n", string_buffer);
                    }
                } else if (meta_event_length > 0) {
                    fread(&meta_event_data_len, sizeof(t_1byte), 1, file_ptr);
                    t_1byte meta_event_value[meta_event_length];
                    fread(meta_event_value, sizeof(t_1byte), meta_event_length, file_ptr);
                }
            } else if (track > 0 && get_high_bits(command_byte) == NOTE_ON) {
                if (is_first_note) {
                    fprintf(file_write_ptr, "\t\t\t\"steps\":[\n");
                    is_first_note = false;
                }

                t_1byte midi_data[2];
                fread(midi_data, sizeof(t_1byte), 2, file_ptr);

                absolute_track_time += delta_time_value;
                step++;

                int current_step = absolute_track_time / STEP + 1;
                while (step < current_step) {
                    fprintf(file_write_ptr, "\t\t\t\t{\"on\": 0, \"key\": \"\", \"step\": \"%i\"},\n", step);
                    step++;
                }
                fprintf(file_write_ptr, "\t\t\t\t{\"on\": 1, \"key\": \"%u\", \"step\": \"%i\"}", (unsigned char)midi_data[0], current_step);
                step = current_step;
            }
        } // end event loop
    } // end track loop

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
    return (x >> 8) | (x << 8);
}

unsigned int reverse_endian_int(unsigned int x) {
    return (x >> 24) | ((x >> 8) & 0x0000FF00) | ((x << 8) & 0x00FF0000) | (x << 24);
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
    printf("================\n");
    printf("  %lu bytes (unsigned char)  : t_1byte\n", sizeof(t_1byte));
    printf("  %lu bytes (unsigned short) : t_2byte\n", sizeof(t_2byte));
    printf("  %lu bytes (unsigned int)   : t_4byte\n", sizeof(t_4byte));
    printf("================\n");
}

void generate_frequencies(float *mi, int len) {
    float C1 = 8.1757989156; // first C frequency
    mi[0] = C1;
    for (int x = 1; x < len; x++) {
        mi[x] = mi[x - 1] * 1.05946309436; // twelfth root of 2
    }
}

unsigned int ticks_per_second(unsigned int ticks_per_beat, unsigned int beats_per_minute) {
    return (ticks_per_beat * beats_per_minute) / 60;
}
