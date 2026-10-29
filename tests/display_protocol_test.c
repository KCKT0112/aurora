#include <aurora/display_protocol.h>
#include <assert.h>
#include <stdio.h>
int main(void)
{
    display_parser_t p={0};uint16_t w=0,h=0;
    uint16_t a=2880,b=1800,seq=42,crc=a^b^seq^0xa75a;
    uint8_t data[12]={'A','R','Z','1',a,a>>8,b,b>>8,seq,seq>>8,crc,crc>>8};
    assert(!display_packet_byte(&p,0xff,&w,&h));
    for(unsigned i=0;i<11;i++)assert(!display_packet_byte(&p,data[i],&w,&h));
    assert(display_packet_byte(&p,data[11],&w,&h) && w==2880 && h==1800);
    data[11]^=1;for(unsigned i=0;i<12;i++)assert(!display_packet_byte(&p,data[i],&w,&h));
    assert(!display_mode_valid(65528,65534));assert(!display_mode_valid(0,0));assert(display_mode_valid(3840,2160));
    puts("display transport: fragmented packet, bounds and checksum passed");
}
