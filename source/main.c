#include <gba.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "tremor/ivorbisfile.h"
#include "cliff_ogg_bin.h"

typedef char sample_buffer_t[256];

short pcm16_buffer[sizeof(sample_buffer_t)];
// hack to get 8-bit pcm quickly from tremor's 16-bit little-endian output
u8* hecko = ((u8*)pcm16_buffer) + 1;

sample_buffer_t buffers[2];

int current_buffer = 0;
int current_section = 0;

volatile bool load_more;

OggVorbis_File vf;

typedef struct {
    const u8* data;
    u32 length;
    u32 position;
} my_vorbis_file;

my_vorbis_file g_my_vorb;

size_t vorbis_read_cb(void *ptr, size_t size, size_t nmemb, void *datasource) {
    my_vorbis_file* my_vorb = (my_vorbis_file*)datasource;

    s32 remaining = (s32)(my_vorb->length) - (s32)(my_vorb->position);
    if (remaining <= 0) {
        return 0;
    }
    size_t len = size * nmemb;
    if (len > remaining) {
        len = remaining;
    }
    memcpy(ptr, my_vorb->data + my_vorb->position, len);
    my_vorb->position += len;

    return len;
}

int vorbis_seek_cb(void *datasource, ogg_int64_t offset, int whence) {
    my_vorbis_file* my_vorb = (my_vorbis_file*)datasource;
    if (whence == SEEK_SET) {
        my_vorb->position = (u32)offset;
    } else if (whence == SEEK_CUR) {
        my_vorb->position += (int)offset;
    } else if (whence == SEEK_END) {
        my_vorb->position = my_vorb->length + (int)offset;
    } else {
        return -1;
    }
    return 0;
}

int vorbis_close_cb(void *datasource) {
    return 0;
}

long int vorbis_tell_cb(void *datasource) {
    my_vorbis_file* my_vorb = (my_vorbis_file*)datasource;
    return my_vorb->position;
}

void isrTimer1(void) {
    if(g_my_vorb.position >= g_my_vorb.length){
        printf("Done playing.\n");
        REG_TM0CNT_H = 0;
        REG_TM1CNT_H = 0;
        REG_DMA1CNT = 0;
    } else {
        REG_DMA1CNT = 0; // disable so we can change source address
                    
        // no-op to let DMA registers catch up
        asm volatile ("eor r0, r0; eor r0, r0" ::: "r0");
        
        current_buffer = !current_buffer;
        REG_DMA1SAD = (u32)buffers[current_buffer];
        REG_DMA1CNT = DMA_ENABLE | DMA_DST_FIXED | DMA_SPECIAL | DMA32 | DMA_REPEAT;
        load_more = 1;
    }
    REG_IF |= REG_IF;
}

static inline void read_more_ogg(int which_buffer) {
    ov_read(&vf, pcm16_buffer, sizeof(pcm16_buffer), &current_section);
    for (int i = 0; i < sizeof(sample_buffer_t); ++i) {
        buffers[which_buffer][i] = hecko[i<<1];
    }
}

int main(void){
    consoleDemoInit();
    
    load_more = 0;
    
    g_my_vorb.data = cliff_ogg_bin;
    g_my_vorb.length = cliff_ogg_bin_size;
    g_my_vorb.position = 0;

    ov_callbacks ovcb = {
        .read_func = &vorbis_read_cb,
        .seek_func = &vorbis_seek_cb,
        .close_func = &vorbis_close_cb,
        .tell_func = &vorbis_tell_cb,
    };
    
    printf(
        "Ogg/Vorbis playback demo\n"
        "\n"
        " Hand-optimized version of\n"
        " Xiph.Org libtremor for ARM:\n"
        " wss.co.uk/pinknoise/tremolo\n"
        "\n"
        " Music is 'Anodyne - Cliff'\n"
        " by: htch.bandcamp.com\n"
        "\n"
        "(GBA demo by lifning)\n"
    );

    irqInit();

    if (ov_open_callbacks(&g_my_vorb, &vf, NULL, 0, ovcb) < 0) {
        printf("\nError opening vorbis data.\n");
        while(1) VBlankIntrWait();
    }

    printf("\nOpened successfully:\n");
    printf(" encoder: %s\n", ov_comment(&vf, -1)->vendor);

    vorbis_info *vi = ov_info(&vf, -1);
    printf(" %d channel, %ldHz\n", vi->channels, vi->rate);

    u32 samples = ov_pcm_total(&vf, -1);
    printf(" %ld samples\n", samples);

    printf("Reading first buffers...\n");
    read_more_ogg(0);
    read_more_ogg(1);
    
    printf("Beginning playback\n");

    irqSet(IRQ_TIMER1, isrTimer1);
    irqEnable(IRQ_TIMER1);

    REG_SOUNDCNT_H = SNDA_L_ENABLE | SNDA_R_ENABLE | SNDA_RESET_FIFO | SNDA_VOL_100;
    REG_SOUNDCNT_X = 0x80; // master enable

    REG_TM0CNT_L = (u16)(65536 - (16777216 / vi->rate));
    REG_TM0CNT_H = TIMER_START;

    REG_TM1CNT_L = (u16)(65536 - sizeof(sample_buffer_t));
    REG_TM1CNT_H = TIMER_START | TIMER_IRQ | TIMER_COUNT;

    REG_DMA1SAD = (u32)buffers[0];
    REG_DMA1DAD = (u32)&REG_FIFO_A;
    REG_DMA1CNT = DMA_ENABLE | DMA_DST_FIXED | DMA_SPECIAL | DMA32 | DMA_REPEAT;
    
    while(1) {
        if (load_more) {
            read_more_ogg(!current_buffer);
            load_more = 0;
        }
    }
    return 0;
}
