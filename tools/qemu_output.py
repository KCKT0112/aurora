"""Keep raw diagnostics; summarize only known unsupported AC97 recording voices."""
import re
CAPTURE_ONLY=re.compile(r"audio: Can not open `ac97\.(pi|mc)' \(no host audio driver\)")
def relay(stream,log,console):
    summarized=False
    for line in stream:
        log.write(line);log.flush()
        if CAPTURE_ONLY.search(line):
            if not summarized:
                console.write('AC97 recording is unavailable on this output-only backend; PCM playback is enabled. Details: qemu.log\n')
                console.flush();summarized=True
        else:
            console.write(line);console.flush()
