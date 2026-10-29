#ifndef AURORA_DISPLAY_PROTOCOL_H
#define AURORA_DISPLAY_PROTOCOL_H
#include <stdbool.h>
#include <stdint.h>
#define DISPLAY_PACKET_SIZE 12u
typedef struct { uint8_t bytes[DISPLAY_PACKET_SIZE]; unsigned used; } display_parser_t;
bool display_mode_valid(uint32_t width,uint32_t height);
bool display_packet_byte(display_parser_t *parser,uint8_t byte,uint16_t *width,uint16_t *height);
#endif
