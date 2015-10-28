.rodata
.balign 32
.globl redmanlength
.globl redmandata
.globl bluemanlength
.globl bluemandata
.globl greenmanlength
.globl greenmandata
.globl orangemanlength
.globl orangemandata

redmanlength:	.long	redmandataend - redmandata
redmandata:
.incbin "../include/redman.jpg"
redmandataend:

bluemanlength:	.long	bluemandataend - bluemandata
bluemandata:
.incbin "../include/blueman.jpg"
bluemandataend:

greenmanlength:	.long	greenmandataend - greenmandata
greenmandata:
.incbin "../include/greenman.jpg"
greenmandataend:

orangemanlength:	.long	orangemandataend - orangemandata
orangemandata:
.incbin "../include/orangeman.jpg"
orangemandataend: