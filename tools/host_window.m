/* Startup-only screen probe. Never observes QEMU windows or emits resize loops. */
#import <Cocoa/Cocoa.h>
int main(int argc,const char **argv)
{
    @autoreleasepool {
        if(argc!=2 || strcmp(argv[1],"--screen"))return 2;
        NSScreen *screen=[NSScreen mainScreen];
        if(!screen)return 1;
        NSRect frame=screen.frame;
        CGFloat scale=screen.backingScaleFactor;
        int width=(int)llround(frame.size.width*scale),height=(int)llround(frame.size.height*scale);
        if(width<=0 || height<=0)return 1;
        printf("%d %d\n",width,height);
    }
    return 0;
}
