#ifndef _FS_H
#define _FS_H

#include <sys/types.h>

#define READ 0
#define WRITE 1
#define READA 2		/* read-ahead - don't pause */
#define WRITEA 3	/* "write-ahead" - silly, but somewhat useful */


void buffer_init(unsigned long buffer_end);

#define MAJOR(a) (((unsigned)(a))>>8)
#define MINOR(a) ((a)&0xff)

#define NR_HASH 307
#define NR_BUFFERS nr_buffers
#define BLOCK_SIZE 1024

struct buffer_head {
	char * b_data;			    /* pointer to data block (1024 bytes) */
	unsigned long b_blocknr;	/* block number */
	unsigned short b_dev;		/* device (0 = free) */
	unsigned char b_uptodate;
	unsigned char b_dirt;		/* 0-clean,1-dirty */
	unsigned char b_count;		/* users using this block */
	unsigned char b_lock;		/* 0 - ok, 1 -locked */
	struct task_struct * b_wait;
	struct buffer_head * b_prev;
	struct buffer_head * b_next;
	struct buffer_head * b_prev_free;
	struct buffer_head * b_next_free;
};

extern int nr_buffers;


#endif