/* Ogginfo
 *
 * A tool to describe ogg file contents and metadata.
 *
 * This file handles codecs we have no specific handling for.
 *
 * Copyright 2002-2005 Michael Smith <msmith@xiph.org>
 * Copyright 2020      Philipp Schafft <lion@lion.leolix.org>
 * Licensed under the GNU GPL, distributed with this program.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

#include <ogg/ogg.h>

#include "i18n.h"

#include "private.h"

typedef struct {
    bool seen_streaminfo;
    bool seen_data;
    bool headers_done;
    ogg_int64_t bytes;
    ogg_int64_t rate;
    ogg_int64_t lastgranulepos;
} misc_flac_info;

static inline ogg_int64_t read_intNbe(const unsigned char *in, int bits, int offset)
{
    ogg_int64_t ret = 0;
    int have = 0;

    ret = *(in++);
    have = 8;

    if (offset) {
        ret = ((ret << offset) & 0xFF) >> offset;
        have -= offset;
    }

    while (have < bits) {
        ret <<= 8;
        ret |= *(in++);
        have += 8;
    }

    ret >>= (have - bits);

    return ret;
}

static void flac_process_streaminfo(stream_processor *stream, misc_flac_info *self, ogg_packet *packet)
{
    self->rate = read_intNbe(&(packet->packet[27]), 20, 0);

    info(_("Channels: %d\n"), (long int)read_intNbe(&(packet->packet[29]), 3, 4) + 1);
    info(_("Bist per sample: %d\n"), (long int)read_intNbe(&(packet->packet[29]), 5, 7) + 1);
    info(_("Rate: %ld\n\n"), (long int)self->rate);

    self->seen_streaminfo = true;
}

static void flac_process_padding(stream_processor *stream, misc_flac_info *self, ogg_packet *packet)
{
    info(_("Padding: %ld bytes\n"), (long int)(packet->bytes - 4));
}

static inline const char * application_type_name(ogg_int64_t type)
{
    return "<unknown>";
}

static void flac_process_application(stream_processor *stream, misc_flac_info *self, ogg_packet *packet)
{
    ogg_int64_t type = read_intNbe(&(packet->packet[4]), 32, 0);
    info(_("Application data: %d (%s)\n"), (int)type, application_type_name(type));
}

static void flac_process_vorbis_comments(stream_processor *stream, misc_flac_info *self, ogg_packet *packet)
{
    size_t end;

    if (packet->bytes > 4) {
        if (handle_vorbis_comments(stream, packet->packet + 4, packet->bytes - 4, &end) == -1) {
            warn(_("WARNING: invalid Vorbis comments on stream %d: packet too short\n"), stream->num);
        }
    } else {
        warn(_("WARNING: invalid Vorbis comments on stream %d: packet too short\n"), stream->num);
    }
}

static inline const char * picture_type_name(ogg_int64_t type)
{
    switch (type) {
        case 0:
            return "Other";
            break;
        case 1:
            return "32x32 pixels file icon (PNG)";
            break;
        case 2:
            return "Other file icon";
            break;
        case 3:
            return "Cover (front)";
            break;
        case 4:
            return "Cover (back)";
            break;
        case 5:
            return "Leaflet page";
            break;
        case 6:
            return "Media";
            break;
        case 7:
            return "Lead artist/lead performer/soloist";
            break;
        case 8:
            return "Artist/performer";
            break;
        case 9:
            return "Conductor";
            break;
        case 10:
            return "Band/Orchestra";
            break;
        case 11:
            return "Composer";
            break;
        case 12:
            return "Lyricist/text writer";
            break;
        case 13:
            return "Recording Location";
            break;
        case 14:
            return "During recording";
            break;
        case 15:
            return "During performance";
            break;
        case 16:
            return "Movie/video screen capture";
            break;
        case 17:
            return "A bright coloured fish";
            break;
        case 18:
            return "Illustration";
            break;
        case 19:
            return "Band/artist logotype";
            break;
        case 20:
            return "Publisher/Studio logotype";
            break;
        default:
            return "<unknown>";
            break;
    }
}

static inline char * read_string(const unsigned char *in, size_t *len)
{
    char *ret;

    *len = read_intNbe(in, 32, 0);

    ret = malloc((*len) + 1);
    if (!ret)
        return NULL;

    memcpy(ret, in + 4, *len);
    ret[*len] = 0;

    *len += 4;

    return ret;
} 

static void flac_process_picture(stream_processor *stream, misc_flac_info *self, ogg_packet *packet)
{
    ogg_int64_t type = read_intNbe(&(packet->packet[4]), 32, 0);
    ogg_int64_t picture_length;
    size_t offset = 8;
    size_t len;
    char *str;
    bool is_url;

    info(_("Picture: %d (%s)\n"), (int)type, picture_type_name(type));

    str = read_string(&(packet->packet[offset]), &len);
    offset += len;
    if (strcmp(str, "-->") == 0) {
        is_url = true;
    } else {
        info(_("\tMIME-Type: %s\n"), str);
        is_url = false;
    }
    free(str);

    str = read_string(&(packet->packet[offset]), &len);
    offset += len;
    if (*str)
        info(_("\tDescription: %s\n"), str);
    free(str);

    info(_("\tWidth: %ld\n"), (long int)read_intNbe(&(packet->packet[offset]), 32, 0));
    info(_("\tHeight: %ld\n"), (long int)read_intNbe(&(packet->packet[offset + 4]), 32, 0));
    info(_("\tColor depth: %ld\n"), (long int)read_intNbe(&(packet->packet[offset + 8]), 32, 0));
    if (read_intNbe(&(packet->packet[offset + 12]), 32, 0))
        info(_("\tUsed colors: %ld\n"), (long int)read_intNbe(&(packet->packet[offset + 12]), 32, 0));

    picture_length = read_intNbe(&(packet->packet[offset + 16]), 32, 0);
    if (is_url) {
        str = read_string(&(packet->packet[offset + 16]), &len);
        info(_("\tURL: %s\n"), str);
        free(str);
    } else {
        info(_("\tSize: %ld bytes\n"), (long int)picture_length);
    }
}

static void flac_process_data(stream_processor *stream, misc_flac_info *self, ogg_packet *packet)
{
    if (packet->granulepos != -1)
        self->lastgranulepos = packet->granulepos;
    self->seen_data = true;
}

static void flac_process(stream_processor *stream, ogg_page *page)
{
    misc_flac_info *self = stream->data;

    ogg_stream_pagein(&stream->os, page);

    while (1) {
        ogg_packet packet;
        int res = ogg_stream_packetout(&stream->os, &packet);

        if (res < 0) {
           warn(_("WARNING: discontinuity in stream (%d)\n"), stream->num);
           continue;
        } else if (res == 0) {
            break;
        }

        if (packet.bytes < 1) {
            warn(_("WARNING: Invalid zero size packet in stream (%d)\n"), stream->num);
            break;
        }

        if (packet.packetno == 0) {
            flac_process_streaminfo(stream, self, &packet);
        } else if (!self->headers_done) {
            switch (packet.packet[0] & 0x7F) {
                case 1:
                    flac_process_padding(stream, self, &packet);
                    break;
                case 2:
                    flac_process_application(stream, self, &packet);
                    break;
                case 3:
                    /* no-op: seek table */
                    break;
                case 4:
                    flac_process_vorbis_comments(stream, self, &packet);
                    break;
                case 5:
                    /* no-op: cue sheet */
                    break;
                case 6:
                    flac_process_picture(stream, self, &packet);
                    break;
                default:
                    warn(_("WARNING: Invalid header of type %d in stream (%d)\n"), (int)(packet.packet[0] & 0x7F), stream->num);
                    break;
            }

            if (packet.packet[0] & 0x80)
                self->headers_done = true;
        } else {
            flac_process_data(stream, self, &packet);
        }
    }

    if (self->headers_done)
        self->bytes += page->header_len + page->body_len;
}

