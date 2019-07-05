// go run mksysnum.go https://cvsweb.openbsd.org/cgi-bin/cvsweb/~checkout~/src/sys/kern/syscalls.master
// Code generated by the command above; see README.md. DO NOT EDIT.

// +build 386,openbsd

package unix

const (
	SYS_EXIT           = 1   // { void sys_exit(int rval); }
	SYS_FORK           = 2   // { int sys_fork(void); }
	SYS_READ           = 3   // { ssize_t sys_read(int fd, void *buf, size_t nbyte); }
	SYS_WRITE          = 4   // { ssize_t sys_write(int fd, const void *buf, size_t nbyte); }
	SYS_OPEN           = 5   // { int sys_open(const char *path, int flags, ... mode_t mode); }
	SYS_CLOSE          = 6   // { int sys_close(int fd); }
	SYS_GETENTROPY     = 7   // { int sys_getentropy(void *buf, size_t nbyte); }
	SYS___TFORK        = 8   // { int sys___tfork(const struct __tfork *param, size_t psize); }
	SYS_LINK           = 9   // { int sys_link(const char *path, const char *link); }
	SYS_UNLINK         = 10  // { int sys_unlink(const char *path); }
	SYS_WAIT4          = 11  // { pid_t sys_wait4(pid_t pid, int *status, int options, struct rusage *rusage); }
	SYS_CHDIR          = 12  // { int sys_chdir(const char *path); }
	SYS_FCHDIR         = 13  // { int sys_fchdir(int fd); }
	SYS_MKNOD          = 14  // { int sys_mknod(const char *path, mode_t mode, dev_t dev); }
	SYS_CHMOD          = 15  // { int sys_chmod(const char *path, mode_t mode); }
	SYS_CHOWN          = 16  // { int sys_chown(const char *path, uid_t uid, gid_t gid); }
	SYS_OBREAK         = 17  // { int sys_obreak(char *nsize); } break
	SYS_GETDTABLECOUNT = 18  // { int sys_getdtablecount(void); }
	SYS_GETRUSAGE      = 19  // { int sys_getrusage(int who, struct rusage *rusage); }
	SYS_GETPID         = 20  // { pid_t sys_getpid(void); }
	SYS_MOUNT          = 21  // { int sys_mount(const char *type, const char *path, int flags, void *data); }
	SYS_UNMOUNT        = 22  // { int sys_unmount(const char *path, int flags); }
	SYS_SETUID         = 23  // { int sys_setuid(uid_t uid); }
	SYS_GETUID         = 24  // { uid_t sys_getuid(void); }
	SYS_GETEUID        = 25  // { uid_t sys_geteuid(void); }
	SYS_PTRACE         = 26  // { int sys_ptrace(int req, pid_t pid, caddr_t addr, int data); }
	SYS_RECVMSG        = 27  // { ssize_t sys_recvmsg(int s, struct msghdr *msg, int flags); }
	SYS_SENDMSG        = 28  // { ssize_t sys_sendmsg(int s, const struct msghdr *msg, int flags); }
	SYS_RECVFROM       = 29  // { ssize_t sys_recvfrom(int s, void *buf, size_t len, int flags, struct sockaddr *from, socklen_t *fromlenaddr); }
	SYS_ACCEPT         = 30  // { int sys_accept(int s, struct sockaddr *name, socklen_t *anamelen); }
	SYS_GETPEERNAME    = 31  // { int sys_getpeername(int fdes, struct sockaddr *asa, socklen_t *alen); }
	SYS_GETSOCKNAME    = 32  // { int sys_getsockname(int fdes, struct sockaddr *asa, socklen_t *alen); }
	SYS_ACCESS         = 33  // { int sys_access(const char *path, int amode); }
	SYS_CHFLAGS        = 34  // { int sys_chflags(const char *path, u_int flags); }
	SYS_FCHFLAGS       = 35  // { int sys_fchflags(int fd, u_int flags); }
	SYS_SYNC           = 36  // { void sys_sync(void); }
	SYS_STAT           = 38  // { int sys_stat(const char *path, struct stat *ub); }
	SYS_GETPPID        = 39  // { pid_t sys_getppid(void); }
	SYS_LSTAT          = 40  // { int sys_lstat(const char *path, struct stat *ub); }
	SYS_DUP            = 41  // { int sys_dup(int fd); }
	SYS_FSTATAT        = 42  // { int sys_fstatat(int fd, const char *path, struct stat *buf, int flag); }
	SYS_GETEGID        = 43  // { gid_t sys_getegid(void); }
	SYS_PROFIL         = 44  // { int sys_profil(caddr_t samples, size_t size, u_long offset, u_int scale); }
	SYS_KTRACE         = 45  // { int sys_ktrace(const char *fname, int ops, int facs, pid_t pid); }
	SYS_SIGACTION      = 46  // { int sys_sigaction(int signum, const struct sigaction *nsa, struct sigaction *osa); }
	SYS_GETGID         = 47  // { gid_t sys_getgid(void); }
	SYS_SIGPROCMASK    = 48  // { int sys_sigprocmask(int how, sigset_t mask); }
	SYS_SETLOGIN       = 50  // { int sys_setlogin(const char *namebuf); }
	SYS_ACCT           = 51  // { int sys_acct(const char *path); }
	SYS_SIGPENDING     = 52  // { int sys_sigpending(void); }
	SYS_FSTAT          = 53  // { int sys_fstat(int fd, struct stat *sb); }
	SYS_IOCTL          = 54  // { int sys_ioctl(int fd, u_long com, ... void *data); }
	SYS_REBOOT         = 55  // { int sys_reboot(int opt); }
	SYS_REVOKE         = 56  // { int sys_revoke(const char *path); }
	SYS_SYMLINK        = 57  // { int sys_symlink(const char *path, const char *link); }
	SYS_READLINK       = 58  // { ssize_t sys_readlink(const char *path, char *buf, size_t count); }
	SYS_EXECVE         = 59  // { int sys_execve(const char *path, char * const *argp, char * const *envp); }
	SYS_UMASK          = 60  // { mode_t sys_umask(mode_t newmask); }
	SYS_CHROOT         = 61  // { int sys_chroot(const char *path); }
	SYS_GETFSSTAT      = 62  // { int sys_getfsstat(struct statfs *buf, size_t bufsize, int flags); }
	SYS_STATFS         = 63  // { int sys_statfs(const char *path, struct statfs *buf); }
	SYS_FSTATFS        = 64  // { int sys_fstatfs(int fd, struct statfs *buf); }
	SYS_FHSTATFS       = 65  // { int sys_fhstatfs(const fhandle_t *fhp, struct statfs *buf); }
	SYS_VFORK          = 66  // { int sys_vfork(void); }
	SYS_GETTIMEOFDAY   = 67  // { int sys_gettimeofday(struct timeval *tp, struct timezone *tzp); }
	SYS_SETTIMEOFDAY   = 68  // { int sys_settimeofday(const struct timeval *tv, const struct timezone *tzp); }
	SYS_SETITIMER      = 69  // { int sys_setitimer(int which, const struct itimerval *itv, struct itimerval *oitv); }
	SYS_GETITIMER      = 70  // { int sys_getitimer(int which, struct itimerval *itv); }
	SYS_SELECT         = 71  // { int sys_select(int nd, fd_set *in, fd_set *ou, fd_set *ex, struct timeval *tv); }
	SYS_KEVENT         = 72  // { int sys_kevent(int fd, const struct kevent *changelist, int nchanges, struct kevent *eventlist, int nevents, const struct timespec *timeout); }
	SYS_MUNMAP         = 73  // { int sys_munmap(void *addr, size_t len); }
	SYS_MPROTECT       = 74  // { int sys_mprotect(void *addr, size_t len, int prot); }
	SYS_MADVISE        = 75  // { int sys_madvise(void *addr, size_t len, int behav); }
	SYS_UTIMES         = 76  // { int sys_utimes(const char *path, const struct timeval *tptr); }
	SYS_FUTIMES        = 77  // { int sys_futimes(int fd, const struct timeval *tptr); }
	SYS_MINCORE        = 78  // { int sys_mincore(void *addr, size_t len, char *vec); }
	SYS_GETGROUPS      = 79  // { int sys_getgroups(int gidsetsize, gid_t *gidset); }
	SYS_SETGROUPS      = 80  // { int sys_setgroups(int gidsetsize, const gid_t *gidset); }
	SYS_GETPGRP        = 81  // { int sys_getpgrp(void); }
	SYS_SETPGID        = 82  // { int sys_setpgid(pid_t pid, pid_t pgid); }
	SYS_FUTEX          = 83  // { int sys_futex(uint32_t *f, int op, int val, const struct timespec *timeout, uint32_t *g); }
	SYS_UTIMENSAT      = 84  // { int sys_utimensat(int fd, const char *path, const struct timespec *times, int flag); }
	SYS_FUTIMENS       = 85  // { int sys_futimens(int fd, const struct timespec *times); }
	SYS_KBIND          = 86  // { int sys_kbind(const struct __kbind *param, size_t psize, int64_t proc_cookie); }
	SYS_CLOCK_GETTIME  = 87  // { int sys_clock_gettime(clockid_t clock_id, struct timespec *tp); }
	SYS_CLOCK_SETTIME  = 88  // { int sys_clock_settime(clockid_t clock_id, const struct timespec *tp); }
	SYS_CLOCK_GETRES   = 89  // { int sys_clock_getres(clockid_t clock_id, struct timespec *tp); }
	SYS_DUP2           = 90  // { int sys_dup2(int from, int to); }
	SYS_NANOSLEEP      = 91  // { int sys_nanosleep(const struct timespec *rqtp, struct timespec *rmtp); }
	SYS_FCNTL          = 92  // { int sys_fcntl(int fd, int cmd, ... void *arg); }
	SYS_ACCEPT4        = 93  // { int sys_accept4(int s, struct sockaddr *name, socklen_t *anamelen, int flags); }
	SYS___THRSLEEP     = 94  // { int sys___thrsleep(const volatile void *ident, clockid_t clock_id, const struct timespec *tp, void *lock, const int *abort); }
	SYS_FSYNC          = 95  // { int sys_fsync(int fd); }
	SYS_SETPRIORITY    = 96  // { int sys_setpriority(int which, id_t who, int prio); }
	SYS_SOCKET         = 97  // { int sys_socket(int domain, int type, int protocol); }
	SYS_CONNECT        = 98  // { int sys_connect(int s, const struct sockaddr *name, socklen_t namelen); }
	SYS_GETDENTS       = 99  // { int sys_getdents(int fd, void *buf, size_t buflen); }
	SYS_GETPRIORITY    = 100 // { int sys_getpriority(int which, id_t who); }
	SYS_PIPE2          = 101 // { int sys_pipe2(int *fdp, int flags); }
	SYS_DUP3           = 102 // { int sys_dup3(int from, int to, int flags); }
	SYS_SIGRETURN      = 103 // { int sys_sigreturn(struct sigcontext *sigcntxp); }
	SYS_BIND           = 104 // { int sys_bind(int s, const struct sockaddr *name, socklen_t namelen); }
	SYS_SETSOCKOPT     = 105 // { int sys_setsockopt(int s, int level, int name, const void *val, socklen_t valsize); }
	SYS_LISTEN         = 106 // { int sys_listen(int s, int backlog); }
	SYS_CHFLAGSAT      = 107 // { int sys_chflagsat(int fd, const char *path, u_int flags, int atflags); }
	SYS_PLEDGE         = 108 // { int sys_pledge(const char *promises, const char *execpromises); }
	SYS_PPOLL          = 109 // { int sys_ppoll(struct pollfd *fds, u_int nfds, const struct timespec *ts, const sigset_t *mask); }
	SYS_PSELECT        = 110 // { int sys_pselect(int nd, fd_set *in, fd_set *ou, fd_set *ex, const struct timespec *ts, const sigset_t *mask); }
	SYS_SIGSUSPEND     = 111 // { int sys_sigsuspend(int mask); }
	SYS_SENDSYSLOG     = 112 // { int sys_sendsyslog(const char *buf, size_t nbyte, int flags); }
	SYS_UNVEIL         = 114 // { int sys_unveil(const char *path, const char *permissions); }
	SYS_GETSOCKOPT     = 118 // { int sys_getsockopt(int s, int level, int name, void *val, socklen_t *avalsize); }
	SYS_THRKILL        = 119 // { int sys_thrkill(pid_t tid, int signum, void *tcb); }
	SYS_READV          = 120 // { ssize_t sys_readv(int fd, const struct iovec *iovp, int iovcnt); }
	SYS_WRITEV         = 121 // { ssize_t sys_writev(int fd, const struct iovec *iovp, int iovcnt); }
	SYS_KILL           = 122 // { int sys_kill(int pid, int signum); }
	SYS_FCHOWN         = 123 // { int sys_fchown(int fd, uid_t uid, gid_t gid); }
	SYS_FCHMOD         = 124 // { int sys_fchmod(int fd, mode_t mode); }
	SYS_SETREUID       = 126 // { int sys_setreuid(uid_t ruid, uid_t euid); }
	SYS_SETREGID       = 127 // { int sys_setregid(gid_t rgid, gid_t egid); }
	SYS_RENAME         = 128 // { int sys_rename(const char *from, const char *to); }
	SYS_FLOCK          = 131 // { int sys_flock(int fd, int how); }
	SYS_MKFIFO         = 132 // { int sys_mkfifo(const char *path, mode_t mode); }
	SYS_SENDTO         = 133 // { ssize_t sys_sendto(int s, const void *buf, size_t len, int flags, const struct sockaddr *to, socklen_t tolen); }
	SYS_SHUTDOWN       = 134 // { int sys_shutdown(int s, int how); }
	SYS_SOCKETPAIR     = 135 // { int sys_socketpair(int domain, int type, int protocol, int *rsv); }
	SYS_MKDIR          = 136 // { int sys_mkdir(const char *path, mode_t mode); }
	SYS_RMDIR          = 137 // { int sys_rmdir(const char *path); }
	SYS_ADJTIME        = 140 // { int sys_adjtime(const struct timeval *delta, struct timeval *olddelta); }
	SYS_GETLOGIN_R     = 141 // { int sys_getlogin_r(char *namebuf, u_int namelen); }
	SYS_SETSID         = 147 // { int sys_setsid(void); }
	SYS_QUOTACTL       = 148 // { int sys_quotactl(const char *path, int cmd, int uid, char *arg); }
	SYS_NFSSVC         = 155 // { int sys_nfssvc(int flag, void *argp); }
	SYS_GETFH          = 161 // { int sys_getfh(const char *fname, fhandle_t *fhp); }
	SYS_SYSARCH        = 165 // { int sys_sysarch(int op, void *parms); }
	SYS_PREAD          = 173 // { ssize_t sys_pread(int fd, void *buf, size_t nbyte, int pad, off_t offset); }
	SYS_PWRITE         = 174 // { ssize_t sys_pwrite(int fd, const void *buf, size_t nbyte, int pad, off_t offset); }
	SYS_SETGID         = 181 // { int sys_setgid(gid_t gid); }
	SYS_SETEGID        = 182 // { int sys_setegid(gid_t egid); }
	SYS_SETEUID        = 183 // { int sys_seteuid(uid_t euid); }
	SYS_PATHCONF       = 191 // { long sys_pathconf(const char *path, int name); }
	SYS_FPATHCONF      = 192 // { long sys_fpathconf(int fd, int name); }
	SYS_SWAPCTL        = 193 // { int sys_swapctl(int cmd, const void *arg, int misc); }
	SYS_GETRLIMIT      = 194 // { int sys_getrlimit(int which, struct rlimit *rlp); }
	SYS_SETRLIMIT      = 195 // { int sys_setrlimit(int which, const struct rlimit *rlp); }
	SYS_MMAP           = 197 // { void *sys_mmap(void *addr, size_t len, int prot, int flags, int fd, long pad, off_t pos); }
	SYS_LSEEK          = 199 // { off_t sys_lseek(int fd, int pad, off_t offset, int whence); }
	SYS_TRUNCATE       = 200 // { int sys_truncate(const char *path, int pad, off_t length); }
	SYS_FTRUNCATE      = 201 // { int sys_ftruncate(int fd, int pad, off_t length); }
	SYS_SYSCTL         = 202 // { int sys_sysctl(const int *name, u_int namelen, void *old, size_t *oldlenp, void *new, size_t newlen); }
	SYS_MLOCK          = 203 // { int sys_mlock(const void *addr, size_t len); }
	SYS_MUNLOCK        = 204 // { int sys_munlock(const void *addr, size_t len); }
	SYS_GETPGID        = 207 // { pid_t sys_getpgid(pid_t pid); }
	SYS_UTRACE         = 209 // { int sys_utrace(const char *label, const void *addr, size_t len); }
	SYS_SEMGET         = 221 // { int sys_semget(key_t key, int nsems, int semflg); }
	SYS_MSGGET         = 225 // { int sys_msgget(key_t key, int msgflg); }
	SYS_MSGSND         = 226 // { int sys_msgsnd(int msqid, const void *msgp, size_t msgsz, int msgflg); }
	SYS_MSGRCV         = 227 // { int sys_msgrcv(int msqid, void *msgp, size_t msgsz, long msgtyp, int msgflg); }
	SYS_SHMAT          = 228 // { void *sys_shmat(int shmid, const void *shmaddr, int shmflg); }
	SYS_SHMDT          = 230 // { int sys_shmdt(const void *shmaddr); }
	SYS_MINHERIT       = 250 // { int sys_minherit(void *addr, size_t len, int inherit); }
	SYS_POLL           = 252 // { int sys_poll(struct pollfd *fds, u_int nfds, int timeout); }
	SYS_ISSETUGID      = 253 // { int sys_issetugid(void); }
	SYS_LCHOWN         = 254 // { int sys_lchown(const char *path, uid_t uid, gid_t gid); }
	SYS_GETSID         = 255 // { pid_t sys_getsid(pid_t pid); }
	SYS_MSYNC          = 256 // { int sys_msync(void *addr, size_t len, int flags); }
	SYS_PIPE           = 263 // { int sys_pipe(int *fdp); }
	SYS_FHOPEN         = 264 // { int sys_fhopen(const fhandle_t *fhp, int flags); }
	SYS_PREADV         = 267 // { ssize_t sys_preadv(int fd, const struct iovec *iovp, int iovcnt, int pad, off_t offset); }
	SYS_PWRITEV        = 268 // { ssize_t sys_pwritev(int fd, const struct iovec *iovp, int iovcnt, int pad, off_t offset); }
	SYS_KQUEUE         = 269 // { int sys_kqueue(void); }
	SYS_MLOCKALL       = 271 // { int sys_mlockall(int flags); }
	SYS_MUNLOCKALL     = 272 // { int sys_munlockall(void); }
	SYS_GETRESUID      = 281 // { int sys_getresuid(uid_t *ruid, uid_t *euid, uid_t *suid); }
	SYS_SETRESUID      = 282 // { int sys_setresuid(uid_t ruid, uid_t euid, uid_t suid); }
	SYS_GETRESGID      = 283 // { int sys_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid); }
	SYS_SETRESGID      = 284 // { int sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid); }
	SYS_MQUERY         = 286 // { void *sys_mquery(void *addr, size_t len, int prot, int flags, int fd, long pad, off_t pos); }
	SYS_CLOSEFROM      = 287 // { int sys_closefrom(int fd); }
	SYS_SIGALTSTACK    = 288 // { int sys_sigaltstack(const struct sigaltstack *nss, struct sigaltstack *oss); }
	SYS_SHMGET         = 289 // { int sys_shmget(key_t key, size_t size, int shmflg); }
	SYS_SEMOP          = 290 // { int sys_semop(int semid, struct sembuf *sops, size_t nsops); }
	SYS_FHSTAT         = 294 // { int sys_fhstat(const fhandle_t *fhp, struct stat *sb); }
	SYS___SEMCTL       = 295 // { int sys___semctl(int semid, int semnum, int cmd, union semun *arg); }
	SYS_SHMCTL         = 296 // { int sys_shmctl(int shmid, int cmd, struct shmid_ds *buf); }
	SYS_MSGCTL         = 297 // { int sys_msgctl(int msqid, int cmd, struct msqid_ds *buf); }
	SYS_SCHED_YIELD    = 298 // { int sys_sched_yield(void); }
	SYS_GETTHRID       = 299 // { pid_t sys_getthrid(void); }
	SYS___THRWAKEUP    = 301 // { int sys___thrwakeup(const volatile void *ident, int n); }
	SYS___THREXIT      = 302 // { void sys___threxit(pid_t *notdead); }
	SYS___THRSIGDIVERT = 303 // { int sys___thrsigdivert(sigset_t sigmask, siginfo_t *info, const struct timespec *timeout); }
	SYS___GETCWD       = 304 // { int sys___getcwd(char *buf, size_t len); }
	SYS_ADJFREQ        = 305 // { int sys_adjfreq(const int64_t *freq, int64_t *oldfreq); }
	SYS_SETRTABLE      = 310 // { int sys_setrtable(int rtableid); }
	SYS_GETRTABLE      = 311 // { int sys_getrtable(void); }
	SYS_FACCESSAT      = 313 // { int sys_faccessat(int fd, const char *path, int amode, int flag); }
	SYS_FCHMODAT       = 314 // { int sys_fchmodat(int fd, const char *path, mode_t mode, int flag); }
	SYS_FCHOWNAT       = 315 // { int sys_fchownat(int fd, const char *path, uid_t uid, gid_t gid, int flag); }
	SYS_LINKAT         = 317 // { int sys_linkat(int fd1, const char *path1, int fd2, const char *path2, int flag); }
	SYS_MKDIRAT        = 318 // { int sys_mkdirat(int fd, const char *path, mode_t mode); }
	SYS_MKFIFOAT       = 319 // { int sys_mkfifoat(int fd, const char *path, mode_t mode); }
	SYS_MKNODAT        = 320 // { int sys_mknodat(int fd, const char *path, mode_t mode, dev_t dev); }
	SYS_OPENAT         = 321 // { int sys_openat(int fd, const char *path, int flags, ... mode_t mode); }
	SYS_READLINKAT     = 322 // { ssize_t sys_readlinkat(int fd, const char *path, char *buf, size_t count); }
	SYS_RENAMEAT       = 323 // { int sys_renameat(int fromfd, const char *from, int tofd, const char *to); }
	SYS_SYMLINKAT      = 324 // { int sys_symlinkat(const char *path, int fd, const char *link); }
	SYS_UNLINKAT       = 325 // { int sys_unlinkat(int fd, const char *path, int flag); }
	SYS___SET_TCB      = 329 // { void sys___set_tcb(void *tcb); }
	SYS___GET_TCB      = 330 // { void *sys___get_tcb(void); }
)
