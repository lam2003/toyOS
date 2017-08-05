require 'fileutils'

puts "kernel build begin"
if !Dir.exist?("build") then
    Dir.mkdir("build", 755)
end
# 编译链接
CC = "clang"
ASM = "nasm"
LD = "~/Dropbox/Developer/binutils/bin/i386-unknown-linux-gnu-ld"
LIB = "-I lib/ -I lib/kernel/ -I lib/user/ -I kernel/ -I device/"
CFLAGS = "-target i386-apple-linux-elf #{LIB} -fno-builtin -c"
ASMFLAGS = "-f elf"
LDFLAGS = "-Ttext 0xc0001500 -e main -o build/kernel.bin"
OBJS = "build/main.o build/init.o build/interrupt.o build/timer.o build/kernel.o build/print.o build/debug.o"

puts "begin compile & link"
`
#{ASM} -o build/mbr.bin -I boot/include/ boot/mbr.s
#{ASM} -o build/loader.bin -I boot/include/ boot/loader.s
#{CC} #{CFLAGS} -o build/timer.o device/timer.c
#{CC} #{CFLAGS} -o build/init.o kernel/init.c
#{CC} #{CFLAGS} -o build/interrupt.o kernel/interrupt.c
#{CC} #{CFLAGS} -o build/main.o kernel/main.c
#{CC} #{CFLAGS} -o build/debug.o kernel/debug.c
#{ASM} #{ASMFLAGS} -o build/print.o lib/kernel/print.s
#{ASM} #{ASMFLAGS} -o build/kernel.o kernel/kernel.s
#{LD} #{LDFLAGS} #{OBJS}
`
puts "compile & link successfully"
# 写入到磁盘镜像中
bochs_dir = "/Users/cache/Dropbox/Developer/bochs/"
FileUtils.cp("build/mbr.bin", bochs_dir)
FileUtils.cp("build/loader.bin", bochs_dir)
FileUtils.cp("build/kernel.bin", bochs_dir)
Dir.chdir(bochs_dir)
puts "write mbr"
`
dd if=mbr.bin of=hd60m.img bs=512 count=1 conv=notrunc
`
puts "write mbr done"
puts "write bootloader"
`
dd if=loader.bin of=hd60m.img bs=512 count=4 seek=2 conv=notrunc
`
puts "write bootloader done"
puts "write kernel"
`
dd if=kernel.bin of=hd60m.img bs=512 count=200 seek=9 conv=notrunc
`
puts "write kernel done"
FileUtils.rm("kernel.bin")
FileUtils.rm("mbr.bin")
FileUtils.rm("loader.bin")
puts "kernel build complete"