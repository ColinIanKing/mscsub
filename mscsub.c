#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <dos.h>

/*
 *  MSCDEX Substitute
 *
 *  Operation:
 *	This is a TSR that inserts itself before the real MSCDEX interrupt
 *	service routines. It is a dummy version of MSCDEX for NFS use, and
 *	emulates the following services:
 *
 *		00h	Get Number of CD ROM Drives
 *		01h	Get CD-ROM Drive Device List
 *		02h	Get Copyright File Name
 *		03h	Get Abstract File Name
 *		04h	Get Bibliographic Doc File Name
 *		05h	Get VTOC (partially emulated - not sure if okay)
 *		06h	Turn Debugging on
 *		07h	Turn Debugging off
 *		08h	Absolute Disk Read - Emulated to return error 21
 *		09h	Absolute Disk Write - NOT SUPPORTED
 *		0ah	(Reserved)
 *		0bh	CD ROM Drive check
 *		0ch	MSCDEX Version
 *		0dh	Get CD Drive Letters
 *		0eh	Get/Set Volume Descriptor Preference
 *		0fh	Get Directory Entry - NOT SUPPORTED
 *		10h	Send Device Driver Request - NOT SUPPORTED
 *
 *    	Also, it uses a reserved service ff, so that the parameters to the
 *	TSR are able to be altered after it is set running. The services
 *	are passed through register BX, eg:
 *
 *		AX = ffh, Bx =
 *			00,  Install drive in cx, eg cx=0, Drive A,
 *						     cx=1, Drive B, etc..
 *			01,  Remove drive in cx,  eg cx=0, Drive A, etc
 *			02,  Set mscdex version number, eg cx = 0x020a => 2.10
 *			03,  Turn off this mscsub, pass over to normal handler
 *			04,  Turn on mscsub.
 *			05,  Change name of device driver (eg MSCD000)
 *			06,  Change name of file that contains data for
 *				sub-functions 02h, 03h, 04h for each drive.
 *
 *
 *  How to load:
 *	After MSCDEX is loaded, you can load this either by running it, or
 *	load it into high memory using loadhigh. Eg: loadhigh mscsub ...
 *
 *  Syntax:
 *	MSCMON drive ...
 *
 *	drive ...  Letters of the CD ROM drives for MSCMON to emulate.
 *
 *  Example:
 *	MSCMON D E F		Load for CD Drives D: E: and F:
 *
 *  Compiler Used:
 *	Borland C++, 3.1
 *
 *  Compile: bcc -1 -ml mscmon.c
 *
 *  Test History:
 *	Works okay on 486's and 386's. I haven't tried it out on any 286's yet.
 *
 *  Author: 	Colin Ian King, Templeman Library, University of Kent.
 *
 *  Modification Record:
 *   01/03/93	Creation Date.
 *   15/03/93	Add full set of MSCDEX functions.
 */

#define	MAX_DRIVES	10
#define	TRUE		1
#define	FALSE		0
#define INT2F		0x2f

extern unsigned _heaplen = 1024;
extern unsigned _stklen  = 512;

void	exit(int);

static long unsigned int drive_map = 0L;
static unsigned int mscdex_version = 0x020a; /* 2.10 */
static int quite_mode = 0;

static char mscdex[]   = "MSCD000";
static char mscdat[64] = "MSCSUB.DAT";
static char buffer[40];

static struct DHD {
		char far *next;
		int   	devattr;
		void	(near *devep1)();
		void	(near *devep2)();
		char	devname[8];
		int	reserved;
		char	driveletter;
		char	numofunits;
} devhead;

void near
nothing()
{
}

void
getinfo(char drive, char* axr, char* filename, char*ptr)
{
	FILE	*fp;
	char	buffer[42];

	strcpy(ptr,"");
	if ((fp = fopen(filename,"r")) == NULL) {
		return;
	}
	for (;;) {
		if (fgets(buffer,41,fp)==NULL)
			goto done;
		if (strncmp(axr,buffer,4)==0)
			break;
	}
	for (;;) {
		if (fgets(buffer,41,fp)==NULL)
			goto done;
		if (*buffer == drive)
			break;
	}
	buffer[40] = '\0';
	buffer[strlen(buffer)-1] = '\0';
	strcpy(ptr,buffer+2);
done:
	fclose(fp);
}

