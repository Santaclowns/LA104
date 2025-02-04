#https://developer.arm.com/open-source/gnu-toolchain/gnu-rm/downloads
set -e

mkdir -p build
cd build

arm-none-eabi-g++ -Os -Werror -fno-common -mcpu=cortex-m3 -mthumb -msoft-float -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wno-psabi -DLA104 -MD -D _ARM -D STM32F10X_HD -c \
  ../source/main.cpp \
  ../../../os_host/source/framework/Wnd.cpp \
  ../../../os_host/source/framework/Serialize.cpp \
  -I../../../os_library/include/ -I ../../../os_host/lib/CMSIS/Device/STM32F10x/Include -I ../../../os_host/lib/STM32F10x_StdPeriph_Driver/inc -I ../../../os_host/lib/CMSIS/Include

arm-none-eabi-gcc -fPIC -mcpu=cortex-m3 -mthumb -o output.elf -nostartfiles -T ../source/app.lds \
  ./main.o ./Wnd.o ./Serialize.o \
  -lbios_la104 -L../../../os_library/build

arm-none-eabi-objdump -d -S output.elf > output.asm
arm-none-eabi-readelf -all output.elf > output.txt

find . -type f -name '*.o' -delete
find . -type f -name '*.d' -delete

../../../../tools/elfstrip/elfstrip output.elf 72uartm.elf

nm --print-size --size-sort -gC output.elf | grep " B " > symbols_ram.txt
nm --print-size --size-sort -gC output.elf | grep " T " > symbols_rom.txt
nm --print-size --size-sort -gC output.elf > symbols_all.txt
#objdump -s -j .dynamic output.elf