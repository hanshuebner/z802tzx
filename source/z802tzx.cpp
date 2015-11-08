
#include "stdafx.h"
#include <string.h>

#include "loader.h"

struct Snapshot
{
	byte reg_a;
	byte reg_f;
	word reg_bc;
	word reg_hl;
	word reg_pc;
	word reg_sp;
	byte reg_i;
	byte reg_r;
    word reg_de;
	word reg_bc2;
	word reg_de2;
	word reg_hl2;
	byte reg_a2;
	byte reg_f2;
	word reg_iy;
	word reg_ix;

	byte border;
	bool ei;				// 0 = DI , 1 = EI
	byte iff2;
	byte inter_mode;		// 0,1 or 2

	bool mode128;
	byte last_out_7ffd;		// 128 Page

	byte last_out_fffd;		// Sound Chip reg.
	byte reg_ay[16];

	byte page[8][16384];	// Memory
};

Snapshot snap;
char filename[512];
char out_filename[512];
char loader_name[256] = "\0";

char game_name[256]="                                ";
char info1[256]="                                ";
char info2[256]="                                ";

int snap_type;				// -1 = Not recognised
							//  0 = Z80
int snap_len;
byte snap_bin[256000];
byte WorkBuffer[65535];

bool verbose = false;
int speed_value = 3;
int load_colour = -1;
bool external = false;
char external_filename[512];
byte bright = 0;

void print_error(const char * text)
{
	printf("\n-- ERROR : %s\n",text);
}

void print_verbose(const char * text)
{
	if (verbose)
	{
		puts(text);
	}
}

static bool decrunch_z80 (byte *BufferIn, word BlLength, byte *BufferOut, word OutLength)

/**********************************************************************************************************************************/
/* Pre   : `BufferIn' points to the crunched spectrum block, `BlLength' holds the length, `BufferOut' points to the result.       */
/*         `OutLength' is the expected length after decrunching.                                                                  */
/*         The version of Z80 file must have been determined.                                                                     */
/* Post  : The block has been decrunched from `BufferIn' to `BufferOut'. The return value is TRUE if all went well.               */
/* Import: None.                                                                                                                  */
/**********************************************************************************************************************************/

{
  word register IndexIn;
  word register IndexOut       = 0;
  word register RunTimeLength;

  for (IndexIn = 0 ; IndexIn < BlLength ; IndexIn ++)                                       /* Decrunch it into the second buffer */
  {
    if (IndexOut >= OutLength)                                                                           /* Past maximum length ? */
      return (false);                                                                           /* Then there's an error for sure */
    if (*(BufferIn + IndexIn) != 0xED)                                                                  /* Start of crunch code ? */
      *(BufferOut + (IndexOut ++)) = *(BufferIn + IndexIn);                                                      /* No, just copy */
    else
      if (*(BufferIn + IndexIn + 1) != 0xED)                                                                  /* Crunch confirm ? */
        *(BufferOut + (IndexOut ++)) = *(BufferIn + IndexIn);                                                    /* No, just copy */
      else                                                                                                      /* Yes, decrunch! */
      {
        RunTimeLength = *(BufferIn + IndexIn + 2);                                                             /* Run time length */
        while (RunTimeLength -- > 0 && IndexOut < OutLength)
          *(BufferOut + (IndexOut ++)) = *(BufferIn + IndexIn + 3);
        IndexIn += 3;
      }
  }
  return (IndexOut == OutLength);
}

static void crunch_z80 (byte *BufferIn, word BlLength, byte *BufferOut, word *CrunchedLength)

/**********************************************************************************************************************************/
/* Pre   : `BufferIn' points to the uncrunched spectrum block, `BlLength' holds the length, `BufferOut' points to the result.     */
/*         `CrunchedLength' is the length after crunching.                                                                        */
/*         The version of Z80 file must have been determined.                                                                     */
/* Post  : The block has been crunched from `BufferIn' to `BufferOut'.                                                            */
/*         If the crunched size is larger than (or equal to) the non-crunched size, the block is returned directly with           */
/*         `CrunchedLength' set to 0x0000.                                                                                        */
/*         The crunched block is returned reversed (for backward loading).                                                        */
/* Import: None.                                                                                                                  */
/**********************************************************************************************************************************/

{
  word register IndexIn;
  word register IndexOut       = 0;
  word          LengthOut;
  word register RunTimeLength;
  bool          CrunchIt       = false;
  byte          RepeatedCode;
  byte          PrevByte       = 0x00;
  byte         *BufferTemp;



  BufferTemp	= WorkBuffer + (word)32768;                                                        /* Use the top 16Kb of Workbuffer */
  for (IndexIn = 0 ; IndexIn < BlLength - 4 ; IndexIn ++)                                     /* Crunch it into the second buffer */
  {
    if (*(BufferIn + IndexIn) == 0xED &&                                                                     /* Exceptional cases */
        *(BufferIn + IndexIn + 1) == 0xED)
      CrunchIt = true;
    else if (*(BufferIn + IndexIn) == *(BufferIn + IndexIn + 1) &&
             *(BufferIn + IndexIn) == *(BufferIn + IndexIn + 2) &&
             *(BufferIn + IndexIn) == *(BufferIn + IndexIn + 3) &&
             *(BufferIn + IndexIn) == *(BufferIn + IndexIn + 4) &&                                        /* At least 5 repeats ? */
             PrevByte != 0xED)                                                                         /* Not right after a 0xED! */
      CrunchIt = true;
    if (CrunchIt)                                                                                             /* Crunch a block ? */
    {
      CrunchIt = false;
      RunTimeLength = 1;
      RepeatedCode = *(BufferIn + IndexIn);
      while (RunTimeLength < 255 && RunTimeLength + IndexIn < BlLength && *(BufferIn + RunTimeLength + IndexIn) == RepeatedCode)
        RunTimeLength ++;
      *(BufferTemp + (IndexOut ++)) = 0xED;
      *(BufferTemp + (IndexOut ++)) = 0xED;
      *(BufferTemp + (IndexOut ++)) = (byte) RunTimeLength;
      *(BufferTemp + (IndexOut ++)) = RepeatedCode;
      IndexIn += RunTimeLength - 1;
      PrevByte = 0x00;
    }
    else
      PrevByte = *(BufferTemp + (IndexOut ++)) = *(BufferIn + IndexIn);                                          /* No, just copy */
  }
  while (IndexIn < BlLength)
    *(BufferTemp + (IndexOut ++)) = *(BufferIn + (IndexIn ++));                                                 /* Copy last ones */
  if (IndexOut >= BlLength)                                                              /* Compressed larger than uncompressed ? */
  {
    memcpy (BufferTemp, BufferIn, BlLength);                                                /* Return the inputblock uncompressed */
    LengthOut = BlLength;
    IndexOut = 0x0000;                                                                                  /* Signal: not compressed */
  }
  else
    LengthOut = IndexOut;
  BufferTemp = WorkBuffer + (word)32768 + LengthOut - 1;                                       /* Point to the last crunched byte */
  for (IndexIn = 0 ; IndexIn < LengthOut ; IndexIn ++)                             /* Now reverse the result for backward loading */
    *(BufferOut + IndexIn) = *(BufferTemp - IndexIn);
  *CrunchedLength = IndexOut;
}

/*
int TestDecZ80(byte *s, byte *d, int l, int ll)
{
// Decompresses a portion of .Z80 file pointed to by s to d, the destination
// length is in l , and return 1 if it can stay decompressed (it never overlaps
// with the source) ll is the length of source
int n=0;
int m=0;
int o;
int over;

over=1;
while (n<l)
	{
	if (s[m]==0xED && s[m+1]==0xED)
		{
		for (o=0; o<s[m+2]; o++)
			d[n++]=s[m+3];
		m+=4;
		}
	else
		d[n++]=s[m++];
	if (((l-ll)+m)<n) over=0;
	}
return(over);
}
*/

