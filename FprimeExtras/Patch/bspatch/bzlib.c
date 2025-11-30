// ======================================================================
// \title  bspatch.c
// \author starchmd
// \brief  C file containing modified bzlib implementation
// \copyright Copyright (C) 1996-2010 Julian Seward <jseward@acm.org>
// \copyright Copyright (C) 2025 Michael Starch (modified for flight)
// ======================================================================
#include "bzlib_private.h"
#include <stdint.h>
#include <stdbool.h>

//! \brief Global static decompression state: ~64 KB
static DState STATE;

//! \brief Initialize a bz_stream for decompression
static bz_stream STREAM; // ~100 Bytes

//static bzFile FILE; //

typedef struct {
      FILE*     handle;
      char      buf[BZ_MAX_UNUSED];
      int32_t   bufN;
      bool      writing;
      bz_stream strm;
      int32_t     lastErr;
      bool      initialisedOk;
} bzFile;


void BZ2_bzDecompressInit(bz_stream* stream, DState* state) {
   state->strm                  = stream;
   state->state                 = BZ_X_MAGIC_1;
   state->bsLive                = 0;
   state->bsBuff                = 0;
   state->calculatedCombinedCRC = 0;
   state->smallDecompress       = false;
   state->ll4                   = NULL;
   state->ll16                  = NULL;
   state->tt                    = NULL;
   state->currBlockNo           = 0;
   state->verbosity             = 0;

   stream->state            = state;
   stream->total_in_lo32    = 0;
   stream->total_in_hi32    = 0;
   stream->total_out_lo32   = 0;
   stream->total_out_hi32   = 0;
}

BZFILE* BZ2_bzReadOpen(bzFile* bzFile, DState* state, FILE* f) {
    int     ret;
    bzFile->initialisedOk = false;
    bzFile->handle        = f;
    bzFile->bufN          = 0;
    bzFile->writing       = false;
    bzFile->strm.bzalloc  = NULL;
    bzFile->strm.bzfree   = NULL;
    bzFile->strm.opaque   = NULL;

    BZ2_bzDecompressInit(&(bzFile->strm), state);
    bzFile->strm.avail_in = bzFile->bufN;
    bzFile->strm.next_in  = bzFile->buf;

    bzFile->initialisedOk = true;
    return bzFile;
}

int BZ2_bzRead(int* bzerror, BZFILE* b, void* buf, int len) {
   Int32   n, ret;
   bzFile* bzf = (bzFile*)b;

   BZ_SETERR(BZ_OK);

   if (bzf == NULL || buf == NULL || len < 0)
      { BZ_SETERR(BZ_PARAM_ERROR); return 0; };

   if (bzf->writing)
      { BZ_SETERR(BZ_SEQUENCE_ERROR); return 0; };

   if (len == 0)
      { BZ_SETERR(BZ_OK); return 0; };

   bzf->strm.avail_out = len;
   bzf->strm.next_out = buf;

   while (True) {

      if (ferror(bzf->handle))
         { BZ_SETERR(BZ_IO_ERROR); return 0; };

      if (bzf->strm.avail_in == 0 && !myfeof(bzf->handle)) {
         n = fread ( bzf->buf, sizeof(UChar),
                     BZ_MAX_UNUSED, bzf->handle );
         if (ferror(bzf->handle))
            { BZ_SETERR(BZ_IO_ERROR); return 0; };
         bzf->bufN = n;
         bzf->strm.avail_in = bzf->bufN;
         bzf->strm.next_in = bzf->buf;
      }

      ret = BZ2_bzDecompress ( &(bzf->strm) );

      if (ret != BZ_OK && ret != BZ_STREAM_END)
         { BZ_SETERR(ret); return 0; };

      if (ret == BZ_OK && myfeof(bzf->handle) &&
          bzf->strm.avail_in == 0 && bzf->strm.avail_out > 0)
         { BZ_SETERR(BZ_UNEXPECTED_EOF); return 0; };

      if (ret == BZ_STREAM_END)
         { BZ_SETERR(BZ_STREAM_END);
           return len - bzf->strm.avail_out; };
      if (bzf->strm.avail_out == 0)
         { BZ_SETERR(BZ_OK); return len; };

   }

   return 0; /*not reached*/
}
