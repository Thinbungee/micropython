import gc
import uos
import samd
from flashbdev import FlashBdev

flashfs = False
# Try to mount an external flash drive.
try:
    if hasattr(samd, "QSPIflash"):
        bdev = FlashBdev(samd.QSPIflash())
        progsize = 256
        flashfs = True
    elif hasattr(samd, "SPIflash") and hasattr(samd, "SPI_ID"):
        from machine import SPI, Pin

        spi = SPI(
            samd.SPI_ID, sck="FLASH_SCK", mosi="FLASH_MOSI", miso="FLASH_MISO", baudrate=24_000_000
        )
        bdev = FlashBdev(samd.SPIflash(spi, Pin("FLASH_CS", Pin.OUT, value=1)))
        progsize = 256
        flashfs = True
        del SPI, Pin, spi
except:
    pass

if not flashfs:
    bdev = FlashBdev(samd.MCUflash())
    progsize = 32  # which is the default

# Try to mount the filesystem, and format the flash if it doesn't exist.
fs_type = uos.VfsLfs2 if hasattr(uos, "VfsLfs2") else uos.VfsLfs1

try:
    vfs = fs_type(bdev, progsize=progsize)
except:
    fs_type.mkfs(bdev, progsize=progsize)
    vfs = fs_type(bdev, progsize=progsize)
uos.mount(vfs, "/")
del vfs, fs_type

gc.collect()
del uos, gc, samd, progsize, bdev, FlashBdev, flashfs