bool test_decz80(byte *source, int final_len, int source_len)
{
	// source is not reversed !!!

	int dst_pnt = 0;
	int src_pnt = final_len-source_len;

	bool overwrite = false;

	byte b1;
	byte b2;
	byte times;
	byte value;

	int off = src_pnt;

	while (dst_pnt < final_len && !overwrite)
	{
		b1 = source[src_pnt-off];
		src_pnt++;
		b2 = source[src_pnt-off];

		if (b1 == 0xED && b2 == 0xED)
		{
			// Repeat
			times = source[(src_pnt-off)+1];
			value = source[(src_pnt-off)+2];
			dst_pnt += times;
			src_pnt+=3;
		}
		else
		{
			dst_pnt++;
		}

		if (dst_pnt > src_pnt)
		{
			overwrite = true;
		}
	}
	return overwrite;
}

bool test_rev_decz80(byte *source, int final_len, int source_len)
{
	// source is reversed !!!

	int dst_pnt = final_len-1;
	int src_pnt = source_len-1;

	bool overwrite = false;

	byte b1;
	byte b2;
	byte times;
	byte value;

	while (dst_pnt >=0 && !overwrite)
	{
		b1 = source[src_pnt];
		src_pnt--;
		b2 = source[src_pnt];

		if (b1 == 0xED && b2 == 0xED)
		{
			// Repeat
			times = source[src_pnt-1];
			value = source[src_pnt-2];
			dst_pnt -= times;
			src_pnt-=3;
		}
		else
		{
			dst_pnt--;
		}

		if (dst_pnt < src_pnt)
		{
			overwrite = true;
		}
	}
	return overwrite;
}

static void reverse_block (byte *BufferOut, byte *BufferIn)
{
  word register  Cnt;
  byte          *BIn;
  byte          *BOut;

  for (BIn = BufferIn, BOut = BufferOut + 16383, Cnt = 0 ; Cnt < 16384 ; Cnt ++)
    *(BOut --) = *(BIn ++);
}

void init_snap(void)
{
	int i;

	snap.reg_a = 0;
	snap.reg_f = 0;
	snap.reg_bc = 0;
	snap.reg_hl = 0;
	snap.reg_pc = 0;
	snap.reg_sp = 0;
	snap.reg_i = 0;
	snap.reg_r = 0;
    snap.reg_de = 0;
	snap.reg_bc2 = 0;
	snap.reg_de2 = 0;
	snap.reg_hl2 = 0;
	snap.reg_a2 = 0;
	snap.reg_f2 = 0;
	snap.reg_iy = 0;
	snap.reg_ix = 0;

	snap.border = 0;
	snap.ei = false;
	snap.iff2 = 0;
	snap.inter_mode = 0;

	snap.mode128 = false;
	snap.last_out_7ffd = 0x10;		// Default page

	snap.last_out_fffd = 0;
	for (i=0; i < 16; i++)
	{
		snap.reg_ay[i] = 0;
	}

	for (i=0; i < 8; i++)
	{
		for (int j=0; j < 16384; j++)
		{
			snap.page[i][j]=0;
		}
	}
}

void create_out_filename()
{
	strcpy(out_filename, filename);
	int last = strlen(out_filename)-1;
	while (out_filename[last] != '.' && last > 0)
	{
		last--;
	}
	if (last == 0)
	{
		// No extension ???
		return;
	}
	out_filename[last+1]='t';
	out_filename[last+2]='z';
	out_filename[last+3]='x';
	out_filename[last+4]=0;
}

void clear_name(char name[])
{
	for(int i=0; i < 32; i++)
	{
		name[i]=' ';
	}
}

char * get_file_only(char *path)
{
	int pos = strlen(path)-1;
	while (pos>=0 && path[pos] != '\\' && path[pos] != '/')
	{
		pos--;
	}
	if (pos == 0)
	{
		return path;
	}
	else
	{
		return (&path[pos+1]);
	}
}

void create_loader_name()
{
	char * skip = get_file_only(filename);

	int len = strlen(skip);
	if (len > 8)
	{
		len = 8;
	}
	for (int i=0; i < len && skip[i] != '.'; i++)
	{
		loader_name[i] = skip[i];
	}
	loader_name[len]=0;
}

bool is_small(char c)
{
	if (c >= 'a' && c <= 'z')
	{
		return true;
	}
	return false;
}

bool is_number(char c)
{
	if (c >= '0' && c <= '9')
	{
		return true;
	}
	return false;
}

bool is_capital(char c)
{
	if (c >= 'A' && c <= 'Z')
	{
		return true;
	}
	return false;
}

void center_name(char name[])
{
	int i;
	int num = 31-i;

	for (i=31; i >=0 && name[i]==' '; i--);

	if (num>1)
	{
		for (i=31; i >= num/2; i--)
		{
			name[i] = name[i-(num/2)];
		}
		for (i=0; i < num/2; i++)
		{
			name[i]=' ';
		}
	}
}

void create_game_name()
{
	char * skip = get_file_only(filename);

	int fl = strlen(skip);

	int pos = 0;
	int posf = 0;

	while (pos < 32 && posf < fl && skip[posf] != '.')
	{
		if (skip[posf] == '_')
		{
			game_name[pos] = ' ';
		}
		else
		{
			game_name[pos] = skip[posf];
			if (is_small(skip[posf]) && (is_number(skip[posf+1]) || is_capital(skip[posf+1]) || skip[posf+1]=='(' || skip[posf+1]=='+'))
			{
				pos++;
				game_name[pos]=' ';
			}
		}

		pos++;
		posf++;
	}
}

void print_usage(bool title)
{
	if (title)
	{
		printf("\nZ80 Snapshot to TZX Tape Converter  v1.0\n");
		printf(  "\n  ->by Tom-Cat<-\n\n");
	}
	printf("Usage:\n\n");
	printf("  Z802TZX Filename.z80 [Options]\n\n");
	printf("  Options:\n");
	printf("  -v    Verbose Output (Info on conversion)\n");
	printf("  -s n  Loading Speed (n: 0=1500  1=2250  2=3000  3=6000 bps) Default: 3\n");
	printf("  -b n  Border (0=Black 1=Blue 2=Red 3=Magenta 4=Green 5=Cyan 6=Yellow 7=White)\n");
	printf("  -r    Use Bright Colour when filling in the last attribute line\n");
	printf("  -$ f  Use External Loading Screen in file f (.scr 6912 bytes long file)\n");
	printf("  -o f  Use f as the Output File (Default: Input File with .tzx extension)\n");
	printf("  -l s  Use String s as the ZX Loader Name (Loading: name) Up To 8 Chars!\n");
	printf("  -g s  Use String s as the Game Name (Shown when Loading starts)\n");
	printf("  -i1 s Show a line of info when loading (first line)\n");
	printf("  -i2 s Show another line of info when loading (second line)\n");
	printf("\nStrings (s) can be Up To 32 chars long. Use '~' as (C) char.If no Border Colour\n");
	printf("is selected then it will be gathered from the snapshot file. Loading and Game\n");
	printf("Name will be taken from the Filename if you don't use the -l or -g parameters!\n");
}

void change_copyright(char * st)
{
	int len = strlen(st);
	for (int jj=0; jj < len; jj++)
	{
		if (st[jj] == '~')
		{
			st[jj] = 0x7f;
		}
	}
}

