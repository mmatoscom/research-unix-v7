/*
 * Accounting structures
 */

typedef	u16 comp_t;	/* "floating pt": 3 bits base 8 exp, 13 bits fraction */
struct	acct
{
	char	ac_comm[10];		/* Accounting command name */
	comp_t	ac_utime;		/* Accounting user time */
	comp_t	ac_stime;		/* Accounting system time */
	comp_t	ac_etime;		/* Accounting elapsed time */
	time_t	ac_btime;		/* Beginning time */
	i16	ac_uid;			/* Accounting user ID */
	i16	ac_gid;			/* Accounting group ID */
	i16	ac_mem;			/* average memory usage */
	comp_t	ac_io;			/* number of disk IO blocks */
	dev_t	ac_tty;			/* control typewriter */
	char	ac_flag;		/* Accounting flag */
};

extern	struct	acct	acctbuf;
extern	struct	inode	*acctp;		/* inode of accounting file */

#define	AFORK	01		/* has executed fork, but no exec */
#define	ASU	02		/* used super-user privileges */
