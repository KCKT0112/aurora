#include <aurora/keymap.h>
char user_key_ascii(user_keymap_t *s, uint16_t scan)
{
    if (scan & 0x100)
        return 0;
    uint8_t code = scan & 0x7f;
    bool released = (scan & 0x80) != 0;
    if (code == 42) {
        s->left_shift = !released;
        return 0;
    }
    if (code == 54) {
        s->right_shift = !released;
        return 0;
    }
    if (code == 58 && !released) {
        s->caps = !s->caps;
        return 0;
    }
    if (released)
        return 0;
    static const char keys[128] = {
        [2] = '1',  [3] = '2',   [4] = '3',  [5] = '4',  [6] = '5',  [7] = '6',  [8] = '7',
        [9] = '8',  [10] = '9',  [11] = '0', [12] = '-', [13] = '=', [16] = 'q', [17] = 'w',
        [18] = 'e', [19] = 'r',  [20] = 't', [21] = 'y', [22] = 'u', [23] = 'i', [24] = 'o',
        [25] = 'p', [26] = '[',  [27] = ']', [30] = 'a', [31] = 's', [32] = 'd', [33] = 'f',
        [34] = 'g', [35] = 'h',  [36] = 'j', [37] = 'k', [38] = 'l', [39] = ';', [40] = '\'',
        [41] = '`', [43] = '\\', [44] = 'z', [45] = 'x', [46] = 'c', [47] = 'v', [48] = 'b',
        [49] = 'n', [50] = 'm',  [51] = ',', [52] = '.', [53] = '/', [57] = ' '};
    char ch = keys[code];
    bool shift = s->left_shift || s->right_shift;
    if (ch >= 'a' && ch <= 'z') {
        if (shift != s->caps)
            ch -= 'a' - 'A';
    } else if (shift) {
        const char *plain = "1234567890-=[];'`,./\\", *upper = "!@#$%^&*()_+{}:\"~<>?|";
        for (unsigned i = 0; plain[i]; i++)
            if (ch == plain[i]) {
                ch = upper[i];
                break;
            }
    }
    return ch;
}
void user_erase(void *data, unsigned size)
{
    volatile unsigned char *p = data;
    while (size--)
        *p++ = 0;
}