bool parse_args(int argc, char * argv[])
{
	if (argc < 2)
	{
		print_usage(true);
		return false;
	}
	char * inf;
	for (int i=1; i < argc; i++)
	{
		if (i==1)
		{
			// Snapshot filename
			strcpy(filename, argv[1]);
			create_out_filename();
			create_loader_name();
			create_game_name();
			center_name(game_name);
		}
		else
		{
			if (argv[i][0] == '-')
			{
				// Options
				switch (argv[i][1])
				{
				case 'r':
					bright = 0x40;
					break;

				case 'i':
					if (argv[i][2] == '1')
					{
						inf=info1;
					}
					else
					{
						inf=info2;
					}
					i++;
					strcpy(inf, argv[i]);
					inf[strlen(inf)]=' ';
					inf[32] = 0;	// Can't be bigger than 32 chars !
					change_copyright(inf);
					center_name(inf);

					break;
					// Game Name (When Loader is loaded)
				case 'g':
					i++;
					clear_name(game_name);
					strcpy(game_name, argv[i]);
					game_name[strlen(game_name)]=' ';
					game_name[32] = 0;	// Can't be bigger than 32 chars !
					change_copyright(game_name);
					center_name(game_name);
					break;
					// Loader Name (Loading: Name)
				case 'l':
					i++;
					strcpy(loader_name, argv[i]);
					loader_name[8] = 0;	// Can't be bigger than 8 chars !
					break;
					// Output Filename
				case 'o':
					i++;
					strcpy(out_filename, argv[i]);
					break;
					// External Loading Screen
				case '$':
					i++;
					external = true;
					strcpy(external_filename, argv[i]);
					break;
					// Border Colour when loading
				case 'b':
					i++;
					sscanf(argv[i], "%i", &load_colour);
					if (load_colour < 0 || load_colour > 7)
					{
						print_error("Invalid Border Colour!");
						return false;
					}
					break;
					// Speed value
				case 's':
					i++;
					sscanf(argv[i], "%i", &speed_value);
					if (speed_value < 0 || speed_value > 3)
					{
						print_error("Invalid Speed Value!");
						return false;
					}
					break;
					// Verbose output
				case 'v':
					verbose = true;
					break;
				default:
					print_error("Unknown Option\n");
					print_usage(false);
					return false;
				}
			}
			else
			{
				print_error("Unknown Option\n");
				print_usage(false);
				return false;
			}
		}
	}

	return true;
}

bool get_type()
{
	int len = strlen(filename);
	char ext[4];
 	strcpy(ext, filename+len-3);
	if (!strcasecmp(ext,"z80"))
	{
		snap_type = 0;
		return true;
	}

	snap_type = -1;
	return false;
}

word gw(byte * t)
{
	word ret = *t + (256* *(t+1));
	return ret;
}

void print_verbose_registers()
{
	char tempc[256];
	print_verbose(" A  F   BC   HL   PC   SP  I  R   DE B'C' D'E' H'L' A' F'   IY   IX Bd EI I2 IM");
//	sprintf(tempc,"00 00 0000 0000 0000 0000 00 00 0000 0000 0000 0000 00 00 0000 0000 00 00 00 00",
	sprintf(tempc,"%02x %02x %04x %04x %04x %04x %02x %02x %04x %04x %04x %04x %02x %02x %04x %04x %02x %02x %02x %02x\n",
		snap.reg_a, snap.reg_f, snap.reg_bc, snap.reg_hl, snap.reg_pc, snap.reg_sp, snap.reg_i, snap.reg_r,
		snap.reg_de, snap.reg_bc2, snap.reg_de2, snap.reg_hl2, snap.reg_a2, snap.reg_f2, snap.reg_iy,
		snap.reg_ix, snap.border, snap.ei, snap.iff2, snap.inter_mode);
	print_verbose(tempc);
}

bool import_z80()
{
	char tempc[256];

	byte z80_12 = snap_bin[12];
	if (z80_12 == 255)
	{
		z80_12 = 1;									// Old version of 1.45 Z80
	}

	snap.reg_a   = snap_bin[0];
	snap.reg_f   = snap_bin[1];
	snap.reg_bc  = gw(&snap_bin[2]);
	snap.reg_hl  = gw(&snap_bin[4]);
	snap.reg_pc  = gw(&snap_bin[6]);
	snap.reg_sp  = gw(&snap_bin[8]);
	snap.reg_i   = snap_bin[10];
	snap.reg_r   = snap_bin[11];					// least 7 bits of R
	snap.reg_r  |= 128*(z80_12&1);					// bit 7 of R
	snap.border  = (z80_12>>1)&7;					// Border in bits 1,2,3
	snap.reg_de  = gw(&snap_bin[13]);
	snap.reg_bc2 = gw(&snap_bin[15]);
	snap.reg_de2 = gw(&snap_bin[17]);
	snap.reg_hl2 = gw(&snap_bin[19]);
	snap.reg_a2  = snap_bin[21];
	snap.reg_f2  = snap_bin[22];
	snap.reg_iy  = gw(&snap_bin[23]);
	snap.reg_ix  = gw(&snap_bin[25]);
	snap.ei      = snap_bin[27]>0;
	snap.iff2    = snap_bin[28];
	snap.inter_mode = snap_bin[29]&3;				// Interrupt mode in bits 0,1

	if (snap.reg_pc != 0)
	{
		print_verbose("Snapshot Version is 1.45 or Older (48k only)\n");
		print_verbose_registers();
		// Old 1.45 version of the snapshot ! Here follows the 48k memory compressed
		int mem_len = snap_len - 30;
		if (z80_12&32)		// Compressed snapshot
		{
			byte buffer[49152];
			mem_len -= 4;		// Don't need the end marker in compressed data (1.45 only)
			if (!decrunch_z80(&snap_bin[30], mem_len, buffer, 49152))
			{
				print_error("Snapshot Corrupted");
				return false;
			}
			memcpy(snap.page[5], buffer      , 16384);
			memcpy(snap.page[1], buffer+16384, 16384);
			memcpy(snap.page[2], buffer+32768, 16384);
		}
		else
		{
			// Uncompressed , just copy the memory to appropritate pages
			memcpy(snap.page[5], &snap_bin[30      ], 16384);
			memcpy(snap.page[1], &snap_bin[30+16384], 16384);
			memcpy(snap.page[2], &snap_bin[30+32768], 16384);
		}

		return true;
	}
	// v 2.01 and UP Z80

 	snap.reg_pc = gw(&snap_bin[32]);

	word additional_len = gw(&snap_bin[30]);
	byte hardware_mode = snap_bin[34];

	if (additional_len == 23)
	{
		print_verbose("Snapshot Version is 2.01\n");
		// v 2.01 Z80 - check for 128k
		if (hardware_mode == 3 || hardware_mode == 4)
		{
			snap.mode128 = true;
		}
	}
	else
	{
		if (additional_len == 54)
		{
			print_verbose("Snapshot Version is 3.0 or Newer\n");
			// v 3.0> Z80 - check for 128k
			if (hardware_mode > 3 && hardware_mode < 7)
			{
				snap.mode128 = true;
			}
		}
		else
		{
			print_error("Snapshot Corrupted");
			return false;
		}
	}

	print_verbose_registers();

	if (snap.mode128)
	{
		// 128 Mode, fill in additional 128 registers !
		snap.last_out_7ffd = snap_bin[35];
		snap.last_out_fffd = snap_bin[38];
		memcpy(snap.reg_ay, &snap_bin[39], 16);	// Copy AY registers
		sprintf(tempc, "128k Snapshot, Last-Out Registers: 7ffd:%02x  fffd:%02x\n", snap.last_out_7ffd, snap.last_out_fffd);
		print_verbose(tempc);
	}
	else
	{
		print_verbose("48k Snapshot\n");
	}

	// Fill the memory pages

	int pos = 32+additional_len;
	word block_len;
	byte page_num;

	bool pages[8]={false,false,false,false,false,false,false,false};

	while (pos < snap_len)
	{
		block_len = gw(&snap_bin[pos]);
		page_num = snap_bin[pos+2]-3;
		pages[page_num]=true;
		if (block_len == 0xffff)
		{
			// uncompressed
			block_len = 16384;
			memcpy(snap.page[page_num], &snap_bin[pos+3], block_len);
		}
		else
		{
			if (!decrunch_z80(&snap_bin[pos+3], block_len, snap.page[page_num], 16384))
			{
				print_verbose("Warning: Error when decompressing memory page!");
//				return false;
			}
		}
		pos += 3+block_len;
	}
	if (verbose)
	{
		sprintf(tempc,"Used Memory Pages: ");
		bool prvi = true;
		for (int i=0; i < 8; i++)
		{
			if(pages[i])
			{
				if(!prvi)
				{
					sprintf(tempc,"%s,%i",tempc,i);
				}
				else
				{
					prvi = false;
					sprintf(tempc,"%s%i",tempc,i);
				}
			}
		}
		print_verbose(tempc);
	}

	return true;
}

