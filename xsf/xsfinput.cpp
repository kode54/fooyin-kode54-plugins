/*
 * XSF Plugin
 * Copyright © 2025, Christopher Snowhill <kode54@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "xsfinput.h"

#include "xsfinputdefs.h"
 
#include <QDir>
#include <QRegularExpression>

#include "highly_experimental/Core/psx.h"
#include "highly_experimental/Core/iop.h"
#include "highly_experimental/Core/r3000.h"
#include "highly_experimental/Core/spu.h"
#include "highly_experimental/Core/bios.h"

#include "highly_theoretical/Core/sega.h"

#include "highly_quixotic/Core/qsound.h"

#undef uint8
#undef uint16
#undef uint32

#include <mgba/core/core.h>
#include <mgba/core/blip_buf.h>
#include <mgba-util/vfs.h>
#include <mgba/core/log.h>

#include "lazyusf2/usf/usf.h"

#include "vio2sf/src/vio2sf/desmume/state.h"

#include "sseqplayer/Player.h"
#include "sseqplayer/SDAT.h"

#include "psflib/psflib.h"
#include "psflib/psf2fs.h"

#include "hebios.h"

#include <zlib.h>

# define strdup(s)							      \
  (__extension__							      \
    ({									      \
      const char *__old = (s);						      \
      size_t __len = strlen (__old) + 1;				      \
      char *__new = (char *) malloc (__len);			      \
      (char *) memcpy (__new, __old, __len);				      \
    }))

#define BORK_TIME 0xC0CAC01A

#include <QLoggingCategory>

Q_LOGGING_CATEGORY(XSF_INPUT, "fy.xsfinput")

using namespace Qt::StringLiterals;

constexpr auto BufferLen = 2048;

namespace {

static void GSFLogger(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args)
{
    (void)logger;
    (void)category;
    (void)level;
    (void)format;
    (void)args;
}

static struct mLogger gsf_logger = {
    .log = GSFLogger,
};

static struct xsf_init {
    xsf_init() {
        bios_set_image( hebios, HEBIOS_SIZE );
        psx_init();
        sega_init();
        qsound_init();
        mLogSetDefaultLogger(&gsf_logger);
    }
} _xsf_init;

inline unsigned get_be16( void const* p )
{
    return  (unsigned) ((unsigned char const*) p) [0] << 8 |
            (unsigned) ((unsigned char const*) p) [1];
}

inline unsigned get_le32( void const* p )
{
    return  (unsigned) ((unsigned char const*) p) [3] << 24 |
            (unsigned) ((unsigned char const*) p) [2] << 16 |
            (unsigned) ((unsigned char const*) p) [1] <<  8 |
            (unsigned) ((unsigned char const*) p) [0];
}

inline unsigned get_be32( void const* p )
{
    return  (unsigned) ((unsigned char const*) p) [0] << 24 |
            (unsigned) ((unsigned char const*) p) [1] << 16 |
            (unsigned) ((unsigned char const*) p) [2] <<  8 |
            (unsigned) ((unsigned char const*) p) [3];
}

inline void set_le32( void* p, unsigned n )
{
    ((unsigned char*) p) [0] = (unsigned char) n;
    ((unsigned char*) p) [1] = (unsigned char) (n >> 8);
    ((unsigned char*) p) [2] = (unsigned char) (n >> 16);
    ((unsigned char*) p) [3] = (unsigned char) (n >> 24);
}

static unsigned long parse_time_crap(const char *input)
{
    unsigned long value = 0;
    unsigned long multiplier = 1000;
    const char * ptr = input;
    unsigned long colon_count = 0;
    
    while (*ptr && ((*ptr >= '0' && *ptr <= '9') || *ptr == ':'))
    {
        colon_count += *ptr == ':';
        ++ptr;
    }
    if (colon_count > 2) return BORK_TIME;
    if (*ptr && *ptr != '.' && *ptr != ',') return BORK_TIME;
    if (*ptr) ++ptr;
    while (*ptr && *ptr >= '0' && *ptr <= '9') ++ptr;
    if (*ptr) return BORK_TIME;
    
    ptr = strrchr(input, ':');
    if (!ptr)
        ptr = input;
    for (;;)
    {
        char * end;
        if (ptr != input) ++ptr;
        if (multiplier == 1000)
        {
            double temp = strtod(ptr, &end);
            if (temp >= 60.0) return BORK_TIME;
            value = (long)(temp * 1000.0f);
        }
        else
        {
            unsigned long temp = strtoul(ptr, &end, 10);
            if (temp >= 60 && multiplier < 3600000) return BORK_TIME;
            value += temp * multiplier;
        }
        if (ptr == input) break;
        ptr -= 2;
        while (ptr > input && *ptr != ':') --ptr;
        multiplier *= 60;
    }
    
    return value;
}

struct psf_tag
{
    char * name;
    char * value;
    struct psf_tag * next;
};

static struct psf_tag * add_tag( struct psf_tag * tags, const char * name, const char * value )
{
    struct psf_tag * tag = (struct psf_tag *) malloc( sizeof( struct psf_tag ) );
    if ( !tag ) return tags;

    tag->name = strdup( name );
    if ( !tag->name ) {
        free( tag );
        return tags;
    }
    tag->value = strdup( value );
    if ( !tag->value ) {
        free( tag->name );
        free( tag );
        return tags;
    }
    tag->next = tags;
    return tag;
}

static void free_tags( struct psf_tag * tags )
{
    struct psf_tag * tag, * next;

    tag = tags;

    while ( tag )
    {
        next = tag->next;
        free( tag->name );
        free( tag->value );
        free( tag );
        tag = next;
    }
}

struct psf_info_meta_state
{
    int tag_song_ms;
    int tag_fade_ms;
    
    bool utf8;
    
    struct psf_tag *tags;
};

typedef struct {
    uint32_t pc0;
    uint32_t gp0;
    uint32_t t_addr;
    uint32_t t_size;
    uint32_t d_addr;
    uint32_t d_size;
    uint32_t b_addr;
    uint32_t b_size;
    uint32_t s_ptr;
    uint32_t s_size;
    uint32_t sp,fp,gp,ret,base;
} exec_header_t;

typedef struct {
    char key[8];
    uint32_t text;
    uint32_t data;
    exec_header_t exec;
    char title[60];
} psxexe_hdr_t;

struct psf1_load_state
{
    void * emu;
    bool first;
    unsigned refresh;
};

static int psf1_info(void * context, const char * name, const char * value)
{
    psf1_load_state * state = ( psf1_load_state * ) context;

    if ( !state->refresh && !strcasecmp(name, "_refresh") )
    {
        char *end;
        state->refresh = strtol( value, &end, 10 );
    }

    return 0;
}

int psf1_load(void * context, const uint8_t * exe, size_t exe_size,
              const uint8_t * reserved, size_t reserved_size)
{
    psf1_load_state * state = ( psf1_load_state * ) context;

    psxexe_hdr_t *psx = (psxexe_hdr_t *) exe;

    if ( exe_size < 0x800 ) return -1;

    uint32_t addr = get_le32( &psx->exec.t_addr );
    uint32_t size = (uint32_t)exe_size - 0x800;

    addr &= 0x1fffff;
    if ( ( addr < 0x10000 ) || ( size > 0x1f0000 ) || ( addr + size > 0x200000 ) ) return -1;

    void * pIOP = psx_get_iop_state( state->emu );
    iop_upload_to_ram( pIOP, addr, exe + 0x800, size );

    if ( !state->refresh )
    {
        if (!strncasecmp((const char *) exe + 113, "Japan", 5)) state->refresh = 60;
        else if (!strncasecmp((const char *) exe + 113, "Europe", 6)) state->refresh = 50;
        else if (!strncasecmp((const char *) exe + 113, "North America", 13)) state->refresh = 60;
    }

    if ( state->first )
    {
        void * pR3000 = iop_get_r3000_state( pIOP );
        r3000_setreg(pR3000, R3000_REG_PC, get_le32( &psx->exec.pc0 ) );
        r3000_setreg(pR3000, R3000_REG_GEN+29, get_le32( &psx->exec.s_ptr ) );
        state->first = false;
    }

    return 0;
}

static int EMU_CALL virtual_readfile(void *context, const char *path, int offset, char *buffer, int length)
{
    return psf2fs_virtual_readfile(context, path, offset, buffer, length);
}

struct sdsf_loader_state
{
    uint8_t * data;
    size_t data_size;
};

int sdsf_loader(void * context, const uint8_t * exe, size_t exe_size,
                const uint8_t * reserved, size_t reserved_size)
{
    if ( exe_size < 4 ) return -1;

    struct sdsf_loader_state * state = ( struct sdsf_loader_state * ) context;

    uint8_t * dst = state->data;

    if ( state->data_size < 4 ) {
        state->data = dst = ( uint8_t * ) malloc( exe_size );
        state->data_size = exe_size;
        memcpy( dst, exe, exe_size );
        return 0;
    }

    uint32_t dst_start = get_le32( dst );
    uint32_t src_start = get_le32( exe );
    dst_start &= 0x7fffff;
    src_start &= 0x7fffff;
    size_t dst_len = state->data_size - 4;
    size_t src_len = exe_size - 4;
    if ( dst_len > 0x800000 ) dst_len = 0x800000;
    if ( src_len > 0x800000 ) src_len = 0x800000;

    if ( src_start < dst_start )
    {
        uint32_t diff = dst_start - src_start;
        state->data_size = dst_len + 4 + diff;
        state->data = dst = ( uint8_t * ) realloc( dst, state->data_size );
        memmove( dst + 4 + diff, dst + 4, dst_len );
        memset( dst + 4, 0, diff );
        dst_len += diff;
        dst_start = src_start;
        set_le32( dst, dst_start );
    }
    if ( ( src_start + src_len ) > ( dst_start + dst_len ) )
    {
        size_t diff = ( src_start + src_len ) - ( dst_start + dst_len );
        state->data_size = dst_len + 4 + diff;
        state->data = dst = ( uint8_t * ) realloc( dst, state->data_size );
        memset( dst + 4 + dst_len, 0, diff );
    }

    memcpy( dst + 4 + ( src_start - dst_start ), exe + 4, src_len );

    return 0;
}

struct qsf_loader_state
{
    uint8_t * key;
    uint32_t key_size;

    uint8_t * z80_rom;
    uint32_t z80_size;

    uint8_t * sample_rom;
    uint32_t sample_size;
};

static int psf_info_meta(void * context, const char * name, const char * value)
{
    struct psf_info_meta_state * state = ( struct psf_info_meta_state * ) context;

    if ( !strcasecmp( name, "length" ) )
    {
        unsigned long n = parse_time_crap( value );
        if ( n != BORK_TIME ) state->tag_song_ms = n;
    }
    else if ( !strcasecmp( name, "fade" ) )
    {
        unsigned long n = parse_time_crap( value );
        if ( n != BORK_TIME ) state->tag_fade_ms = n;
    }
    else if ( !strcasecmp( name, "utf8" ) )
    {
        state->utf8 = true;
    }
    else if ( *name != '_' )
    {
        if ( !strcasecmp( name, "game" ) ) name = "album";
        else if ( !strcasecmp( name, "year" ) ) name = "date";
        else if ( !strcasecmp( name, "tracknumber" ) ) name = "track";
        else if ( !strcasecmp( name, "discnumber" ) ) name = "disc";

        state->tags = add_tag( state->tags, name, value );
    }

    return 0;
}

static int qsf_upload_section( struct qsf_loader_state * state, const char * section, uint32_t start,
                           const uint8_t * data, uint32_t size )
{
    uint8_t ** array = NULL;
    uint32_t * array_size = NULL;
    uint32_t max_size = 0x7fffffff;

    if ( !strcmp( section, "KEY" ) ) { array = &state->key; array_size = &state->key_size; max_size = 11; }
    else if ( !strcmp( section, "Z80" ) ) { array = &state->z80_rom; array_size = &state->z80_size; }
    else if ( !strcmp( section, "SMP" ) ) { array = &state->sample_rom; array_size = &state->sample_size; }
    else return -1;

    if ( ( start + size ) < start ) return -1;

    uint32_t new_size = start + size;
    uint32_t old_size = *array_size;
    if ( new_size > max_size ) return -1;

    if ( new_size > old_size ) {
        *array = (uint8_t *) realloc( *array, new_size );
        *array_size = new_size;
        memset( *array + old_size, 0, new_size - old_size );
    }

    memcpy( *array + start, data, size );

    return 0;
}

static int qsf_load(void * context, const uint8_t * exe, size_t exe_size,
                                    const uint8_t * reserved, size_t reserved_size)
{
    struct qsf_loader_state * state = ( struct qsf_loader_state * ) context;

    for (;;) {
        char s[4];
        if ( exe_size < 11 ) break;
        memcpy( s, exe, 3 ); exe += 3; exe_size -= 3;
        s [3] = 0;
        uint32_t dataofs  = get_le32( exe ); exe += 4; exe_size -= 4;
        uint32_t datasize = get_le32( exe ); exe += 4; exe_size -= 4;
        if ( datasize > exe_size )
            return -1;

        if ( qsf_upload_section( state, s, dataofs, exe, datasize ) < 0 )
            return -1;

        exe += datasize;
        exe_size -= datasize;
    }

    return 0;
}

struct gsf_loader_state
{
    int entry_set;
    uint32_t entry;
    uint8_t * data;
    size_t data_size;
};

static int gsf_loader(void * context, const uint8_t * exe, size_t exe_size,
                      const uint8_t * reserved, size_t reserved_size)
{
    if ( exe_size < 12 ) return -1;
    
    struct gsf_loader_state * state = ( struct gsf_loader_state * ) context;
    
    unsigned char *iptr;
    size_t isize;
    unsigned char *xptr;
    unsigned xentry = get_le32(exe + 0);
    unsigned xsize = get_le32(exe + 8);
    unsigned xofs = get_le32(exe + 4) & 0x1ffffff;
    if ( xsize < exe_size - 12 ) return -1;
    if (!state->entry_set)
    {
        state->entry = xentry;
        state->entry_set = 1;
    }
    {
        iptr = state->data;
        isize = state->data_size;
        state->data = 0;
        state->data_size = 0;
    }
    if (!iptr)
    {
        size_t rsize = xofs + xsize;
        {
            rsize -= 1;
            rsize |= rsize >> 1;
            rsize |= rsize >> 2;
            rsize |= rsize >> 4;
            rsize |= rsize >> 8;
            rsize |= rsize >> 16;
            rsize += 1;
        }
        iptr = (unsigned char *) malloc(rsize + 10);
        if (!iptr)
            return -1;
        memset(iptr, 0, rsize + 10);
        isize = rsize;
    }
    else if (isize < xofs + xsize)
    {
        size_t rsize = xofs + xsize;
        {
            rsize -= 1;
            rsize |= rsize >> 1;
            rsize |= rsize >> 2;
            rsize |= rsize >> 4;
            rsize |= rsize >> 8;
            rsize |= rsize >> 16;
            rsize += 1;
        }
        xptr = (unsigned char *) realloc(iptr, xofs + rsize + 10);
        if (!xptr)
        {
            free(iptr);
            return -1;
        }
        iptr = xptr;
        isize = rsize;
    }
    memcpy(iptr + xofs, exe + 12, xsize);
    {
        state->data = iptr;
        state->data_size = isize;
    }
    return 0;
}

struct gsf_running_state
{
    struct mAVStream stream;
    void * rom;
    int16_t samples[BufferLen * 2];
    int buffered;
};

static void _gsf_postAudioBuffer(struct mAVStream * stream, blip_t * left, blip_t * right)
{
    struct gsf_running_state * state = ( struct gsf_running_state * ) stream;
    blip_read_samples(left, state->samples, BufferLen, true);
    blip_read_samples(right, state->samples + 1, BufferLen, true);
    state->buffered = BufferLen;
}

struct usf_loader_state
{
    uint32_t enablecompare;
    uint32_t enablefifofull;

    void * emu_state;
};

static int usf_loader(void * context, const uint8_t * exe, size_t exe_size,
                      const uint8_t * reserved, size_t reserved_size)
{
    struct usf_loader_state * uUsf = ( struct usf_loader_state * ) context;
    if ( exe && exe_size > 0 ) return -1;

    return usf_upload_section( uUsf->emu_state, reserved, reserved_size );
}

static int usf_info(void * context, const char * name, const char * value)
{
    struct usf_loader_state * uUsf = ( struct usf_loader_state * ) context;

    if ( !strcasecmp( name, "_enablecompare" ) && strlen( value ) )
        uUsf->enablecompare = 1;
    else if ( !strcasecmp( name, "_enablefifofull" ) && strlen( value ) )
        uUsf->enablefifofull = 1;

    return 0;
}

struct twosf_loader_state
{
    uint8_t * rom;
    uint8_t * state;
    size_t rom_size;
    size_t state_size;

    int initial_frames;
    int sync_type;
    int clockdown;
    int arm9_clockdown_level;
    int arm7_clockdown_level;

    twosf_loader_state()
    : rom(0), state(0), rom_size(0), state_size(0),
    initial_frames(-1), sync_type(0), clockdown(0),
    arm9_clockdown_level(0), arm7_clockdown_level(0)
    {
    }

    ~twosf_loader_state()
    {
        if (rom) free(rom);
        if (state) free(state);
    }
};

static int load_twosf_map(struct twosf_loader_state *state, int issave, const unsigned char *udata, unsigned usize)
{
    if (usize < 8) return -1;

    unsigned char *iptr;
    size_t isize;
    unsigned char *xptr;
    unsigned xsize = get_le32(udata + 4);
    unsigned xofs = get_le32(udata + 0);
    if (issave)
    {
        iptr = state->state;
        isize = state->state_size;
        state->state = 0;
        state->state_size = 0;
    }
    else
    {
        iptr = state->rom;
        isize = state->rom_size;
        state->rom = 0;
        state->rom_size = 0;
    }
    if (!iptr)
    {
        size_t rsize = xofs + xsize;
        if (!issave)
        {
            rsize -= 1;
            rsize |= rsize >> 1;
            rsize |= rsize >> 2;
            rsize |= rsize >> 4;
            rsize |= rsize >> 8;
            rsize |= rsize >> 16;
            rsize += 1;
        }
        iptr = (unsigned char *) malloc(rsize + 10);
        if (!iptr)
            return -1;
        memset(iptr, 0, rsize + 10);
        isize = rsize;
    }
    else if (isize < xofs + xsize)
    {
        size_t rsize = xofs + xsize;
        if (!issave)
        {
            rsize -= 1;
            rsize |= rsize >> 1;
            rsize |= rsize >> 2;
            rsize |= rsize >> 4;
            rsize |= rsize >> 8;
            rsize |= rsize >> 16;
            rsize += 1;
        }
        xptr = (unsigned char *) realloc(iptr, xofs + rsize + 10);
        if (!xptr)
        {
            free(iptr);
            return -1;
        }
        iptr = xptr;
        isize = rsize;
    }
    memcpy(iptr + xofs, udata + 8, xsize);
    if (issave)
    {
        state->state = iptr;
        state->state_size = isize;
    }
    else
    {
        state->rom = iptr;
        state->rom_size = isize;
    }
    return 0;
}

static int load_twosf_mapz(struct twosf_loader_state *state, int issave, const unsigned char *zdata, unsigned zsize, unsigned zcrc)
{
    int ret;
    int zerr;
    uLongf usize = 8;
    uLongf rsize = usize;
    unsigned char *udata;
    unsigned char *rdata;

    udata = (unsigned char *) malloc(usize);
    if (!udata)
        return -1;

    while (Z_OK != (zerr = uncompress(udata, &usize, zdata, zsize)))
    {
        if (Z_MEM_ERROR != zerr && Z_BUF_ERROR != zerr)
        {
            free(udata);
            return -1;
        }
        if (usize >= 8)
        {
            usize = get_le32(udata + 4) + 8;
            if (usize < rsize)
            {
                rsize += rsize;
                usize = rsize;
            }
            else
                rsize = usize;
        }
        else
        {
            rsize += rsize;
            usize = rsize;
        }
        rdata = (unsigned char *) realloc(udata, usize);
        if (!rdata)
        {
            free(udata);
            return -1;
        }
        udata = rdata;
    }

    rdata = (unsigned char *) realloc(udata, usize);
    if (!rdata)
    {
        free(udata);
        return -1;
    }

    if (0)
    {
        uLong ccrc = crc32(crc32(0L, Z_NULL, 0), rdata, (uInt) usize);
        if (ccrc != zcrc)
            return -1;
    }

    ret = load_twosf_map(state, issave, rdata, (unsigned) usize);
    free(rdata);
    return ret;
}

static int twosf_loader(void * context, const uint8_t * exe, size_t exe_size,
                        const uint8_t * reserved, size_t reserved_size)
{
    struct twosf_loader_state * state = ( struct twosf_loader_state * ) context;

    if ( exe_size >= 8 )
    {
        if ( load_twosf_map(state, 0, exe, (unsigned) exe_size) )
            return -1;
    }

    if ( reserved_size )
    {
        size_t resv_pos = 0;
        if ( reserved_size < 16 )
            return -1;
        while ( resv_pos + 12 < reserved_size )
        {
            unsigned save_size = get_le32(reserved + resv_pos + 4);
            unsigned save_crc = get_le32(reserved + resv_pos + 8);
            if (get_le32(reserved + resv_pos + 0) == 0x45564153)
            {
                if (resv_pos + 12 + save_size > reserved_size)
                    return -1;
                if (load_twosf_mapz(state, 1, reserved + resv_pos + 12, save_size, save_crc))
                    return -1;
            }
            resv_pos += 12 + save_size;
        }
    }

    return 0;
}

static int twosf_info(void * context, const char * name, const char * value)
{
    struct twosf_loader_state * state = ( struct twosf_loader_state * ) context;
    char *end;

    if ( !strcasecmp( name, "_frames" ) )
    {
        state->initial_frames = (int)strtol( value, &end, 10 );
    }
    else if ( !strcasecmp( name, "_clockdown" ) )
    {
        state->clockdown = (int)strtol( value, &end, 10 );
    }
    else if ( !strcasecmp( name, "_vio2sf_sync_type") )
    {
        state->sync_type = (int)strtol( value, &end, 10 );
    }
    else if ( !strcasecmp( name, "_vio2sf_arm9_clockdown_level" ) )
    {
        state->arm9_clockdown_level = (int)strtol( value, &end, 10 );
    }
    else if ( !strcasecmp( name, "_vio2sf_arm7_clockdown_level" ) )
    {
        state->arm7_clockdown_level = (int)strtol( value, &end, 10 );
    }

    return 0;
}

struct ncsf_loader_state {
	uint32_t sseq;
	std::vector<uint8_t> sdatData;
	std::unique_ptr<SDAT> sdat;

	std::vector<uint8_t> outputBuffer;

	ncsf_loader_state()
	: sseq(0) {
	}
};

static int ncsf_loader(void *context, const uint8_t *exe, size_t exe_size,
                       const uint8_t *reserved, size_t reserved_size) {
	struct ncsf_loader_state *state = (struct ncsf_loader_state *)context;

	if(reserved_size >= 4) {
		state->sseq = get_le32(reserved);
	}

	if(exe_size >= 12) {
		uint32_t sdat_size = get_le32(exe + 8);
		if(sdat_size > exe_size) return -1;

		if(state->sdatData.empty())
			state->sdatData.resize(sdat_size, 0);
		else if(state->sdatData.size() < sdat_size)
			state->sdatData.resize(sdat_size);
		memcpy(&state->sdatData[0], exe, sdat_size);
	}

	return 0;
}

static void * psf_file_fopen( void *context, const char * uri )
{
    (void)context;
    return fopen( uri, "rb" );
}

static size_t psf_file_fread( void * buffer, size_t size, size_t count, void * handle )
{
    return fread( buffer, size, count, (FILE *) handle );
}

static int psf_file_fseek( void * handle, int64_t offset, int whence )
{
    return fseek( (FILE *) handle, offset, whence );
}

static int psf_file_fclose( void * handle )
{
    fclose( (FILE *) handle );
    return 0;
}

static long psf_file_ftell( void * handle )
{
    return ftell( (FILE *) handle );
}

const psf_file_callbacks psf_file_system =
{
    "\\/|:",
    NULL,
    psf_file_fopen,
    psf_file_fread,
    psf_file_fseek,
    psf_file_fclose,
    psf_file_ftell
};

int
get_srate(int version)
{
    switch (version)
    {
        case 1: case 0x11: case 0x12: case 0x21:
        case 0x22: case 0x24: case 0x25:
            return 44100;
            
        case 2:
            return 48000;

        case 0x41:
            return 24038;
    }
    return -1;
}

static void
psf_error_log(void * unused, const char * message) {
    fprintf(stderr, "%s", message);
}


QStringList fileExtensions()
{
    static const QStringList extensions = {u"psf"_s, u"minipsf"_s, u"psf2"_s, u"minipsf2"_s, u"ssf"_s, u"minissf"_s, u"dsf"_s, u"minidsf"_s, u"qsf"_s, u"miniqsf"_s, u"usf"_s, u"miniusf"_s, u"gsf"_s, u"minigsf"_s, u"2sf"_s, u"mini2sf"_s, u"ncsf"_s, u"minincsf"_s};
    return extensions;
}
 
} // namespace

namespace Fooyin::XSFInput {
XSFDecoder::XSFDecoder()
{
    m_format.setSampleFormat(Fooyin::SampleFormat::S16);
    m_format.setChannelCount(2);
    m_emulator = NULL;
    m_emulatorExtra = NULL;
}

QStringList XSFDecoder::extensions() const
{
    return fileExtensions();
}

bool XSFDecoder::isSeekable() const
{
    return true;
}

bool XSFDecoder::trackHasChanged() const
{
    return m_changedTrack.isValid();
}
 
Fooyin::Track XSFDecoder::changedTrack() const
{
    return m_changedTrack;
}

void XSFDecoder::emu_cleanup()
{
    if (m_version == 0x02) {
        if(m_emulator) {
            free(m_emulator);
        }
        if(m_emulatorExtra) {
            psf2fs_delete(m_emulatorExtra);
        }
    } else if (m_version == 0x21) {
        if(m_emulator) {
            usf_shutdown(m_emulator);
            free(m_emulator);
        }
    } else if (m_version == 0x22) {
        if(m_emulator) {
            struct mCore * core = ( struct mCore * ) m_emulator;
            core->deinit(core);
        }
        if (m_emulatorExtra)
        {
            struct gsf_running_state * rstate = ( struct gsf_running_state * ) m_emulatorExtra;
            free( rstate->rom );
            free( rstate );
        }
    } else if (m_version == 0x24) {
        if(m_emulator) {
            NDS_state * state = (NDS_state *) m_emulator;
            state_deinit(state);
            free(state);
        }
        if(m_emulatorExtra) {
            free(m_emulatorExtra);
        }
    } else if (m_version == 0x25) {
        {
            if(m_emulator) {
			    Player *player = (Player *)m_emulator;
			    delete player;
            }
            if(m_emulatorExtra) {
		        struct ncsf_loader_state *state = (struct ncsf_loader_state *) m_emulatorExtra;
    		    delete state;
            }
        }
    } else if(m_version == 0x41) {
        if(m_emulator) {
            free(m_emulator);
        }
        if(m_emulatorExtra) {
            struct qsf_loader_state * state = (struct qsf_loader_state *) m_emulatorExtra;
            free(state->key);
            free(state->z80_rom);
            free(state->sample_rom);
            free(state);
        }
    } else {
        if(m_emulator) {
            free(m_emulator);
        }
    }
    m_emulator = NULL;
    m_emulatorExtra = NULL;
}

int XSFDecoder::emu_init() {
    emu_cleanup();
    
    if (m_version == 1 || m_version == 2)
    {
        m_emulator = malloc(psx_get_state_size(m_version));

        if (!m_emulator) {
            return -1;
        }

        psx_clear_state(m_emulator, m_version);

        if (m_version == 1) {
            psf1_load_state state;

            state.emu = m_emulator;
            state.first = true;
            state.refresh = 0;

            if (psf_load(m_path.toUtf8().constData(), &psf_file_system, 1, psf1_load, &state, psf1_info, &state, 1, psf_error_log, 0) <= 0) {
                return -1;
            }

            if (state.refresh)
                psx_set_refresh(m_emulator, state.refresh);
        }
        else if (m_version == 2)
        {
            m_emulatorExtra = psf2fs_create();
            if (!m_emulatorExtra) {
                return -1;
            }

            psf1_load_state state;

            state.refresh = 0;

            if (psf_load(m_path.toUtf8().constData(), &psf_file_system, 2, psf2fs_load_callback, m_emulatorExtra, psf1_info, &state, 1, psf_error_log, 0) <= 0) {
                return -1;
            }

            if (state.refresh)
                psx_set_refresh(m_emulator, state.refresh);

            psx_set_readfile(m_emulator, virtual_readfile, m_emulatorExtra);
        }
    }
    else if (m_version == 0x11 || m_version == 0x12)
    {
        struct sdsf_loader_state state;
        memset(&state, 0, sizeof(state));

        if (psf_load(m_path.toUtf8().constData(), &psf_file_system, m_version, sdsf_loader, &state, 0, 0, 0, psf_error_log, 0) <= 0) {
            return -1;
        }

        m_emulator = malloc(sega_get_state_size(m_version - 0x10));

        if (!m_emulator) {
            free(state.data);
            return -1;
        }

        sega_clear_state(m_emulator, m_version - 0x10);

        sega_enable_dry(m_emulator, 1);
        sega_enable_dsp(m_emulator, 1);

        sega_enable_dsp_dynarec(m_emulator, 0);

        uint32_t start = get_le32(state.data);
        size_t length = state.data_size;
        const size_t max_length = (m_version == 0x12) ? 0x800000 : 0x80000;
        if ((start + (length - 4)) > max_length)
            length = max_length - start + 4;
        sega_upload_program(m_emulator, state.data, (uint32_t)length);

        free(state.data);
    }
    else if (m_version == 0x21)
    {
        struct usf_loader_state state;
        memset(&state, 0, sizeof(state));

        state.emu_state = malloc(usf_get_state_size());
        if (!state.emu_state) {
            return -1;
        }

        usf_clear(state.emu_state);

        usf_set_hle_audio(state.emu_state, 1);

        m_emulator = (void *) state.emu_state;

        if (psf_load(m_path.toUtf8().constData(), &psf_file_system, 0x21, usf_loader, &state, usf_info, &state, 1, psf_error_log, 0) <= 0) {
            return -1;
        }

        usf_set_compare(state.emu_state, state.enablecompare);
        usf_set_fifo_full(state.emu_state, state.enablefifofull);
    }
    else if (m_version == 0x22)
    {
        struct gsf_loader_state state;
        memset(&state, 0, sizeof(state));

        if (psf_load(m_path.toUtf8().constData(), &psf_file_system, 0x22, gsf_loader, &state, 0, 0, 0, psf_error_log, 0) <= 0) {
            return -1;
        }

        if (state.data_size > UINT_MAX) {
            free(state.data);
            return -1;
        }

        struct VFile * rom = VFileFromConstMemory(state.data, state.data_size);
        if ( !rom ) {
            free( state.data );
            return -1;
        }

        struct mCore * core = mCoreFindVF( rom );
        if ( !core ) {
            free(state.data);
            return -1;
        }

        struct gsf_running_state * rstate = (struct gsf_running_state *) calloc(1, sizeof(struct gsf_running_state));
        if ( !rstate ) {
            core->deinit(core);
            free(state.data);
            return -1;
        }

        rstate->rom = state.data;
        rstate->stream.postAudioBuffer = _gsf_postAudioBuffer;

        core->init(core);
        core->setAVStream(core, &rstate->stream);
        mCoreInitConfig(core, NULL);

        core->setAudioBufferSize(core, BufferLen);

        blip_set_rates(core->getAudioChannel(core, 0), core->frequency(core), 44100);
        blip_set_rates(core->getAudioChannel(core, 1), core->frequency(core), 44100);

        struct mCoreOptions opts = {
            .skipBios = true,
            .useBios = false,
            .sampleRate = 44100,
            .volume = 0x100,
        };

        mCoreConfigLoadDefaults(&core->config, &opts);

        core->loadROM(core, rom);
        core->reset(core);

        m_emulator = (void *) core;
        m_emulatorExtra = (void *) rstate;
    }
    else if (m_version == 0x24)
    {
        struct twosf_loader_state state;
        memset(&state, 0, sizeof(state));

        NDS_state * nds_state = (NDS_state *) calloc(1, sizeof(*nds_state));
        if (!nds_state) {
            return -1;
        }

        m_emulator = (void *) nds_state;

        if (state_init(nds_state)) {
            return -1;
        }

        if (psf_load(m_path.toUtf8().constData(), &psf_file_system, 0x24, twosf_loader, &state, twosf_info, &state, 1, psf_error_log, 0) <= 0) {
            return -1;
        }

        if (!state.arm7_clockdown_level)
            state.arm7_clockdown_level = state.clockdown;
        if (!state.arm9_clockdown_level)
            state.arm9_clockdown_level = state.clockdown;

        nds_state->dwInterpolation = 1;
        nds_state->dwChannelMute = 0;

        nds_state->initial_frames = state.initial_frames;
        nds_state->sync_type = state.sync_type;
        nds_state->arm7_clockdown_level = state.arm7_clockdown_level;
        nds_state->arm9_clockdown_level = state.arm9_clockdown_level;

        if (state.rom)
            state_setrom(nds_state, state.rom, (u32)state.rom_size, 0);

        state_loadstate(nds_state, state.state, (u32)state.state_size);

        m_emulatorExtra = state.rom;
        state.rom = 0; // So twosf_loader_state doesn't free it when it goes out of scope
	}
    else if (m_version == 0x25)
    {
		struct ncsf_loader_state *state = new struct ncsf_loader_state;

		if(psf_load(m_path.toUtf8().constData(), &psf_file_system, 0x25, ncsf_loader, state, 0, 0, 1, psf_error_log, 0) <= 0) {
			delete state;
			return -1;
		}

		Player *player = new Player;

		player->interpolation = INTERPOLATION_SINC;

		PseudoFile file;
		file.data = &state->sdatData;

		state->sdat.reset(new SDAT(file, state->sseq));

		auto *sseqToPlay = state->sdat->sseq.get();

		player->sampleRate = 44100;
		player->Setup(sseqToPlay);
		player->Timer();

		state->outputBuffer.resize(BufferLen * sizeof(int16_t) * 2);

		m_emulator = (void *) player;
		m_emulatorExtra = (void *) state;
    }
    else if (m_version == 0x41)
    {
        struct qsf_loader_state * state = (struct qsf_loader_state *) calloc(1, sizeof(*state));

        if (!state) {
            return -1;
        }

        m_emulatorExtra = (void *) state;

        if ( psf_load(m_path.toUtf8().constData(), &psf_file_system, 0x41, qsf_load, state, 0, 0, 0, psf_error_log, 0) <= 0 ) {
            return -1;
        }

        m_emulator = malloc(qsound_get_state_size());
        if (!m_emulator) {
            return -1;
        }

        qsound_clear_state(m_emulator);

        if(state->key_size == 11) {
            uint8_t * ptr = state->key;
            uint32_t swap_key1 = get_be32(ptr +  0);
            uint32_t swap_key2 = get_be32(ptr +  4);
            uint32_t addr_key  = get_be16(ptr +  8);
            uint8_t  xor_key   =        *(ptr + 10);
            qsound_set_kabuki_key(m_emulator, swap_key1, swap_key2, addr_key, xor_key);
        } else {
            qsound_set_kabuki_key(m_emulator, 0, 0, 0, 0);
        }
        qsound_set_z80_rom(m_emulator, state->z80_rom, state->z80_size);
        qsound_set_sample_rom(m_emulator, state->sample_rom, state->sample_size);
    } else {
        return -1;
    }

    return 0;
}

int XSFDecoder::emu_render(int16_t* buf, unsigned& count)
{
    int err = 0;
    const char* errmsg;
    switch (m_version)
    {
        case 1:
        case 2:
            err = psx_execute( m_emulator, 0x7FFFFFFF, buf, &count, 0 );
            break;

        case 0x11:
        case 0x12:
            err = sega_execute( m_emulator, 0x7FFFFFFF, buf, &count );
            break;

        case 0x21:
            errmsg = usf_render_resampled( m_emulator, buf, count, 44100 );
            if (errmsg) {
                err = -1;
            }
            break;

        case 0x22:
        {
            struct mCore * core = ( struct mCore * ) m_emulator;
            struct gsf_running_state * rstate = ( struct gsf_running_state * ) m_emulatorExtra;

            unsigned long frames_to_render = count;

            do {
                unsigned long frames_rendered = rstate->buffered;

                if ( frames_rendered >= frames_to_render ) {
                    if (buf) memcpy( buf, rstate->samples, frames_to_render * 4 );
                    frames_rendered -= frames_to_render;
                    memcpy( rstate->samples, rstate->samples + frames_to_render * 2, frames_rendered * 4 );
                    frames_to_render = 0;
                } else {
                    if (buf) {
                        memcpy( buf, rstate->samples, frames_rendered * 4 );
                        buf = (int16_t *)(((uint8_t *) buf) + frames_rendered * 4);
                    }
                    frames_to_render -= frames_rendered;
                    frames_rendered = 0;
                }
                rstate->buffered = (int) frames_rendered;

                if (frames_to_render) {
                    while ( !rstate->buffered )
                        core->runFrame(core);
                }
            }
            while (frames_to_render);
            count -= (unsigned) frames_to_render;
        }
            break;

        case 0x24:
            state_render( (NDS_state *)m_emulator, buf, count );
            break;

        case 0x25:
        {
            Player *player = (Player *)m_emulator;
            ncsf_loader_state *state = (ncsf_loader_state *)m_emulatorExtra;
            std::vector<uint8_t> &buffer = state->outputBuffer;
            unsigned long frames_to_do = count;
            while(frames_to_do) {
                unsigned frames_this_run = BufferLen;
                if(frames_this_run > frames_to_do)
                    frames_this_run = (unsigned int)frames_to_do;
                player->GenerateSamples(buffer, 0, frames_this_run);
                if (buf) {
                    memcpy(buf, &buffer[0], frames_this_run * sizeof(int16_t) * 2);
                    buf += frames_this_run * 2;
                }
                frames_to_do -= frames_this_run;
            }
        }
            break;

        case 0x41:
            err = qsound_execute( m_emulator, 0x7FFFFFFF, buf, &count );
            break;
    }
    if ( !count ) return -1;
    return err;
}

std::optional<Fooyin::AudioFormat> XSFDecoder::init(const Fooyin::AudioSource& source, const Fooyin::Track& track, DecoderOptions options)
{
    if(track.isInArchive()) {
        return {};
    }

    m_path = track.filepath();

    struct psf_info_meta_state info_state;
    memset(&info_state, 0, sizeof(info_state));
    
    int psf_version = psf_load(m_path.toUtf8().constData(), &psf_file_system, 0, 0, 0, psf_info_meta, &info_state, 0, psf_error_log, 0);
    if(psf_version < 0) {
        return {};
    }

    free_tags(info_state.tags);

    m_version = psf_version;

    if(emu_init() < 0) {
        return {};
    }

    int srate = get_srate(psf_version);
    if(srate < 0) {
        return {};
    }
    m_format.setSampleRate(srate);

    int tag_song_ms = info_state.tag_song_ms;
    int tag_fade_ms = info_state.tag_fade_ms;

    if(!tag_song_ms) {
        tag_song_ms = m_settings.value(MaxLength, DefaultMaxLength).toInt() * 60 * 1000;
        tag_fade_ms = m_settings.value(FadeLength, DefaultFadeLength).toInt();
    }

    framesRead = 0;
    framesLength = m_format.framesForDuration(tag_song_ms);
    framesFade = m_format.framesForDuration(tag_fade_ms);
    totalFrames = framesLength + framesFade;

    return m_format;
 }
 
void XSFDecoder::start()
{
    emu_init();
    framesRead = 0;
}
 
void XSFDecoder::stop()
{
    emu_cleanup();
    m_changedTrack = {};
}

void XSFDecoder::seek(uint64_t pos)
{
    uint64_t framesTarget = m_format.framesForDuration(pos);
    if(framesTarget < framesRead) {
        emu_init();
        framesRead = 0;
    }
    while(framesRead < framesTarget) {
        unsigned toSkip = BufferLen;
        if(toSkip > framesTarget - framesRead) toSkip = (unsigned)(framesTarget - framesRead);
        if(emu_render(NULL, toSkip) < 0) {
            break;
        }
        framesRead += toSkip;
    }
}

Fooyin::AudioBuffer XSFDecoder::readBuffer(size_t bytes)
{
    if(framesRead >= totalFrames)
    {
        return {};
    }

    const auto startTime = static_cast<uint64_t>(m_format.durationForFrames(framesRead));

    AudioBuffer buffer{m_format, startTime};
    buffer.resize(bytes);

    const int frames = m_format.framesForBytes(static_cast<int>(bytes));
    int framesWritten{0};
    while(framesWritten < frames) {
        unsigned framesToWrite = std::min(frames - framesWritten, BufferLen);
        const int bufferPos     = m_format.bytesForFrames(framesWritten);
        int16_t* framesOut = (int16_t *)(buffer.data() + bufferPos);
        if(emu_render(framesOut, framesToWrite) < 0) {
            return {};
        }
        framesWritten += framesToWrite;
    }
    if(framesWritten + framesRead > framesLength)
    {
        if(framesFade)
        {
            long fadeStart = (framesLength > framesRead) ? framesLength : framesRead;
            long fadeEnd = (framesRead + framesWritten > totalFrames) ? totalFrames : (framesRead + framesWritten);
            long fadePos;

            int16_t* buff = (int16_t *)(buffer.data()) + fadeStart - framesRead;

            float fadeScale = (float)(framesFade - (fadeStart - framesLength)) / framesFade;
            float fadeStep = 1.0f / (float)framesFade;
            for(fadePos = fadeStart; fadePos < fadeEnd; ++fadePos)
            {
                buff[0] *= fadeScale;
                buff[1] *= fadeScale;
                buff += 2;
                fadeScale += fadeStep;
                if(fadeScale < 0.f)
                {
                    fadeScale = 0.f;
                    fadeStep = 0.f;
                }
            }
        }

        if(framesRead + framesWritten > totalFrames) {
            size_t newFramesWritten = totalFrames - framesRead;
            int16_t* buff = ((int16_t *)(buffer.data() + m_format.bytesForFrames(newFramesWritten)));
            memset(buff, 0, m_format.bytesForFrames(framesWritten - newFramesWritten));
        }
    }
    framesRead += framesWritten;
 
    return buffer;
}
 
QStringList XSFReader::extensions() const
{
    return fileExtensions();
}

bool XSFReader::canReadCover() const
{
    return false;
}

bool XSFReader::canWriteMetaData() const
{
    return false;
}
 
bool XSFReader::readTrack(const AudioSource& source, Track& track)
{
    if(track.isInArchive()) {
        return false;
    }

    struct psf_info_meta_state state;
    memset( &state, 0, sizeof(state) );

    QString path = track.filepath();

    int psf_version = psf_load( path.toUtf8().constData(), &psf_file_system, 0, 0, 0, psf_info_meta, &state, 0, psf_error_log, 0 );
    if(psf_version < 0) {
        return false;
    }

    const FySettings settings;
 
    int tag_song_ms = state.tag_song_ms;
    int tag_fade_ms = state.tag_fade_ms;

    if(!tag_song_ms) {
        tag_song_ms = settings.value(MaxLength, DefaultMaxLength).toInt() * 60 * 1000;
        tag_fade_ms = settings.value(FadeLength, DefaultFadeLength).toInt();
    }

    long totalFrames = tag_song_ms + tag_fade_ms;
    int SampleRate = get_srate(psf_version);
    if(SampleRate < 0) {
        return false;
    }
 
    track.setDuration(static_cast<uint64_t>(totalFrames));
    track.setSampleRate(static_cast<int>(SampleRate));
    track.setBitDepth(16);
    track.setChannels(2);
    track.setEncoding(u"Synthesized"_s);

    struct psf_tag * tag = state.tags;
    while( tag ) {
        QString name, value;
        if( state.utf8 ) {
            name = QString::fromUtf8(tag->name);
            value = QString::fromUtf8(tag->value);
        } else {
            name = QString::fromLocal8Bit(tag->name);
            value = QString::fromLocal8Bit(tag->value);
        }
        if(!strcasecmp(tag->name, "TITLE")) {
            track.setTitle(value);
        } else if(!strcasecmp(tag->name, "ARTIST")) {
            track.setArtists({value});
        } else if(!strcasecmp(tag->name, "ALBUM")) {
            track.setAlbum(value);
        } else if(!strcasecmp(tag->name, "DATE")) {
            track.setDate(value);
        } else if(!strcasecmp(tag->name, "GENRE")) {
            track.setGenres({value});
        } else if(!strcasecmp(tag->name, "COMMENT")) {
            track.setComment({value});
        } else if(!strncasecmp(tag->name, "REPLAYGAIN_", 11)) {
            char* end;
            float fval = strtod(tag->value, &end);
            if(!strcasecmp(tag->name + 11, "ALBUM_GAIN")) {
                track.setRGAlbumGain(fval);
            } else if(!strcasecmp(tag->name + 11, "ALBUM_PEAK")) {
                track.setRGAlbumPeak(fval);
            } else if(!strcasecmp(tag->name + 11, "TRACK_GAIN")) {
                track.setRGTrackGain(fval);
            } else if(!strcasecmp(tag->name + 11, "TRACK_PEAK")) {
                track.setRGTrackPeak(fval);
            }
        } else {
            track.addExtraTag(name, value);
        }
        tag = tag->next;
    }

    free_tags( state.tags );
 
    return true;
}
} // namespace Fooyin::XSFInput
 