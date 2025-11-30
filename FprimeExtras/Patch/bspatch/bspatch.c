// ======================================================================
// \title  bspatch.c
// \author starchmd
// \brief  c file containing modified bspatch implementation
// \copyright Copyright 2003-2005 Colin Percival
// \copyright Copyright (c) 2025 Michael Starch (modified for flight)
// ======================================================================

#include <string.h>
#include <stdint.h>
#include <stdbool.h>


//! \brief status codes for BSPatch operations
//!
//! Defines status codes for BSPatch operations in order to have more explicit
//! error handling and reporting.
typedef enum {
    STATUS_OK,
    STATUS_CORRUPT_PATCH,
    STATUS_CORRUPT_PATCH_HEADER_INSUFFICIENT_DATA,
    STATUS_CORRUPT_PATCH_HEADER_BAD_MAGIC,
    STATUS_FILE_READ_ERROR,
} BSPatchStatus;

//! \brief Expected magic bytes for BSPatch files
const char BINARY_DIFF_MAGIC[8] = "BSDIFF40";



//! \brief External read function prototype
//!
//! Prototype for the external read function used by the bspatch algorithm. This should be defined by the caller and 
//! linked in. `read_pointer` is an opaque pointer that will be passed through to the read function.
//!
//! The length parameter is both an input and output parameter. On input, it specifies the number of bytes to read and
//! on output, it is updated to reflect the actual number of bytes read. If fewer bytes are read than requested, the
//! size pointed to by length should be updated accordingly but a STATUS_OK should still be returned unless an error
//! occurred.
//!
//! \param read_pointer Opaque pointer for read context
//! \param buffer Buffer to read data into
//! \param length Pointer to size of data to read; updated with actual size read
//! \return status code where 0 indicates success, and anything else indicates an error
extern int read(void* read_object, void* buffer, size_t* length);

//! \brief External assertion prototype
//!
//! Externally defined assertion function.
//!
//! \param condition Condition to assert
extern void assert(bool condition);

//! \brief Convert 8-byte buffer to uint64_t
//!
//! Converts an 8-byte buffer in big-endian format to a uint64_t value.
//!
//! \param buffer Pointer to 8-byte buffer
//! \return Converted uint64_t value
uint64_t convert_offset(char* buffer) {
    assert(buffer != NULL);
    uint64_t offset = 0;
    for (size_t i = 0; i < sizeof(uint64_t); i++) {
        offset = (offset << 8) | (unsigned char)buffer[i];
    }
    return offset;
}

//! \brief Verify BSPatch header
//!
//! Verifies the header of a BSPatch patch file by reading from the provided read object and ensuring that the expected
//! magic bytes are present.
//!
//! \param read_object Opaque pointer passed to the read function
//! \param control_length Pointer to store control block length
//! \param diff_length Pointer to store diff block length
//! \param new_size Pointer to store new file size
//! \return status code indicating success or type of failure
BSPatchStatus verify_header(void* read_object, uint64_t* control_length, uint64_t* diff_length, uint64_t* new_size) {
    BSPatchStatus status = STATUS_OK;
    char header[32];
    size_t length = sizeof(header);

    // Validate input parameters
    assert(read_object != NULL);
    assert(control_length != NULL);
    assert(diff_length != NULL);
    assert(new_size != NULL);

    // Read header from patch file and check that the magic bytes are correct
    BSPatchStatus status = read(read_object, header, &length);
    if ((status == STATUS_OK) && (length == sizeof(header))) {
        if (memcmp(header, BINARY_DIFF_MAGIC, sizeof(BINARY_DIFF_MAGIC)) != 0) {
            status = STATUS_CORRUPT_PATCH_HEADER_BAD_MAGIC;
        }
        // Extract lengths from header
        *control_length = convert_offset(header + 8);
        *diff_length = convert_offset(header + 16);
        *new_size = convert_offset(header + 24);
    } else {
        status = STATUS_CORRUPT_PATCH_HEADER_INSUFFICIENT_DATA;
    }
    return status;
}


BSPatchStatus patch(void* read_object) {
    BSPatchStatus status = STATUS_OK;
    uint64_t control_length = 0;
    uint64_t diff_length = 0;
    uint64_t new_size = 0;

    // Verify header and extract lengths
    status = verify_header(read_object, &control_length, &diff_length, &new_size);
    if (status == STATUS_OK) {

    }
    return status;;
}