bool load_snap(void)
{
	bool succ = false;

	if (!get_type())
	{
		print_error("Snapshot type not recognised!");
		return false;
	}

 	FILE * file = NULL;
	file = fopen(filename, "rb");
	if (file == NULL)
	{
		print_error("Snapshot cannot be loaded!");
		return false;
	}
	snap_len = fread((void *)snap_bin, sizeof(byte), 256000, file);
	fclose(file);

	switch (snap_type)
	{
	case 0:	//Z80
		print_verbose("Snapshot Type is Z80\n");
		succ = import_z80();
		break;
	}

	if (!succ)
	{
		return false;
	}
	return true;
}

#define LOADERPREPIECE  (224+1)-87+12-1+41+41                                                  /* Length of the BASIC part before the loader code */
static byte SpectrumBASICData[LOADERPREPIECE] = {

	/* 0 {INK 255}{PAPER 255}BORDER PI-PI : PAPER PI-PI : INK PI-PI : CLS
		PRINT "{AT 6,0}{INK 5}
		{AT 12,9}{INK 6}{PAPER 2}{FLASH 1} NOW LOADING {AT 0,0}{FLASH 0}{PAPER 0}{INK 0}":
		RANDOMIZE USR (PEEK VAL "23627"+VAL "256"*PEEK VAL "23628") */
	"\x00\x00\xFF\xFF\x10\xFF\x11\xFF\xE7\xA7\x2D\xA7\x3A\xDA\xA7\x2D\xA7\x3A\xD9\xA7\x2D\xA7\x3A\xFB\x3A"
	"\xF5\"\x16\x06\x00\x10\x05""                                "
	"\x16\x0B\x0A\x10\x06\x11\x02\x12\x01 IS LOADING \x16\x00\x00\x12\x00\x11\x00\x10\x00\""
	"\x3A\xF5\"\x16\x13\x00\x10\x04""                                ""\""
	"\x3A\xF5\"\x16\x15\x00\x10\x04""                                ""\""
	"\x3A\xF9\xC0(\xBE\xB0\"23627\"+\xB0\"256\"*\xBE\xB0\"23628\")\x0D"

	/* Variables area - the 768 byte loader block is	appended after this piece */
	"\xCD\x52\x00"         /* VARSTART  CALL 0052,RET_INSTR   ; Determine on which address we are now             */
	"\x3B"                 /* RETRET    DEC  SP                                                                   */
	"\x3B"                 /*           DEC  SP                                                                   */
	"\xE1"                 /*           POP  HL                                                                   */
	"\x01\x12\x00"         /*           LD   BC,0012                                                              */
	"\x09"                 /*           ADD  HL,BC            ; Find start of the 768 byte loader                 */
	"\x11\x00\xBD"         /*           LD   DE,+BD00                                                             */
	"\x01\x00\x03"         /*           LD   BC,+0300                                                             */
	"\xED\xB0"             /*           LDIR                  ; Move it into place                                */
	"\xC3\x00\xBD" };      /*           JP   BD00,LDSTART     ; And run it!                                       */

/**********************************************************************************************************************************/
/* The custom loader. Any speed can be handled by it.                                                                             */
/* Notice that the EAR bit value is NOT rotated as opposed to the ROM and is kept at 0x40!                                        */
/* ROM speed: a = 22, b = 25 (bd = 27) => delay = 32*22 = 704, sampler = 59*25 = 1475 => delay/sampler = 1/2                      */
/* Max speed: a = 1, b = 0 (bd = 2) => 279 + 32 = 311 (hb0 = 156 T) => 468 average bit => 7479 bps                                */
/* (see below) The pilot pulse must be between 1453 + 16a T and 3130 + 16a T (a=known)                                            */
/* (see below) The first sync pulse must be 840 + 16a T maximum (a=known)                                                         */
/* Average bit length = 3,500,000 / speed : (a=x, b=2x - so delay/sampler is kept at 1/2)                                         */
/*  1364 bps => 2566 T => hb0 = 855 T, hb1 = 1710 T, avg = 1283 (2565) T       <-- ROM speed                                      */
/*              279 + 32a + 43b = 2565 => 32x + 86x = 2286 => 118x = 2286 => x = 19.4                                             */
/*              279 + 32*20 + 43*39 = 2596 (2565 needed), so a = 20, b = 39, bd = 41                                              */
/*              PilotMin = 1453 + 16a = 1453 + 16*20 = 1773 T                                                                     */
/*              PilotMax = 3130 + 16a = 3130 + 16*20 = 3450 T => 2168 T                                                           */
/*              Sync0 = 840 + 16a = 840 + 16*20 = 1160 T => 667 T                                                                 */
/*  2250 bps => 1556 T => hb0 = 518 T, hb1 = 1036 T, avg = 777 (1554) T                                                           */
/*              279 + 32a + 43b = 1554 => 32x + 86x = 1275 => 118x = 1275 => x = 10.8                                             */
/*              279 + 32*11 + 43*22 = 1577 (1554 needed), so a = 11, b = 22, bd = 24                                              */
/*              PilotMin = 1453 + 16a = 1453 + 16*11 = 1629 T                                                                     */
/*              PilotMax = 3130 + 16a = 3130 + 16*11 = 3306 T => 2000 T                                                           */
/*              Sync0 = 840 + 16a = 840 + 16*11 = 1016 T => 600 T                                                                 */
/*  3000 bps => 1167 T => hb0 = 389 T, hb1 = 778 T, avg = 584 (1167) T                                                            */
/*              279 + 32a + 43b = 1167 => 32x + 86x = 888 => 118x = 888 => x = 7.5                                                */
/*              279 + 32*7 + 43*16 = 1191 (1167 needed), so a = 7, b = 16, bd = 18                                                */
/*              PilotMin = 1453 + 16a = 1453 + 16*7 = 1565 T                                                                      */
/*              PilotMax = 3130 + 16a = 3130 + 16*7 = 3242 T => 1900 T                                                            */
/*              Sync0 = 840 + 16a = 840 + 16*7 = 952 T => 550 T                                                                   */
/*  6000 bps => 584 T => hb0 = 195 T, hb1 = 390 T, avg = 293 (585) T                                                              */
/*              279 + 32a + 43b = 585 => 32x + 86x = 306 => 118x = 306 => x = 2.6                                                 */
/*              279 + 32*3 + 43*5 = 590 (585 needed), so a = 3, b = 5, bd = 7                                                     */
/*              PilotMin = 1453 + 16a = 1453 + 16*3 = 1501 T                                                                      */
/*              PilotMax = 3130 + 16a = 3130 + 16*3 = 3178 T => 1700 T                                                            */
/*              Sync0 = 840 + 16a = 840 + 16*3 = 888 T => 450 T                                                                   */
/**********************************************************************************************************************************/

static struct TurboLoadVars
{
  /* Values as stored inside the TurboLoader code */
  byte      _Compare;    /* Variable 'bd' + starting value (+80) */
  byte      _Delay;      /* Variable 'a' */
  /* Timing values as stored in the TZX block header (unless ROM speed) */
  word      _LenPilot;   /* Number of pilot pulses is calculated to make the pilot take exactly 1 second */
                         /* This is just enough for the largest possible block to decompress and sync again */
  word      _LenSync0;   /* Both sync values are made equal */
  word      _Len0;       /* A '1' bit gets twice this value */
} turbo_vars[4] = {{ 0x80 + 41, 20, 2168, 667, 855 },   /*  1364 bps - uses the normal ROM timing values! */
                   { 0x80 + 24, 11, 2000, 600, 518 },   /*  2250 bps */
                   { 0x80 + 18,  7, 1900, 550, 389 },   /*  3000 bps */
                   { 0x80 +  7,  3, 1700, 450, 195 }};  /*  6000 bps */

