/*---------------------------------------------------------------------------/
/  FatFs - FAT file system module configuration file  R0.11 (C)ChaN, 2015
/---------------------------------------------------------------------------*/

#ifndef _FFCONF
#define _FFCONF 32020

/*---------------------------------------------------------------------------/
/ Functions and Buffer Configurations
/---------------------------------------------------------------------------*/

#define	_FS_TINY                0
#define _FS_READONLY            0
#define _FS_MINIMIZE            1	/* Remove stat/unlink/mkdir/etc. */
#define	_USE_STRFUNC            0
#define _USE_FIND               0
#define	_USE_MKFS               1	/* Enable f_mkfs() for formatting */
#define	_USE_FASTSEEK           0
#define _USE_LABEL              0
#define	_USE_FORWARD            0
#define _USE_BUFF_WO_ALIGNMENT  0

/*---------------------------------------------------------------------------/
/ Locale and Namespace Configurations
/---------------------------------------------------------------------------*/

#define _CODE_PAGE              437	/* US ASCII */
#define	_USE_LFN                0	/* Disable LFN (8.3 only, no heap needed) */
#define	_MAX_LFN                255
#define	_LFN_UNICODE            0
#define _STRF_ENCODE            3
#define _FS_RPATH               0

/*---------------------------------------------------------------------------/
/ Drive/Volume Configurations
/---------------------------------------------------------------------------*/

#define _VOLUMES                1
#define _STR_VOLUME_ID          0
#define _VOLUME_STRS            "SD"
#define	_MULTI_PARTITION        0
#define	_MIN_SS                 512
#define	_MAX_SS                 512
#define	_USE_TRIM               0
#define _FS_NOFSINFO            0

/*---------------------------------------------------------------------------/
/ System Configurations
/---------------------------------------------------------------------------*/

#define _FS_NORTC               1
#define _NORTC_MON              3
#define _NORTC_MDAY             9
#define _NORTC_YEAR             2026

#define	_FS_LOCK                0
#define _FS_REENTRANT           0
#define _FS_TIMEOUT             1000
#define	_SYNC_t                 void*

#define _WORD_ACCESS            0

#endif /* _FFCONF */