static void interrupt (*oldfunc)(unsigned bp, unsigned di, unsigned si,
			         unsigned ds, unsigned es, unsigned dx,	
			         unsigned cx, unsigned bx, unsigned ax);


void interrupt  show_regs(unsigned bp, unsigned di, unsigned si,
			  unsigned ds, unsigned es, unsigned dx,	
			  unsigned cx, unsigned bx, unsigned ax)
{
	char		*p;
	int		i, j;
	unsigned long  	sh;

	struct DLL {
		char	subunit;
		struct DHD far *devhead;
	} *dll;

	if ((ax & 0xff00) == 0x1500) {
		/*
		 *  If turned off, do the normal handling and forget this
	 	 *  handler altogether, UNLESS it is one my mscsub
	 	 *  own command codes when al = 0xff. Sneaky.
		 */
		if (quite_mode) {
			if ((ax & 0xff) != 0xff) {
				_chain_intr(oldfunc);
			}
		}
		switch(ax & 0xff) {
		/*
 		 *  MSCSUB sub command switches for altering the context.
		 *
	 	 *    bx sub-command:
		 *	00,  Install drive in cx, eg cx=0, Drive A,
		 *				     cx=1, Drive B, etc
 		 *	01,  Remove drive in cx,  eg cx=0, Drive A, etc
		 *	02,  Set mscdex version number, eg cx = 0x020a => 2.10
		 *	03,  Turn off this mscsub, pass over to normal handler
		 *	04,  Turn on mscsub.
 		 *	05,  Change name of device driver (eg MSCD000)
	 	 *		name in ES:BX, name is 8 chars (including '\0')
		 *	06,  Change name of file that contains data for
		 *	     sub-functions 02h, 03h, 04h for each drive.
		 *		name in ES:BX, name is 64 chars (including '\0')
		 */
		case 0xff:
			switch (bx & 0xff) {
			case 0x00:
				drive_map |= 1L << (cx & 0x1f);
				break;
			case 0x01:
				drive_map &= (0x1f ^ (1L << (cx & 0x1f)));
				break;
			case 0x02:
				mscdex_version = cx;
				break;
			case 0x03:
				quite_mode = 1;
				break;
			case 0x04:
				quite_mode = 0;
				break;
			case 0x05:
				p = (char *)((((long) es) << 16) + cx);
				strncpy(mscdex,p,8);
				break;
			case 0x06:
				p = (char *)((((long) es) << 16) + cx);
				strncpy(mscdat,p,64);
				break;
			default:
				break;
			}
			break;
		case 0x00:
			cx = 0xffff;
			for (i=0,bx = 0, sh=drive_map;  sh; sh >>= 1,i++) {
				if (sh & 0x01) {	
					bx++;
					if (cx == 0xffff) {
						cx = i;
					}
				}
			}
			break;
		case 0x01:
			dll = (struct DLL *)((((long) es) << 16) + bx);

			for (i=0,j = 0, sh=drive_map;  sh; sh >>= 1,i++) {
				if (sh & 0x01) {	
					dll[j].subunit = j;
					dll[j].devhead = (struct DHD *)&devhead;
					j++;
					devhead.driveletter = i + 1; // magic
				}
			}
			devhead.next    = (char far*)-1;	/* List End */
			devhead.devattr = 32768+16384+2048;	/* Magic */
			devhead.devep1  = nothing;		/* Do nothing */
			devhead.devep2  = nothing;
			strncpy(devhead.devname,mscdex,8);
			devhead.reserved= 0;
			devhead.numofunits=j;
			break;
		/*
		 *  Copyright File Name
		 */
		case 0x02:
			p = (char *)((((long) es) << 16) + bx);
			getinfo(cx+'A',"1502",mscdat,p);
			break;
		/*
	 	 *  Abstract File Name
		 */
		case 0x03:
			p = (char *)((((long) es) << 16) + bx);
			getinfo(cx+'A',"1503",mscdat,p);
			break;
		/*
	 	 *  Bibliographic Documentation File Name
		 */
		case 0x04:
			p = (char *)((((long) es) << 16) + bx);
			getinfo(cx+'A',"1504",mscdat,p);
			break;
		/*
		 *  Read VTOC - Set AX to FFh for Volume Descriptor
		 *  Terminator record found.
		 */
		case 0x05:
			ax = 0x00ff;
			break;
		/*
		 *  0x06: Debugging turned on
		 *  0x07: Debugging turned off
	 	 */
		case 0x06:
		case 0x07:
			break;
		/*
		 *  Bodge Absolute Disk Read, Force DOS error 21.
		 */
		case 0x08:
			ax = 0x0021;	/* Error_Not_Ready */
			asm {
				stc	
			}
			break;
		/*
		 *  0x09: Forget about Absolute Disk Write
		 *  0x0a: Reserved, so ingnore..
		 */
		case 0x09:
		case 0x0a:	
			break;
		/*	
		 *  CD-ROM Drive Check, drive = cx
		 */
		case 0x0b:
			if (drive_map & (1L << (cx & 0x1f))) {
				bx = 0xadad;	/* Magic */
				ax = 0xffff;	/* Any Non Zero will do? */
			}
			else {
				ax = 0x0000;	/* MSCDEX non supported for
						 * drive in cx */
			}
			break;
		/*
		 *  MSCDEX Version Number
		 */
		case 0x0c:
			bx = mscdex_version;
			break;
		/*
		 *  Get CD ROM Drive Letters, ES:BX = buffer
		 */
		case 0x0d: 		
			p = (char *)((((long) es) << 16) + bx); // o/p buffer ptr
			for (i = 0, sh=drive_map;  sh; sh >>= 1,i++) {
				if (sh & 0x01) {
					*p++ = (char)i;
				}
			}
			break;
		/*
		 *  Get/Set Volume Descriptor Preferences
		 */
		case 0x0e:
			if (drive_map & (1L << (cx & 0x1f))) {
				switch (bx & 0xff) {
				/*
				 *  Get...
				 */
				case 0x00:
					dx = 0;  /* This version of MSCDEX does not
				   	    	support SVD's */
					break;
				/*
				 *  Set...
				 */
				case 0x01:
					dx = 0;  /* No preference!?? */
					break;
				default:
					ax = 1;	 /* Error Invalid Function */
					break;
				}
			}
			else {
				ax = 15; /* Error Invalid Drive */
			}
			break;
		default:
			break;
		}
	}
	else {
		_chain_intr(oldfunc);
	}
}


/*
 *  Append to vector and TSR
 */
int
main(int argc, char** argv)
{
	char	*ptr;
	char	ch;
	int	sh,
		i;


	/*
        * Scan & parse drive list
 	 */
	fprintf(stdout,"MSCDEX Substitute, by cik@ukc.ac.uk, V0.1, ");
	if (argc < 2) {
		fprintf(stdout,"\nUsage: MSCSUB drive ...\n");
		exit(1);
	}
	if (argc > 26) {
		fprintf(stdout,"\nMaximum of 26 drives allowed\n");
		exit(1);
	}
	for (i = 1; i<argc; i++) {
		ch = argv[i][0];
		if (!isalpha(ch)) {
			fprintf(stdout,"\n%c is not a drive\n",argv[i][0]);
			exit(1);
		}
		drive_map |= 1L << ((ch-'A') & 0x1f);
	}
	for (i = 0, sh=drive_map;  sh; sh >>= 1,i++) {
		if (sh & 0x01) {
			fprintf(stdout,"%c: ",(char)(i+'A'));
		}
	}

	oldfunc = _dos_getvect(INT2F);
	_dos_setvect(INT2F,show_regs);
	_dos_keep(0, (_SS + 20 + (_SP/16) - _psp));


	return 0;
}

