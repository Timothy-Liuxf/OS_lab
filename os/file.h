#ifndef FILE_H
#define FILE_H

#include "fs.h"
#include "proc.h"
#include "types.h"

#define PIPESIZE (512)
#define FILEPOOLSIZE (NPROC * FD_BUFFER_SIZE)

// in-memory copy of an inode,it can be used to quickly locate file entities on disk
struct inode {
	uint dev; // Device number
	uint inum; // Inode number
	int ref; // Reference count
	int valid; // inode has been read from disk?
	short type; // copy of disk inode
	uint size;
	uint addrs[NDIRECT + 1];
	// LAB 4
	short nlink;
};

// a struct for pipe
struct pipe {
	char data[PIPESIZE]; 
	uint nread; // number of bytes read
	uint nwrite; // number of bytes written
	int readopen; // read fd is still open
	int writeopen; // write fd is still open
};

// file.h
struct file {
	enum { FD_NONE = 0, FD_INODE, FD_STDIO } type;
	int ref; // reference count
	char readable;
	char writable;
	struct inode *ip; // FD_INODE
	uint off;
};

typedef struct Stat {
	uint64 dev;
	uint64 ino;
	uint32 mode;
	uint32 nlink;
	uint64 pad[7];
} Stat;

// A few specific fd
enum {
	STDIN = 0,
	STDOUT = 1,
	STDERR = 2,
};

extern struct file filepool[FILEPOOLSIZE];

void fileclose(struct file *);
struct file *filealloc();
int fileopen(char *, uint64);
uint64 inodewrite(struct file *, uint64, uint64);
uint64 inoderead(struct file *, uint64, uint64);
struct file *stdio_init(int);
int show_all_files();
int linkat(int olddirfd, char *oldpath, int newdirfd, char *newpath, int flags);
int unlinkat(int dirfd, char *path, int flags);
int fstat(int fd, struct Stat *st);

#endif // FILE_H