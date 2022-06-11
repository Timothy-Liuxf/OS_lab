# lab4

---

2019011008

无92 刘雪枫

---

## 目录

[TOC]

## 新增代码

切换到分支 `ch6` 编写代码



### Merge `ch5` 与代码修正

将 `ch5` 的代码 merge 到 `ch6`。运行 `ch5_usertest` 发现 `spawn` 调用存在问题。因此做出修改：  

`spawn` 调用改为根据文件路径寻找可执行文件。在 `sys_spawn` 中，将传入的用户态文件名和命令行参数复制进内核态：  

```c
uint64 sys_spawn(uint64 path, uint64 uargv)
{
	// TODO: your job is to complete the sys call
	struct proc *p = curr_proc();
	char name[MAX_STR_LEN];
	copyinstr(p->pagetable, name, path, MAX_STR_LEN);
	uint64 arg;
	static char strpool[MAX_ARG_NUM][MAX_STR_LEN];
	char *argv[MAX_ARG_NUM];
	int i;
	for (i = 0; uargv && (arg = fetchaddr(p->pagetable, uargv));
	     uargv += sizeof(char *), i++) {
		copyinstr(p->pagetable, (char *)strpool[i], arg, MAX_STR_LEN);
		argv[i] = (char *)strpool[i];
	}
	argv[i] = NULL;
	debugf("sys_spawn %s\n", name);
	return spawn(name, (char **)argv);
}
```

在 `spawn` 中，增加初始化 `stdio` 和压入命令行参数的操作。  

```c
int spawn(char *path, char **argv)
{
	infof("spawn : %s\n", path);
	struct inode *ip;
	if ((ip = namei(path)) == 0) {
		errorf("invalid file name %s", path);
		return -1;
	}
	struct proc *np = allocproc();
	if (np == NULL) {
		panic("In spawn: allocproc failed!\n");
	}

	bin_loader(ip, np);
	np->parent = curr_proc();
	init_stdio(np);
	add_task(np);
	iput(ip);
	push_argv(np, argv);
	return np->pid;
}
```

然后运行 `ch5_usertest` 可以正常运行。  



### 增加链接计数

减少 `inode` 和 `dinode` 的一个 `pad`，增加一个链接计数 `nlink`：  

```diff
 // os/fs.h
 struct dinode {
 	short type;
-	short pad[3];
+	short pad[2];
+	short nlink;
 	uint size;
 	uint addrs[NDIRECT + 1];
 };
 
 // os/file.h
 struct inode {
 	/* ... */
 	uint addrs[NDIRECT + 1];
+	short nlink;
 };
 
 // nfs/fs.h
  struct dinode {
 	short type; // File type
-	short pad[3];
+	short pad[2];
+	short nlink;
 	uint size; // Size of file (bytes)
 	uint addrs[NDIRECT + 1]; // Data block addresses
 };
```



### 更新链接计数

现在生成虚拟磁盘文件的代码中初始化硬链接计数为 1：

```diff
// nfs/fs.c
uint ialloc(ushort type) {
 	/* ... */
 	din.size = xint(0);
+	din.nlink = 1;
 	/* ... */
 }
```

  在 os 文件夹中，在创建文件（`create`）中将文件加入到目录后加入初始化 `nlink` 的代码：  

```diff
 static struct inode *create(char *path, short type)
 {
 	/* ... */
 	if (dirlink(dp, path, ip->inum) < 0)
 		panic("create: dirlink");
+	ip->nlink = 1;
 	iput(dp);
 	return ip;
 }
```

读取 `inode` 时，增加对 `nlink` 的读取：  

```diff
 void ivalid(struct inode *ip) {
 		/* ... */
 		dip = (struct dinode *)bp->data + ip->inum % IPB;
 		ip->type = dip->type;
 		ip->size = dip->size;
+		ip->nlink = dip->nlink;
 		memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
 		brelse(bp);
 		ip->valid = 1;
 		/* ... */
 }
```

在减少 `inode` 计数时，修改判断条件，当 `nlink` 为 0 时再删除文件：  

```diff
 void iput(struct inode *ip)
 {
-	if (ip->ref == 1 && ip->valid && 0 /*&& ip->nlink == 0*/) {
+	if (ip->ref == 1 && ip->valid && ip->nlink == 0) {
 	/* ... */
 }
```



### 实现 `linkat`

在系统调用 `sys_linkat` 中，将传入字符串复制到内核态，再传给 `linkat` 处理。在代码中注意到，文件名最长为 `DIRSIZ`，因此缓冲区取 `DIRSIZ` 即可：  

