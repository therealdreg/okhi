The web page is NOT here any more.

index.html.gz is now embedded directly in the application binary. It is
written to main/index.html.gz by webusb/webgen.js and linked in by
EMBED_FILES in main/CMakeLists.txt, so the page and the firmware that
serves it can never end up out of sync, and serving it no longer disables
the flash cache the way a SPIFFS read does.

This partition still exists, and it is still needed: it is where an
incoming RP firmware image is staged during an over the air update, as
/spiffs/rp.new and then /spiffs/rp.bin.

Nothing is shipped in it. This file only keeps the directory non-empty so
spiffs_create_partition_image() always has something to build from.

If you find an index.html.gz next to this file, it is a leftover from
before the change. Delete it: it is dead weight that nothing reads.