static void flac_end(stream_processor *stream)
{
    misc_flac_info *self = stream->data;
    long minutes, seconds, milliseconds;
    double bitrate, time;

    /* This should be lastgranulepos - startgranulepos, or something like that*/
    time = (double)self->lastgranulepos / self->rate;
    minutes = (long)time / 60;
    seconds = (long)time - minutes*60;
    milliseconds = (long)((time - minutes*60 - seconds)*1000);
    bitrate = self->bytes*8 / time / 1000.0;

    info(_("FLAC stream %d:\n"
           "\tTotal data length: %" PRId64 " bytes\n"
           "\tPlayback length: %ldm:%02ld.%03lds\n"
           "\tAverage bitrate: %f kb/s\n"),
            stream->num, self->bytes, minutes, seconds, milliseconds, bitrate);

    if (!self->seen_streaminfo)
        warn(_("WARNING: stream (%d) did not contain STREAMINFO\n"), stream->num);

    if (!self->seen_data)
        warn(_("WARNING: stream (%d) did not contain data packets\n"), stream->num);

    free(stream->data);
}

void flac_start(stream_processor *stream)
{
    stream->type = "FLAC";
    stream->process_page = flac_process;
    stream->process_end = flac_end;
    stream->data = calloc(1, sizeof(misc_flac_info));
}
