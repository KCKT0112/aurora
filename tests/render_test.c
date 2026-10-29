#include <ui/draw.h>
#include <ui/font.h>
#include <ui/vector.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIDTH 80
#define HEIGHT 64
static uint32_t pixels[WIDTH*HEIGHT],output[WIDTH*HEIGHT];
int main(int argc,char **argv)
{
    assert(argc==2);
    ui_surface_t s={.pixels=pixels,.width=40,.height=32,.stride=80,.scale=2,.red_shift=16,.green_shift=8};
    ui_surface_reset_clip(&s);
    ui_fill_rect(&s,0,0,40,32,255,255,255,255);
    ui_surface_set_clip(&s,4,4,20,20);
    ui_fill_round_rect(&s,2,2,28,28,8,0,0,0,255);
    assert(pixels[7*80+8]==0xffffff);assert(pixels[30*80+30]==0);
    assert(pixels[48*80+30]==0xffffff);
    unsigned partial=0;for(unsigned i=0;i<WIDTH*HEIGHT;i++)if(pixels[i]!=0 && pixels[i]!=0xffffff)partial++;
    assert(partial>0); /* true edge coverage, not a nearest-neighbour enlarged 1x bitmap */
    ui_surface_t dst=s;dst.pixels=output;
    memset(output,0x55,sizeof(output));ui_present_region(&dst,&s,4,4,20,20);
    assert(output[30*80+30]==0);assert(output[7*80+8]==0x55555555);
    ui_surface_reset_clip(&s);
    ui_icon_draw(&s,UI_ICON_FOLDER,5,5,22,0x226644);
    ui_shadow(&s,2,2,30,25,6,true);
    FILE *f=fopen(argv[1],"rb");assert(f);fseek(f,0,SEEK_END);size_t size=(size_t)ftell(f);rewind(f);
    void *data=malloc(size);assert(data && fread(data,1,size,f)==size);fclose(f);
    ui_font_t font;assert(ui_font_init(&font,data,size));font.scale=2;
    assert(ui_font_text_width(&font,13,400,"Hi")>0);
    ui_font_draw(&font,&s,13,400,0,0,"Hi",20,40,50,255);
    free(data);puts("render: HiDPI clipping, presentation, antialiased corners, vectors and fonts passed");
}
