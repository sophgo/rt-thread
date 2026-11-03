import os

# toolchains options
ARCH        ='risc-v'
VENDOR      ='cvitek'
CPU         ='c906fdv'
CROSS_TOOL  ='gcc'

# bsp lib config
BSP_LIBRARY_TYPE = None

if os.getenv('RTT_ROOT'):
    RTT_ROOT = os.getenv('RTT_ROOT')
else:
    RTT_ROOT = r'../../..'

if os.getenv('RTT_CC'):
    CROSS_TOOL = os.getenv('RTT_CC')

if  CROSS_TOOL == 'gcc':
    PLATFORM    = 'gcc'
    EXEC_PATH   = os.path.abspath(RTT_ROOT + r'/host-tools/gcc/Xuantie-900-gcc-elf-newlib-x86_64-V2.8.1/bin')
else:
    print('Please make sure your toolchains is GNU GCC!')
    exit(0)

if os.getenv('RTT_EXEC_PATH'):
    EXEC_PATH = os.getenv('RTT_EXEC_PATH')

BUILD = 'debug'

if PLATFORM == 'gcc':
    # toolchains
    PREFIX  = 'riscv64-unknown-elf-'
    CC      = PREFIX + 'gcc'
    CXX     = PREFIX + 'g++'
    AS      = PREFIX + 'gcc'
    AR      = PREFIX + 'ar'
    LINK    = PREFIX + 'gcc'
    TARGET_EXT = 'elf'
    SIZE    = PREFIX + 'size'
    OBJDUMP = PREFIX + 'objdump'
    OBJCPY  = PREFIX + 'objcopy'


    DEVICE  = ' -mcpu=c906fdv -mcmodel=medany -march=rv64imafdc_xtheadc -mabi=lp64d'
    CFLAGS  = DEVICE
    CFLAGS  += ' -Wall -Wno-error=int-to-pointer-cast -Wno-error=unused-function -Wno-cpp \
                -Wno-error=unused-but-set-variable -fno-var-tracking-assignments -ffreestanding \
                -fno-common -ffunction-sections -fdata-sections -fstrict-volatile-bitfields \
                -fno-asynchronous-unwind-tables -fno-builtin-fprintf \
                -D_POSIX_SOURCE -D_SYS__PTHREADTYPES_H_'
    CFLAGS  += ' -DCONFIG_CPU_XUANTIE_C906FDV=1 -DCONFIG_CHECK_FPU_DIRTY=1 -DCONFIG_CHECK_VECTOR_DIRTY=1'
    # Xuantie-900-gcc-elf-newlib-x86_64-V2.8.1 need to add these flags to enable xtheadc and xtheadsync
    CFLAGS  += " -D__riscv_xtheadsync -D__riscv_xtheadcmo"
    CFLAGS  += ' -g -Os'
    #CFLAGS  += ' -save-temps=obj'

    LINKER_SCRIPTS = r'gcc_flash_cv18xx.ld'
    LINKER_SCRIPTS_PATH = r' -L ./'

#    if BUILD == 'debug':
#        CFLAGS += ' -O2 -g'
#        AFLAGS += ' -g'
#    else:
#        CFLAGS += ' -Os'

    AFLAGS  = ' -c' + DEVICE + ' -D__ASSEMBLY__' + CFLAGS
    LFLAGS  = DEVICE + ' -nostartfiles -Wl,--gc-sections -Wl,--whole-archive -Wl,--no-whole-archive -Wl,-Map=rtthread.map,-cref,-u,_start -T ' + LINKER_SCRIPTS + LINKER_SCRIPTS_PATH
    CPATH   = ''
    LPATH   = ''

#    CXXFLAGS = CFLAGS + ' -std=gnu++17 -Wno-multichar -Wno-parentheses'

DUMP_ACTION = OBJDUMP + ' -D -S $TARGET > rtt.asm\n'
POST_ACTION = OBJCPY + ' -O binary $TARGET rtthread.bin\n' + SIZE + ' $TARGET \n'