```c
int sys_linkat(int olddirfd, uint64 oldpath, int newdirfd, uint64 newpath, uint64 flags)
{
	if (oldpath == 0 || newpath == 0) {
		return -1;
	}
	struct proc *p = curr_proc();
	char oldpathbuf[DIRSIZ], newpathbuf[DIRSIZ];
	memset((void *)oldpathbuf, 0, sizeof(oldpathbuf));
	memset((void *)newpathbuf, 0, sizeof(newpathbuf));
	if (copyinstr(p->pagetable, oldpathbuf, oldpath, DIRSIZ) == -1 ||
	    copyinstr(p->pagetable, newpathbuf, newpath, DIRSIZ) == -1) {
		errorf("In sys_linkat: invalid virtual address!");
		return -1;
	}
	return linkat(olddirfd, oldpathbuf, newdirfd, newpathbuf, flags);
}
```

在 `linkat` 中，先检查参数合法性（例如原链接与新链接名字相同），再进行链接。连接时，先获取原链接的 `inode`，再调用 `dirlink` 将新链接加到根目录中，最后增加 `nlink` 计数：  

```c
int linkat(int olddirfd, char *oldpath, int newdirfd, char *newpath, int flags)
{
	(void)olddirfd; (void)newdirfd; (void)flags;

	int ret = 0;
	struct inode *dp, *oldip, *newip;

	if (strncmp(oldpath, newpath, DIRSIZ) == 0) {
		errorf("In linkat: Same link!");
		ret = -1;
		goto quit;
	}

	dp = root_dir();

	if ((oldip = dirlookup(dp, oldpath, NULL)) == NULL) {
		errorf("In linkat: Old file not exists!");
		ret = -1;
		goto release_dp;
	}
	ivalid(oldip);

	if ((newip = dirlookup(dp, newpath, NULL)) != NULL) {
		errorf("In linkat: New file exists!");
		iput(newip);
		ret = -1;
		goto release_old_ip;
	}

	if (dirlink(dp, newpath, oldip->inum) != 0) {
		errorf("In linkat: dirlink failed!");
		ret = -1;
		goto release_old_ip;
	}
	++oldip->nlink;

release_old_ip:
	iput(oldip);
release_dp:
	iput(dp);
quit:
	return ret;
}
```



### 实现 `unlinkat`

在 `sys_unlinkat` 中，将传入字符串拷贝进内核态，再转给 `unlinkat` 函数：  

```c
int sys_unlinkat(int dirfd, uint64 name, uint64 flags)
{
	struct proc *p = curr_proc();
	char namebuf[DIRSIZ];
	memset(namebuf, 0, sizeof(namebuf));
	if (copyinstr(p->pagetable, namebuf, name, DIRSIZ) == -1) {
		errorf("In sys_unlinkat: invalid virtual address!");
		return -1;
	}
	return unlinkat(dirfd, namebuf, flags);
}
```

该函数中，先调用 `dirunlink` 将文件从目录项中删除，再减少链接计数，并调用 `iupdate` 将新链接数写入磁盘。在 `iput` 中，若链接数和引用数均为零会删除文件。  

```c
int unlinkat(int dirfd, char *path, int flags)
{
	(void)dirfd; (void)flags;
	int ret = 0;
	struct inode *dp, *ip;
	dp = root_dir();
	if ((ip = dirlookup(dp, path, NULL)) == NULL) {
		errorf("In unlinkat: path not exist!");
		ret = -1;
		goto quit;
	}
	ivalid(ip);

	if (dirunlink(dp, path) != 0) {
		errorf("In unlinkat: dirunlink failed!");
		ret = -1;
		goto release_ip;
	}
	--ip->nlink;
	iupdate(ip);

release_ip:
	iput(ip);
quit:
	return ret;
}
```

其中，`dirunlink` 需要在磁盘中遍历目录项，如遇到该文件名的目录项，则删除之：  

```c
int dirunlink(struct inode *dp, char *name)
{
	int off;
	struct inode *ip;
	struct dirent de;
	if ((ip = dirlookup(dp, name, NULL)) == 0) {
		return -1;
	}
	iput(ip);

	for (off = 0; off < dp->size; off += sizeof(de)) {		// 遍历目录 dp 的目录项
		if (readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
			panic("dirunlink read");
		if (de.inum != 0 && strncmp(name, de.name, DIRSIZ) == 0) {	// 若找到了该文件名的目录项，删除之
			de.inum = 0;
			if (writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
				panic("dirlink");
			return 0;
		}
	}
	return -1;
}
```



### 实现 `fstat`

先根据 `user` 中的代码编写一个内存布局完全相同的 `Stat` 结构体：  

```c
typedef struct Stat {
	uint64 dev;
	uint64 ino;
	uint32 mode;
	uint32 nlink;
	uint64 pad[7];
} Stat;
```

