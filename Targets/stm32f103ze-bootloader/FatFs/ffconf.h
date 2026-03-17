/*---------------------------------------------------------------------------/
/  FatFs R0.15 — Bootloader configuration (minimal, exFAT, polling SDIO)
/---------------------------------------------------------------------------*/

#define FFCONF_DEF	80286

/*--- Function Configurations ---*/

#define FF_FS_READONLY	0	/* Need f_rename, f_unlink, f_write (ota_status.txt) */
#define FF_FS_MINIMIZE	0	/* Need f_stat, f_unlink, f_rename for OTA */
#define FF_USE_FIND	0
#define FF_USE_MKFS	0	/* No formatting in bootloader */
#define FF_USE_FASTSEEK	0
#define FF_USE_EXPAND	0
#define FF_USE_CHMOD	0
#define FF_USE_LABEL	0
#define FF_USE_FORWARD	0
#define FF_USE_STRFUNC	0
#define FF_PRINT_LLI	0
#define FF_PRINT_FLOAT	0
#define FF_STRF_ENCODE	0

/*--- Locale and Namespace ---*/

#define FF_CODE_PAGE	437
#define FF_USE_LFN	1	/* LFN on stack (required for exFAT) */
#define FF_MAX_LFN	32	/* Short names only for OTA files */
#define FF_LFN_UNICODE	0
#define FF_LFN_BUF	32
#define FF_SFN_BUF	12
#define FF_FS_RPATH	0

/*--- Drive/Volume ---*/

#define FF_VOLUMES	1
#define FF_STR_VOLUME_ID	0
#define FF_VOLUME_STRS	"SD"
#define FF_MULTI_PARTITION	0
#define FF_MIN_SS	512
#define FF_MAX_SS	512
#define FF_LBA64	0
#define FF_MIN_GPT	0x10000000
#define FF_USE_TRIM	0

/*--- System ---*/

#define FF_FS_TINY	1	/* Tiny mode — no per-file sector buffer, saves 512B RAM */
#define FF_FS_EXFAT	1	/* SD card is exFAT formatted */
#define FF_FS_NORTC	1
#define FF_NORTC_MON	3
#define FF_NORTC_MDAY	17
#define FF_NORTC_YEAR	2026
#define FF_FS_NOFSINFO	0
#define FF_FS_LOCK	0
#define FF_FS_REENTRANT	0
#define FF_FS_TIMEOUT	1000

/*--- End of configuration ---*/