/*
  COMPARE is on 94
  DELAY   is on 118
  XOR COL is on 134
*/

byte turbo_loader[144+1]   =
  { "\xCD\x5B\xBF"        /* LD_BLOCK  CALL LD_BYTES         */
    "\xD8"                /*           RET  C                */
    "\xCF"                /*           RST  0008,ERROR_1     */
    "\x1A"                /*           DEFB +1A ; R Tape Loading Error */
    "\x14"                /* LD_BYTES  INC  D                */
    "\x08"                /*           EX   AF,AF'           */
    "\x15"                /*           DEC  D                */
    "\x3E\x08"            /*           LD   A,+08            (Border BLACK + MIC off) */
    "\xD3\xFE"            /*           OUT  (+FE),A          */
    "\xDB\xFE"            /*           IN   A,(+FE)          */
    "\xE6\x40"            /*           AND  +40              */
    "\x4F"                /*           LD   C,A              (BLACK/chosen colour) */
    "\xBF"                /*           CP   A                */
    "\xC0"                /* LD_BREAK  RET  NZ               */
    "\xCD\xCA\xBF"        /* LD_START  CALL LD_EDGE_1        */
    "\x30\xFA"            /*           JR   NC,LD_BREAK      */
    "\x26\x00"            /*           LD   H,+80            */

    /* Each subsequent 2 edges take:   65 + LD_EDGE_2 T = 283 + 32a + 43b T, bd = b + 2                      */
    /* Minimum:                                                                                              */
    /*   ROM => b = (+C6 - +9C - 2) + 1 = 41, a=22 => 427 + 32a + 59b T => 427 + 32*22 + 59*41 T = 3550 T    */
    /*          283 + 32a + 43b = 3432, a=20 => 283 + 32*20 + 43b = 3550 => 43b = 2627 => b = 61, bd = 63    */
    /*   b = 61, so each 2 leader pulses must fall within:                                                   */
    /*   283 + 32a + 43*61 T = 2906 + 32a T                                                                  */
    /*   One leader pulse must therefore be (2906 + 32a) / 2 = 1453 + 16a T minimum                          */
    /* Maximum, at timeout:                                                                                  */
    /*   ROM => b = +100 - +9C - 2 = 98, a=22 => 427 + 32a + 59b T => 427 + 32*22 + 59*98 T = 6913 T         */
    /*          283 + 32a + 43b = 6913, a=20 => 283 + 32*20 + 43b = 6913 => 43b = 6002 => b = 139, bd = 141  */
    /*   b = 139, so each 2 leader pulses must fall within:                                                  */
    /*   283 + 32a + 43*139 T = 6260 + 32a T                                                                 */
    /*   One leader pulse must therefore be (6260 + 32a) / 2 = 3130 + 16a T maximum                          */
    /* Notice that, as opposed to the ROM, we don't need the leader to be over a second. Only 256 pulses are */
    /* required (128 * 2 edges, counted in H).                                                               */

    "\x06\x75"            /* LD_LEADER LD   B,+73            7       */
    "\xCD\xC6\xBF"        /*           CALL LD_EDGE_2        17      */
    "\x30\xF1"            /*           JR   NC,LD_BREAK      7 (/12) */
    "\x3E\xB0"            /*           LD   A,+B2            7       */
    "\xB8"                /*           CP   B                4       */
    "\x30\xED"            /*           JR   NC,LD_START      7 (/12) */
    "\x24"                /*           INC  H                4       */
    "\x20\xF1"            /*           JR   NZ,LD_LEADER     7/12    */

    /* Each subsequent edge takes:   54 + LD_EDGE_1 T = 152 + 16a + 43b T, bd = b + 1                        */
    /* Sync0 maximum:                                                                                        */
    /*   ROM => b = +D4 - +C9 - 1 = 10, a=22 => 224 + 16a + 59b T => 224 + 16*22 + 59*10 T = 1166 T          */
    /*          152 + 16a + 43b = 1166, a=20 => 152 + 16*20 + 43b = 1166 => 43b = 694 => b = 16, bd = 17     */
    /*   b = 16, so the (first) sync pulse must be less than:                                                */
    /*   152 + 16a + 43*16 T = 840 + 16a T                                                                   */
    /* Leader maximum (at timeout):                                                                          */
    /*   ROM => b = +100 - +C9 - 1 = 56, a=22 => 224 + 16a + 59b T => 224 + 16*22 + 59*56 T = 3880 T         */
    /*          152 + 16a + 43b = 3880, a=20 => 152 + 16*20 + 43b = 3880 => 43b = 3408 => b = 79, bd = 80    */
    /*   Notice that this leader maximum is higher than above, so is not needed to be considered             */

    "\x06\xB0"            /* LD_SYNC   LD   B,+B0            7       */
    "\xCD\xCA\xBF"        /*           CALL LD_EDGE_1        17      */
    "\x30\xE2"            /*           JR   NC,LD_BREAK      7 (/12) */
    "\x78"                /*           LD   A,B              4       */
    "\xFE\xC1"            /*           CP   +C1              7       */
    "\x30\xF4"            /*           JR   NC,LD_SYNC       7/12    */

    "\xCD\xCA\xBF"        /*           CALL LD_EDGE_1        17      */
    "\xD0"                /*           RET  NC               5 (/11) */
    "\x26\x00"            /*           LD   H,+00            7       */
    "\x06\x80"            /*           LD   B,+80            7       */
    "\x18\x17"            /*           JR   LD_MARKER        12   = 48S */

    "\x08"                /* LD_LOOP   EX   AF,AF'           4       */
    "\x20\x05"            /*           JR   NZ,LD_FLAG       7/12 = 16F */
    "\xDD\x75\x00"        /*           LD   (IX+00),L        19      */
    "\x18\x09"            /*           JR   LD_NEXT          12   = 42D */

    "\xCB\x11"            /* LD_FLAG   RL   C                4       */
    "\xAD"                /*           XOR  L                4       */
    "\xC0"                /*           RET  NZ               5 (/11) */
    "\x79"                /*           LD   A,C              4       */
    "\x1F"                /*           RRA                   4       */
    "\x4F"                /*           LD   C,A              4       */
    "\x18\x03"            /*           JR   LD_FLNEXT        12   = 37F */

    "\xDD\x2B"            /* LD_NEXT   DEC  IX               10      (We're loading BACKWARD!) */
    "\x1B"                /*           DEC  DE               6       */
    "\x08"                /* LD_FLNEXT EX   AF,AF'           4       */
    "\x06\x82"            /*           LD   B,+82            7       */
    "\x2E\x01"            /* LD_MARKER LD   L,+01            7    = 34D/18F/7S */

    /* The very first (flag) bit takes (S) = 48 + 7 = 55 + LD_EDGE_2 T = 273 + 32a + 43b T                       */
    /* (D) 55 + 32 + 42 + 34 = 163, so                                                                           */
    /* Each first (data) bit takes: 163 + LD_EDGE_2 - 2b T = 295 + 32a + 43b T (ROM = 405 + 32a + 59b T)         */
    /* Each subsequent bit takes:   60 + LD_EDGE_2       T = 278 + 32a + 43b T (ROM = 422 + 32a + 59b T)         */

    "\xCD\xC6\xBF"        /* LD_8_BITS CALL LD_EDGE_2        17      */
    "\xD0"                /*           RET  NC               5 (/11) */
    "\x3E"                /*           LD   A,COMPARE        7    (variable 'bd') */

    "\x00"				// COMPARE !!!

    "\xB8"                /*           CP   B                4       */
    "\xCB\x15"            /*           RL   L                8       */
    "\x06\x80"            /*           LD   B,+80            7       */
    "\x30\xF3"            /*           JR   NC,LD_8_BITS     7/12 = 55D/60N */

    "\x7C"                /*           LD   A,H              4       */
    "\xAD"                /*           XOR  L                4       */
    "\x67"                /*           LD   H,A              4       */
    "\x7A"                /*           LD   A,D              4       */
    "\xB3"                /*           OR   E                4       */
    "\x20\xD3"            /*           JR   NZ,LD_LOOP       (7/) 12 = 32D */
    "\x7C"                /*           LD   A,H                      */
    "\xFE\x01"            /*           CP   +01                      */
    "\xC9"                /*           RET                           */

    /* The LD_EDGE_2 routine takes:  22 + 2 * (98 + 16a) + 43b T = 218 + 32a + 43b T (ROM = 362 + 32a + 59b T), bd = b + 2 */

    "\xCD\xCA\xBF"        /* LD_EDGE_2 CALL LD_EDGE_1        17      */
    "\xD0"                /*           RET  NC               5 (/11) */

    /* The LD_EDGE_1 routine takes:  98 + 16a + 43b T (ROM = 170 + 16a + 59b T, a=22), bd = b + 1 */

    "\x3E"                /* LD_EDGE_1 LD   A,DELAY          7    (variable 'a') */

    "\x00" // DELAY !!!

    "\x3D"                /* LD_DELAY  DEC  A                4       */
    "\x20\xFD"            /*           JR   NZ,LD_DELAY      7/12    */
    "\xA7"                /*           AND  A                4       */

    "\x04"                /* LD_SAMPLE INC  B                4    (variable 'b') */
    "\xC8"                /*           RET  Z                5 (/11) */
    "\xDB\xFE"            /*           IN   A,(+FE)          11      */
    "\xA9"                /*           XOR  C                4       */
    "\xE6\x40"            /*           AND  +40              7       */
    "\x28\xF7"            /*           JR   Z,LD_SAMPLE      7/12 = 43 */

    "\x79"                /*           LD   A,C              4       */
    "\xEE"                /*           XOR  COLOUR           7       */

    "\x00" // XOR COLOUR !!!

    "\x4F"                /*           LD   C,A              4       */
    "\xE6\x07"            /*           AND  +07              7       */
    "\xF6\x08"            /*           OR   +08              7       */
    "\xD3\xFE"            /*           OUT  (+FE),A          11      */
    "\x37"                /*           SCF                   4       */
    "\xC9" };            /*           RET                   10      */

