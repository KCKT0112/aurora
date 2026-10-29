#include <aurora/keymap.h>
#include <assert.h>
#include <stdio.h>
int main(void)
{
    user_keymap_t k={0};assert(user_key_ascii(&k,30)=='a');
    assert(user_key_ascii(&k,42)==0);assert(user_key_ascii(&k,30)=='A');
    assert(user_key_ascii(&k,2)=='!');assert(user_key_ascii(&k,12)=='_');
    assert(user_key_ascii(&k,0x80|42)==0);assert(user_key_ascii(&k,2)=='1');
    user_key_ascii(&k,58);assert(user_key_ascii(&k,30)=='A');
    user_key_ascii(&k,54);assert(user_key_ascii(&k,30)=='a');
    assert(user_key_ascii(&k,0x153)==0);assert(user_key_ascii(&k,0x80|30)==0);
    puts("keymap: shift, caps lock, symbols and release handling passed");
}
