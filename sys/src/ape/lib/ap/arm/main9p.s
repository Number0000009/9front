arg=0
sp=13
sb=12

#define NPRIVATES	16

GLOBL	_tos(SB), $4
GLOBL	_errnoloc(SB), $4
GLOBL	_plan9err(SB), $4
GLOBL	_privates(SB), $4
GLOBL	_nprivates(SB), $4

TEXT	_mainp(SB), 1, $(16+4+128+NPRIVATES*4)

	MOVW	$setR12(SB), R(sb)

	/* _tos = arg */
	MOVW	R(arg), _tos(SB)

	MOVW	$16(R(sp)), R1
	MOVW	R1, _errnoloc(SB)
	ADD	$4, R1
	MOVW	R1, _plan9err(SB)
	ADD	$128, R1
	MOVW	R1, _privates(SB)
	MOVW	$NPRIVATES, R1
	MOVW	R1, _nprivates(SB)

	/* _profmain(); */
	BL	_profmain(SB)

	/* _tos->prof.pp = _tos->prof.next; */
	MOVW	_tos+0(SB),R1
	MOVW	4(R1), R2
	MOVW	R2, 0(R1)

	BL	_envsetup(SB)

	/* main(argc, argv, environ); */
	MOVW	environ(SB), R(arg)
	MOVW	R(arg), 12(R(sp))
	MOVW	$inargv+0(FP), R(arg)
	MOVW	R(arg), 8(R(sp))
	MOVW	inargc-4(FP), R(arg)
	MOVW	R(arg), 4(R(sp))
	BL	main(SB)
loop:
	MOVW	R(arg), 4(R(sp))
	BL	exit(SB)
	MOVW	$_div(SB), R(arg)	/* force loading of div */
	MOVW	$_profin(SB), R(arg)	/* force loading of profile */
	B	loop

TEXT	_saveret(SB), 1, $0
TEXT	_savearg(SB), 1, $0
	RET

TEXT	_callpc(SB), 1, $0
	MOVW	argp-4(FP), R(arg)
	RET