int main(int argc,char * argv[])
{
	FILE * f, * cpf, * dpf, * epf;
	BZFILE * cpfbz2, * dpfbz2, * epfbz2;
	int cbz2err, dbz2err, ebz2err;
	int fd;
	ssize_t oldsize,newsize;
	ssize_t bzctrllen,bzdatalen;
	u_char header[32],buf[8];
	u_char *old, *new;
	off_t oldpos,newpos;
	off_t ctrl[3];
	off_t lenread;
	off_t i;

	if(argc!=4) errx(1,"usage: %s oldfile newfile patchfile\n",argv[0]);

	/* Open patch file */
	if ((f = fopen(argv[3], "r")) == NULL)
		err(1, "fopen(%s)", argv[3]);

	/*
	File format:
		0	8	"BSDIFF40"
		8	8	X
		16	8	Y
		24	8	sizeof(newfile)
		32	X	bzip2(control block)
		32+X	Y	bzip2(diff block)
		32+X+Y	???	bzip2(extra block)
	with control block a set of triples (x,y,z) meaning "add x bytes
	from oldfile to x bytes from the diff block; copy y bytes from the
	extra block; seek forwards in oldfile by z bytes".
	*/

	/* Read header */
	if (fread(header, 1, 32, f) < 32) {
		if (feof(f))
			errx(1, "Corrupt patch\n");
		err(1, "fread(%s)", argv[3]);
	}

	/* Check for appropriate magic */
	if (memcmp(header, "BSDIFF40", 8) != 0)
		errx(1, "Corrupt patch\n");

	/* Read lengths from header */
	bzctrllen=offtin(header+8);
	bzdatalen=offtin(header+16);
	newsize=offtin(header+24);
	if((bzctrllen<0) || (bzdatalen<0) || (newsize<0))
		errx(1,"Corrupt patch\n");

	/* Close patch file and re-open it via libbzip2 at the right places */
	if (fclose(f))
		err(1, "fclose(%s)", argv[3]);
	if ((cpf = fopen(argv[3], "r")) == NULL)
		err(1, "fopen(%s)", argv[3]);
	if (fseeko(cpf, 32, SEEK_SET))
		err(1, "fseeko(%s, %lld)", argv[3],
		    (long long)32);
	if ((cpfbz2 = BZ2_bzReadOpen(&cbz2err, cpf, 0, 0, NULL, 0)) == NULL)
		errx(1, "BZ2_bzReadOpen, bz2err = %d", cbz2err);
	if ((dpf = fopen(argv[3], "r")) == NULL)
		err(1, "fopen(%s)", argv[3]);
	if (fseeko(dpf, 32 + bzctrllen, SEEK_SET))
		err(1, "fseeko(%s, %lld)", argv[3],
		    (long long)(32 + bzctrllen));
	if ((dpfbz2 = BZ2_bzReadOpen(&dbz2err, dpf, 0, 0, NULL, 0)) == NULL)
		errx(1, "BZ2_bzReadOpen, bz2err = %d", dbz2err);
	if ((epf = fopen(argv[3], "r")) == NULL)
		err(1, "fopen(%s)", argv[3]);
	if (fseeko(epf, 32 + bzctrllen + bzdatalen, SEEK_SET))
		err(1, "fseeko(%s, %lld)", argv[3],
		    (long long)(32 + bzctrllen + bzdatalen));
	if ((epfbz2 = BZ2_bzReadOpen(&ebz2err, epf, 0, 0, NULL, 0)) == NULL)
		errx(1, "BZ2_bzReadOpen, bz2err = %d", ebz2err);

	if(((fd=open(argv[1],O_RDONLY,0))<0) ||
		((oldsize=lseek(fd,0,SEEK_END))==-1) ||
		((old=malloc(oldsize+1))==NULL) ||
		(lseek(fd,0,SEEK_SET)!=0) ||
		(read(fd,old,oldsize)!=oldsize) ||
		(close(fd)==-1)) err(1,"%s",argv[1]);
	if((new=malloc(newsize+1))==NULL) err(1,NULL);

	oldpos=0;newpos=0;
	while(newpos<newsize) {
		/* Read control data */
		for(i=0;i<=2;i++) {
			lenread = BZ2_bzRead(&cbz2err, cpfbz2, buf, 8);
			if ((lenread < 8) || ((cbz2err != BZ_OK) &&
			    (cbz2err != BZ_STREAM_END)))
				errx(1, "Corrupt patch\n");
			ctrl[i]=offtin(buf);
		};

		/* Sanity-check */
		if(newpos+ctrl[0]>newsize)
			errx(1,"Corrupt patch\n");

		/* Read diff string */
		lenread = BZ2_bzRead(&dbz2err, dpfbz2, new + newpos, ctrl[0]);
		if ((lenread < ctrl[0]) ||
		    ((dbz2err != BZ_OK) && (dbz2err != BZ_STREAM_END)))
			errx(1, "Corrupt patch\n");

		/* Add old data to diff string */
		for(i=0;i<ctrl[0];i++)
			if((oldpos+i>=0) && (oldpos+i<oldsize))
				new[newpos+i]+=old[oldpos+i];

		/* Adjust pointers */
		newpos+=ctrl[0];
		oldpos+=ctrl[0];

		/* Sanity-check */
		if(newpos+ctrl[1]>newsize)
			errx(1,"Corrupt patch\n");

		/* Read extra string */
		lenread = BZ2_bzRead(&ebz2err, epfbz2, new + newpos, ctrl[1]);
		if ((lenread < ctrl[1]) ||
		    ((ebz2err != BZ_OK) && (ebz2err != BZ_STREAM_END)))
			errx(1, "Corrupt patch\n");

		/* Adjust pointers */
		newpos+=ctrl[1];
		oldpos+=ctrl[2];
	};

	/* Clean up the bzip2 reads */
	BZ2_bzReadClose(&cbz2err, cpfbz2);
	BZ2_bzReadClose(&dbz2err, dpfbz2);
	BZ2_bzReadClose(&ebz2err, epfbz2);
	if (fclose(cpf) || fclose(dpf) || fclose(epf))
		err(1, "fclose(%s)", argv[3]);

	/* Write the new file */
	if(((fd=open(argv[2],O_CREAT|O_TRUNC|O_WRONLY,0666))<0) ||
		(write(fd,new,newsize)!=newsize) || (close(fd)==-1))
		err(1,"%s",argv[2]);

	free(new);
	free(old);

	return 0;
}
