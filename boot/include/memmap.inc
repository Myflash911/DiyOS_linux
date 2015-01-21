;定义各个程序使用的内存地址
BaseOfBootStack		equ	0x7c00		;boot.bin使用的栈基址
BaseOfLoaderStack	equ	0x0100		;loader.bin使用的栈基址

;loader.bin被加载到的基地址和偏移地址
BaseOfLoader		equ	0x9000
OffsetOfLoader		equ	0x0100
BaseOfLoaderPhyAddr	equ	BaseOfLoader * 0x0010
;kernel.bin被加载到的基址和偏移地址
BaseOfKernelFile	equ	0x8000		;最开始将kernel.bin加载到内存的基址
OffsetOfKernelFile	equ	0x0000		;最开始将kernel.bin加载到内存的偏移地址
BaseOfKernelFilePhyAddr	equ	BaseOfKernelFile * 0x10	;loader中使用的物理地址
;32保护模式下
;PageDirBase		equ	0x100000	;页目录开始地址：1M
;PageTblBase		equ	0x101000	;页表开始地址：1M+4K

;内存最终所在的位置
;loader.bin在使用bios服务获取一些机器硬件信息后，将废除bios以及bios中断，将内核加载到物理地址开始
KernelEntryPointPhyAddr		equ	0x0400	;必须于ld -Ttext值相等

;在kernel.bin开始将重新设置中断