在 `sys_fstat` 中，先转给 `fstat` 函数处理，获取文件信息到本地的缓冲区 `statbuf` 中，再将 `statbuf` 拷贝到用户态的内存：  

```c
int sys_fstat(int fd, uint64 stat)
{
	// TODO: your job is to complete the syscall
	struct Stat statbuf;
	struct proc *p = curr_proc();
	if (fstat(fd, &statbuf) == -1) {
		return -1;
	}
	if (copyout(p->pagetable, stat, (char *)&statbuf, sizeof(struct Stat)) == -1) {
		errorf("In sys_fstat: invalid va!");
		return -1;
	}
	return 0;
}
```

在 `fstat` 中，先检查 `fd` 的合法性（越界或者未打开的文件），再获取文件结构指针并获取相应信息。根据 `user` 中的代码，可知用户程序中，使用 `0x040000` 代表目录，`0x100000` 代表文件：  

```c
#define USER_DIR 0x040000
#define USER_FILE 0x100000

int fstat(int fd, struct Stat *st)
{
	struct proc *p = curr_proc();
	if (fd < 0 || fd >= sizeof(p->files) / sizeof(p->files[0])) {
		errorf("In fstat: fd [%d] overflow!", fd);
		return -1;
	}
	struct file *fp = p->files[fd];
	if (fp == NULL || fp->ref == 0 || fp->type == FD_NONE) {
		errorf("In fstat: fd [%d] not open!", fd);
		return -1;
	}
	st->dev = fp->ip->dev;
	st->ino = fp->ip->inum;
	st->mode =	fp->ip->type == T_FILE ? USER_FILE :
				fp->ip->type == T_DIR  ? USER_DIR : 0;
	st->nlink = fp->ip->nlink;
	return 0;
}
```



### 文件名空位置零

由于本实验中，文件名最大取 `DIRSIZ` 大小，因此当文件名不足该大小时，约定将空位置零。因此当打开文件时，应将文件名空位置零。需要修改 `sys_openat` 函数：  

```diff
 uint64 sys_openat(uint64 va, uint64 omode, uint64 _flags)
 {
 	struct proc *p = curr_proc();
 	char path[200];
+	memset(path, 0, sizeof(path));
 	copyinstr(p->pagetable, path, va, 200);
 	return fileopen(path, omode);
 }
```



## 运行结果

运行命令：  

```shell
$ make test BASE=0 INIT_PROC=ch6_usertest
```

得到如下结果：  

```
[rustsbi] RustSBI version 0.1.1
.______       __    __      _______.___________.  _______..______   __
|   _  \     |  |  |  |    /       |           | /       ||   _  \ |  |
|  |_)  |    |  |  |  |   |   (----`---|  |----`|   (----`|  |_)  ||  |
|      /     |  |  |  |    \   \       |  |      \   \    |   _  < |  |
|  |\  \----.|  `--'  |.----)   |      |  |  .----)   |   |  |_)  ||  |
| _| `._____| \______/ |_______/       |__|  |_______/    |______/ |__|

[rustsbi] Platform: QEMU (Version 0.1.0)
[rustsbi] misa: RV64ACDFIMSU
[rustsbi] mideleg: 0x222
[rustsbi] medeleg: 0xb1ab
[rustsbi-dtb] Hart count: cluster0 with 1 cores
[rustsbi] Kernel entry: 0x80200000
hello world!
/* ... */
ch6 Usertests passed!
[PANIC 1] os/proc.c:153: all app are over!
```

可以看到程序正常运行，说明编写的代码正确  



## 思考题

### ch6

#### 1 

Q：在我们的文件系统中，root inode 起着什么作用？如果 root inode 中的内容损坏了，会发生什么？  

A：我们的文件系统中，root inode 是根目录的 inode，一切文件的搜索都需要从 root inode 开始。而且由于我们的文件系统只含一个目录 root，因此 root inode 也含有所有文件的目录项。如果 root inode 的内容损坏，则会导致无法按文件名找到文件，甚至可能无法创建新文件。  

### ch7

#### 1

Q：举出使用 pipe 的一个实际应用的例子  

A：当实现统计一个文件，例如 `main.c` 的行数时，可以使用 `cat main.c | wc -l`，即将 `cat` 的标准输出和 `wc` 的标准输入使用管道连接。`cat` 用于将文件的内容输出，`wc` 接收 `cat` 的输出作为输入，统计内容的行数。  

#### 2

Q：如果需要在多个进程间互相通信，则需要为每一对进程建立一个管道，非常繁琐，请设计一个更易用的多进程通信机制。  
A：可以使用共享内存机制。多个进程共享一块内存，通过对该共享内存的读写实现进程间通信。  