byte tzx_start[10]={'Z','X','T','a','p','e','!',0x1a, 1, 13};

#define tzx_header_size  5+1+17+1
byte tzx_header[tzx_header_size] =
							{	0x10, 0x00, 0x00, 0x13, 0x00,
								0x00, 0x00,			// Basic
								0x11, 0x05, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08, 0x08,		// Filename
								0xff, 0xff,		// Length of Data block
								0x00, 0x00,		// Autostart line number
								0xCB, 0x00,		// Variable Area
								0xff};			// Checksum

byte tzx_header_data[6] = { 0x10, 0x00, 0x00, 0xff, 0xff, 0xff };

struct tzx_turbo_head_str
{
	byte id;
	word pilot;
	word sync1;
	word sync2;
	word zero;
	word one;
	word pilotlen;
	byte usedbits;
	word pause;
	word len1;
	byte len2;	// High byte of len is 0 !
	byte flag;
};

  struct PageOrder_s     /* Each page as found in a Z80 file */
  {
    byte PageNumber;     /* Spectrum value, not Z80 value! (= +3) */
    word PageStart;      /* Normal (non-paged) start address of this 16Kb chunk */
  };                     /* Notice that the 3 blocks for an 48K machine have different block numbers for a 48K and 128K snapshot! */

  struct PageOrder_s      PageOrder48S[4]           = {{255, 0xC000 },	// Loading screen (external)
														{ 1, 0x8000 },  // + With alternate loading screen
                                                        { 2, 0xC000 },  // +
                                                        { 5, 0x4000 }}; // + Must be decompressed !

  struct PageOrder_s      PageOrder128S[9]          = {{255, 0xC000 },	// Loading screen (external)
														{ 2, 0x8000 },  // +
                                                        { 1, 0xC000 },
                                                        { 3, 0xC000 },
                                                        { 4, 0xC000 },
                                                        { 6, 0xC000 },
                                                        { 7, 0xC000 },
                                                        { 0, 0xC000 },  // +
                                                        { 5, 0x4000 }}; // + Must be decompressed

  struct PageOrder_s      PageOrder48N[3]            = {{ 5, 0xC000 },  // + Loading screen from memory
                                                        { 1, 0x8000 },  // +
                                                        { 2, 0xC000 }}; // +

  struct PageOrder_s      PageOrder128N[8]           = {{ 5, 0xC000 },  // + Loading screen from memory
                                                        { 2, 0x8000 },  // +
                                                        { 1, 0xC000 },
                                                        { 3, 0xC000 },
                                                        { 4, 0xC000 },
                                                        { 6, 0xC000 },
                                                        { 7, 0xC000 },
                                                        { 0, 0xC000 }}; // +


byte tzx_turbo_head[20];

bool load_page[8];
bool load_768 = false;

//byte loader_data[768];

int data_pos = 0;

void add_data(void * dat, int len)
{
	memcpy(snap_bin+data_pos, dat, len);
	data_pos+=len;
}

byte calc_checksum(byte data[], int len)
{
	byte xx = 0;

	for (int i=0; i < len; i++)
	{
		xx ^= data[i];
	}
	return xx;
}

void create_main_header()
{
	int len = (LOADERPREPIECE-1)+768;

	// Fill in the name
	int tlen = strlen(loader_name);
	for (int i=0; i < tlen; i++)
	{
		tzx_header[9+i] = loader_name[i];
	}

	// Fill in the length of data
	tzx_header[17] = len&255;
	tzx_header[18] = len>>8;

	int var = (LOADERPREPIECE-1)-21;	// Varables start here
	// Fill in proper variable area
	tzx_header[21] = var&255;
	tzx_header[22] = var>>8;

	// Calc checksum
	tzx_header[23] = calc_checksum(tzx_header+5, 18);
}

void set_loader_speed()
{
	turbo_loader[94] = turbo_vars[speed_value]._Compare;
	turbo_loader[118]= turbo_vars[speed_value]._Delay;
	byte xor_colour;
	if (load_colour == -1)
	{
		load_colour = snap.border;
	}
    if (load_colour == 0x00)
	{
		/* Border is going to be black ? */
		xor_colour = 0x41;                           /* Then use blue as counter colour (black/black cannot be seen....) */
	}
	else
	{
		xor_colour = 0x40 | load_colour;           /* Use the ultimate colour as counter colour for the loading stripes */
	}
	turbo_loader[134]= xor_colour;
}

#define POS_TABLE			0x16C		//  Loader table
#define POS_HL2				0x00C		//2 H'L'
#define POS_DE2				0x00F		//2 D'E'
#define POS_BC2				0x012		//2 B'C'
#define	POS_IY				0x071		//2 IY
#define POS_LAST_ATTR		0x079		//  Last Line Attribute value
#define POS_768_LOAD		0x0A5		//  FF = YES , 00 = NO
#define POS_48K_1			0x0B9		//  0x56 = 48k , 0x57 = 128k
#define POS_48K_2			0x0C7		//  -||-
#define POS_CLEAN_1			0x0C8		//3 ED,B0,00 = LDIR for Clean 768 !
#define POS_CLEAN_2			0x0E8		//3 -||-
#define POS_LAST_AY			0x0E2		//  Last AY out byte
#define POS_SP				0x108		//2 SP
#define POS_IM				0x10B		//  IM: 0=0x46 , 1=0x56 , 2=0x5E
#define POS_DIEI			0x10C		//  DI=0xF3 , EI=0xFB
#define POS_PC				0x10E		//2 PC
#define POS_AYREG			0x110		//2 16 AY registers, first byte = 00 !
#define POS_BORDER			0x131		//  Border Colour
#define POS_PAGE			0x135		//  Current 128 page (if 48k then 0x10)
#define POS_IX				0x136		//2 IX
#define POS_F2				0x138		//  F'
#define POS_A2				0x139		//  A'
#define POS_R				0x13A		//  R
#define POS_I				0x13B		//  I
#define POS_HL				0x13C		//2 HL
#define POS_DE				0x13E		//2 DE
#define POS_BC				0x140		//2 BC
#define POS_F				0x142		//  F
#define POS_A				0x143		//  A

