export PICO=$(realpath dep/picolibc)
export PICO_EXTENSION=$PICO/extension
export LIBGCC=/Users/xianghao/TS/arm-gnu-toolchain/lib/gcc/aarch64-none-elf/12.3.1

cmake -S dep/libnfs -B build
cmake --build build