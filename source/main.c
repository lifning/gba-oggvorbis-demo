#include <gba.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "codeclib.h"
#include "cliff_ogg_bin.h"

typedef char sample_buffer_t[256];

short pcm16_buffer[sizeof(sample_buffer_t)];
// hack to get 8-bit pcm quickly from tremor's 16-bit little-endian output
u8* hecko = ((u8*)pcm16_buffer) + 1;

sample_buffer_t buffers[2];

int current_buffer = 0;
int current_section = 0;

volatile bool load_more;

typedef struct {
    const u8* data;
    u32 length;
    u32 position;
} my_vorbis_file;

my_vorbis_file g_my_vorb;
u32 g_scanlines_blank;

__attribute__((section(".iwram")))
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

__attribute__((section(".iwram")))
void isrVblank(void) {
    g_scanlines_blank += 228;
}

int main(void){
    consoleDemoInit();
    
    load_more = 0;
    g_scanlines_blank = 0;
    
    g_my_vorb.data = cliff_ogg_bin;
    g_my_vorb.length = cliff_ogg_bin_size;
    g_my_vorb.position = 0;

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
/*
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
*/    
    printf("Beginning playback\n");

    irqSet(IRQ_TIMER1, isrTimer1);
    irqSet(IRQ_VBLANK, isrVblank);
    irqEnable(IRQ_TIMER1);
    irqEnable(IRQ_VBLANK);

    REG_SOUNDCNT_H = SNDA_L_ENABLE | SNDA_R_ENABLE | SNDA_RESET_FIFO | SNDA_VOL_100;
    REG_SOUNDCNT_X = 0x80; // master enable

//    REG_TM0CNT_L = (u16)(65536 - (16777216 / vi->rate));
    REG_TM0CNT_H = TIMER_START;

    REG_TM1CNT_L = (u16)(65536 - sizeof(sample_buffer_t));
    REG_TM1CNT_H = TIMER_START | TIMER_IRQ | TIMER_COUNT;

    REG_DMA1SAD = (u32)buffers[0];
    REG_DMA1DAD = (u32)&REG_FIFO_A;
    REG_DMA1CNT = DMA_ENABLE | DMA_DST_FIXED | DMA_SPECIAL | DMA32 | DMA_REPEAT;
    
    u32 max = 0;
    while(1) {
        if (load_more) {
            u32 start = g_scanlines_blank + REG_VCOUNT;
  //          read_more_ogg(!current_buffer);
            load_more = 0;
            u32 duration = g_scanlines_blank + REG_VCOUNT - start;
            if (duration > max) {
                max = duration;
            }
            iprintf("\x1b[18;0Hdecode: %4ld lines (max %ld)\n", duration, max);
        }
    }
    return 0;
}