void create_main_data()
{
	char tempc[256];

	int len = (LOADERPREPIECE-1)+768+2;
	// Fill in the length of data
	tzx_header_data[3] = len&255;
	tzx_header_data[4] = len>>8;

	// Fill the loader with appropriate speed values and colour
	set_loader_speed();
	// Copy the loader to its position in the data
	memcpy(loader_data+341+256, turbo_loader, 144);

	data_pos = 0;
	add_data(tzx_header_data, 6);

	// Copy the Game Name here and the info lines
	memcpy(SpectrumBASICData+32, game_name, 32);
	memcpy(SpectrumBASICData+103, info1, 32);
	memcpy(SpectrumBASICData+144, info2, 32);

	add_data(SpectrumBASICData, LOADERPREPIECE-1);

	int loader_start_pos = data_pos;	// Remember where the main loader starts so we can fill the table later
	int loader_table_pos = loader_start_pos + POS_TABLE;

	int pp = loader_start_pos;

	add_data(loader_data, 768);
//	snap_bin[data_pos]= calc_checksum(snap_bin+5, data_pos-5);  // need to recalc this in the end !!!
	int main_checksum = data_pos;

	data_pos++;

	// setup turbo header

	tzx_turbo_head[0]       = 0x11;
	tzx_turbo_head[1]       = turbo_vars[speed_value]._LenPilot&255;
	tzx_turbo_head[2]       = turbo_vars[speed_value]._LenPilot>>8;
	tzx_turbo_head[3]       = turbo_vars[speed_value]._LenSync0&255;
	tzx_turbo_head[4]       = turbo_vars[speed_value]._LenSync0>>8;
	tzx_turbo_head[5]       = turbo_vars[speed_value]._LenSync0&255;
	tzx_turbo_head[6]       = turbo_vars[speed_value]._LenSync0>>8;
	tzx_turbo_head[7]       = turbo_vars[speed_value]._Len0&255;
	tzx_turbo_head[8]       = turbo_vars[speed_value]._Len0>>8;
	tzx_turbo_head[9]       = (turbo_vars[speed_value]._Len0*2)&255;
	tzx_turbo_head[10]      = (turbo_vars[speed_value]._Len0*2)>>8;
	tzx_turbo_head[11]      = (word)((dword)3500000 / turbo_vars[speed_value]._LenPilot)&255;
	tzx_turbo_head[12]      = (word)((dword)3500000 / turbo_vars[speed_value]._LenPilot)>>8;
	tzx_turbo_head[13]      = 8;
	tzx_turbo_head[14]      = 0;
	tzx_turbo_head[15]      = 0;
	tzx_turbo_head[16]      = 0;	// Lowest two filled in later
	tzx_turbo_head[17]      = 0;
	tzx_turbo_head[18]      = 0;	// Highest byte of length is 0 !
	tzx_turbo_head[19]      = 0xaa;

	// We only need to write proper lenght later !

	int num_pages;
	PageOrder_s * page_order;

	if (snap.mode128)
	{
		if (external)
		{
			num_pages = 9;
			page_order = PageOrder128S;
		}
		else
		{
			num_pages = 8;
			page_order = PageOrder128N;
		}
	}
	else
	{
		if (external)
		{
			num_pages = 4;
			page_order = PageOrder48S;
		}
		else
		{
			num_pages = 3;
			page_order = PageOrder48N;
		}
	}


	int loader_table_entry = 0;
	word add;
	byte num;
	word pagelen;
	word loadlen;
	int smallpage;

	byte external_screen[6912];

	int shortpage=2;	// The page which contains loader - 1 on 48k and 2 on 128k !
	int i;

	for (i=0; i < num_pages; i++)
	{
		if (page_order[i].PageStart == 0x8000)
		{
			shortpage = page_order[i].PageNumber;
		}
	}

		// Get the information on which pages need to be loaded in !
	// Pages that are not loaded in are filled with 0 !
	bool load;
	for (i=0; i < 8; i++)
	{
		load = false;
		if (i != shortpage)
		{
			// Pages 0, 2-8 are loaded in FULL
			for (int j=0; j < 16384 && !load; j++)
			{
				if (snap.page[i][j] != 0)
				{
					load = true;
				}
			}
		}
		else
		{
			int j;
			// Page 1 (48k) or 2 (128k) contains loader, so it is loaded in 2 parts
			for (j=0; j < 16384-768 && !load; j++)
			{
				if (snap.page[i][j] != 0)
				{
					load = true;
				}
			}
			for (j=16384-768; j < 16384 && !load_768; j++)
			{
				if (snap.page[i][j] != 0)
				{
					load_768 = true;
				}
			}
		}
		load_page[i] = load;
	}

	for (i=0; i < num_pages; i++)
	{
		num = page_order[i].PageNumber;
		if (num == 255)
		{
			// External Loading screen
			FILE * exfile = NULL;
			exfile = fopen(external_filename, "rb");
			if (exfile == NULL)
			{
				print_error("Could not read the Loading Screen!");
				return;
			}
			fread(external_screen, 1, 6912, exfile);
			fclose(exfile);
			add = page_order[i].PageStart;

			pagelen = 6912;

			crunch_z80(external_screen, pagelen, WorkBuffer, &loadlen);

			// Fill in the table data
			add = add+(pagelen-1);
			byte addhi = (add>>8)&255;

			snap_bin[loader_table_pos+(loader_table_entry*4)] = 0x10;
			snap_bin[loader_table_pos+(loader_table_entry*4)+1] = addhi;
			snap_bin[loader_table_pos+(loader_table_entry*4)+2] = loadlen&255;
			snap_bin[loader_table_pos+(loader_table_entry*4)+3] = loadlen>>8;

			if (loadlen == 0)
			{
				sprintf(tempc,"- Adding Separate Loading Screen (Uncompressed) Len: %04x", 6912);
			}
			else
			{
				sprintf(tempc,"- Adding Separate Loading Screen  (Compressed)  Len: %04x", loadlen);
			}
			print_verbose(tempc);

			if (loadlen == 0)
			{
				loadlen = pagelen;	// The crunch_z80 returns 0 if the block could not be crunched
			}

			// Update the TZX Turbo header with the page length
			tzx_turbo_head[16] = (byte) ((loadlen+2)&255);
			tzx_turbo_head[17] = (byte) ((loadlen+2)>>8);

			add_data(tzx_turbo_head, 20);	// Add the turbo header to the file
			add_data(WorkBuffer, loadlen);	// Add the actual data
			snap_bin[data_pos]= calc_checksum((snap_bin+data_pos)-(loadlen+1), loadlen+1);
			data_pos++;

			loader_table_entry++;
		}
		else
		{
			if (load_page[num])
			{
				// This page needs to be loaded
				add = page_order[i].PageStart;
				pagelen = 16384;

				byte realnum;

				if (snap.mode128)
				{
					if (loader_table_entry == 0)
					{
						realnum = 0x10;
					}
					else
					{
						realnum = num | 0x10;
					}
				}
				else
				{
					realnum = 0x10;
				}

				if (add == 0x8000)
				{
					// Special case - load 768 bytes less
					pagelen-=768;

					realnum = 0x12;	// Always assume 32768 page when loading short !

					smallpage = num;	// Remember which page it was for later !
				}
				// Now crunch the block
				crunch_z80(snap.page[num], pagelen, WorkBuffer, &loadlen);

				if (external && num == 5)
				{
					// Check if external screen is loading and last page selected
					// If so then check if it would overwrite loading screen - if so then load decrunched
					if (loadlen > (16384-6912))
					{
						reverse_block(WorkBuffer, snap.page[num]);
						loadlen = 0;	// Not compressed !
					}
				}

				int reverse_off = 0;

				if (loadlen != 0)
				{
					// Check if it would overwrite itself ?
					if (test_rev_decz80(WorkBuffer, pagelen, loadlen))
					{
						reverse_block(WorkBuffer, snap.page[num]);
						if (pagelen != 16384)
						{
							reverse_off = 768;		// We flipped the whole block, need only lower part !
						}
						loadlen = 0;	// Not compressed !
					}
				}

/*				char ttt[255];
				sprintf(ttt,"page%d.bin",num);
				FILE * fff = fopen(ttt,"wb");
				fwrite(snap.page[num],1,pagelen,fff);
				fclose(fff);
*/
				// Fill in the table data
				add = add+(pagelen-1);
				byte addhi = (add>>8)&255;

				snap_bin[loader_table_pos+(loader_table_entry*4)] = realnum;
				snap_bin[loader_table_pos+(loader_table_entry*4)+1] = addhi;
				snap_bin[loader_table_pos+(loader_table_entry*4)+2] = loadlen&255;
				snap_bin[loader_table_pos+(loader_table_entry*4)+3] = loadlen>>8;

				if (loadlen == 0)
				{
					sprintf(tempc,"- Adding Memory Page %i           (Uncompressed) Len: %04x", num, pagelen);
				}
				else
				{
					sprintf(tempc,"- Adding Memory Page %i            (Compressed)  Len: %04x", num, loadlen);
				}
				print_verbose(tempc);

				if (loadlen == 0)
				{
					loadlen = pagelen;	// The crunch_z80 returns 0 if the block could not be crunched
				}

				// Update the TZX Turbo header with the page length
				tzx_turbo_head[16] = (byte) ((loadlen+2)&255);
				tzx_turbo_head[17] = (byte) ((loadlen+2)>>8);

				add_data(tzx_turbo_head, 20);	// Add the turbo header to the file
				add_data(WorkBuffer+reverse_off, loadlen);	// Add the actual data
				snap_bin[data_pos]= calc_checksum((snap_bin+data_pos)-(loadlen+1), loadlen+1);
				data_pos++;

				loader_table_entry++;
			}
		}
	}

	if (load_768)
	{
		// Area where the loader is was not empty - need to load it in
		print_verbose("- Adding Extra ROM Loading Block");

		tzx_header_data[3] = (768+2)&255;	// Length of extra block
		tzx_header_data[4] = (768+2)>>8;
		tzx_header_data[5] = 0x55;		// Flag is 0x55

		add_data(tzx_header_data, 6);
		int cstart = data_pos-1;
		add_data(&snap.page[smallpage][16384-768],768);
		snap_bin[data_pos]= calc_checksum(snap_bin+cstart, 769);
		data_pos++;

		// Set the 768 load flag to FF !
		snap_bin[pp+POS_768_LOAD] = 0xFF;

	}
	else
	{
		// Set the 768 load flag to 01 !
		snap_bin[pp+POS_768_LOAD] = 0x00;

		// Change the load stuff to LDIR stuff !
		snap_bin[pp+POS_CLEAN_1]   = 0xed;
		snap_bin[pp+POS_CLEAN_1+1] = 0xb0;
		snap_bin[pp+POS_CLEAN_1+2] = 0x00;
		snap_bin[pp+POS_CLEAN_2]   = 0xed;
		snap_bin[pp+POS_CLEAN_2+1] = 0xb0;
		snap_bin[pp+POS_CLEAN_2+2] = 0x00;
	}

	// Now lets fill the registers and stuff

	snap_bin[pp+POS_HL2]   = snap.reg_hl2&255;
	snap_bin[pp+POS_HL2+1] = snap.reg_hl2>>8;

	snap_bin[pp+POS_DE2]   = snap.reg_de2&255;
	snap_bin[pp+POS_DE2+1] = snap.reg_de2>>8;

	snap_bin[pp+POS_BC2]   = snap.reg_bc2&255;
	snap_bin[pp+POS_BC2+1] = snap.reg_bc2>>8;

	snap_bin[pp+POS_IY]    = snap.reg_iy&255;
	snap_bin[pp+POS_IY+1]  = snap.reg_iy>>8;

	snap_bin[pp+POS_LAST_ATTR] = load_colour|(load_colour<<3)|bright;

	if (snap.mode128)
	{
		snap_bin[pp+POS_48K_1] = 0x57;
		snap_bin[pp+POS_48K_2] = 0x57;
	}
	else
	{
		snap_bin[pp+POS_48K_1] = 0x56;
		snap_bin[pp+POS_48K_2] = 0x56;
	}

	snap_bin[pp+POS_LAST_AY] = snap.last_out_fffd;

	snap_bin[pp+POS_SP]   = snap.reg_sp&255;
	snap_bin[pp+POS_SP+1] = snap.reg_sp>>8;

	switch(snap.inter_mode)
	{
	case 0:
		snap_bin[pp+POS_IM] = 0x46;
		break;
	case 1:
		snap_bin[pp+POS_IM] = 0x56;
		break;
	case 2:
		snap_bin[pp+POS_IM] = 0x5E;
		break;
	}

	if (snap.ei)
	{
		snap_bin[pp+POS_DIEI] = 0xfb;
	}
	else
	{
		snap_bin[pp+POS_DIEI] = 0xf3;
	}

	snap_bin[pp+POS_PC]   = snap.reg_pc&255;
	snap_bin[pp+POS_PC+1] = snap.reg_pc>>8;

	snap_bin[pp+POS_BORDER] = load_colour;

	snap_bin[pp+POS_PAGE] = snap.last_out_7ffd;

	snap_bin[pp+POS_IX]   = snap.reg_ix&255;
	snap_bin[pp+POS_IX+1] = snap.reg_ix>>8;

	snap_bin[pp+POS_F2] = snap.reg_f2;
	snap_bin[pp+POS_A2] = snap.reg_a2;

	word rr = (word) snap.reg_r;
	rr-=0x0a;	// Compensate for the instructions after R is loaded !

	snap_bin[pp+POS_R] = rr&255;
	snap_bin[pp+POS_I] = snap.reg_i;

	snap_bin[pp+POS_HL]   = snap.reg_hl&255;
	snap_bin[pp+POS_HL+1] = snap.reg_hl>>8;

	snap_bin[pp+POS_DE]   = snap.reg_de&255;
	snap_bin[pp+POS_DE+1] = snap.reg_de>>8;

	snap_bin[pp+POS_BC]   = snap.reg_bc&255;
	snap_bin[pp+POS_BC+1] = snap.reg_bc>>8;

	snap_bin[pp+POS_F] = snap.reg_f;
	snap_bin[pp+POS_A] = snap.reg_a;

	int ppay = POS_AYREG+1;
	for (i=0; i < 16; i++)
	{
		snap_bin[pp+ppay] = snap.reg_ay[15-i];
		ppay+=2;
	}

	snap_bin[main_checksum]= calc_checksum(snap_bin+5, main_checksum-5);  // need to recalc this in the end!!!
}

void convert_snap()
{
	print_verbose("\nCreating TZX File :");

	FILE * file= NULL;
	file = fopen(out_filename,"wb");

	if (file == NULL)
	{
		print_error("Output File could not be Opened!");
		return;
	}
	data_pos=0;
	add_data(tzx_start, 10);	// TZX start - ZXTape!...

	create_main_header();
	add_data(tzx_header, tzx_header_size);

	fwrite(snap_bin, 1, data_pos, file);

	print_verbose("- Adding Loader");

	// TEST - load the loader from the loader.bin file - will be hard-coded in the program when release !!!
//	FILE * file2 = fopen("loader.bin","rb");
//	fread(loader_data, 1, 768, file2);
//	fclose(file2);


	create_main_data();
	fwrite(snap_bin, 1, data_pos, file);

	fclose(file);
}

int main(int argc, char* argv[])
{

	init_snap();

	if (parse_args(argc, argv))
	{
		if (load_snap())
		{
			convert_snap();
		}
	}

	print_verbose("\nDone!");

	return 0;
}

