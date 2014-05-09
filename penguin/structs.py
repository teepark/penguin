import collections


itimerspec = collections.namedtuple("itimerspec", "it_value it_interval")
siginfo = collections.namedtuple("siginfo", "ssi_signum ssi_code")
ipc_perm = collections.namedtuple("ipc_perm",
        "key uid gid cuid cgid mode seq")
msqid_ds = collections.namedtuple("msqid_ds",
        "msg_perm msg_stime msg_rtime msg_ctime msg_cbytes msg_qnum " +
        "msg_qbytes msg_lspid msg_lrpid")
semid_ds = collections.namedtuple("semid_ds",
        "sem_perm sem_otime sem_ctime sem_nsems")
shmid_ds = collections.namedtuple("shmid_ds",
        "shm_perm shm_segsz shm_atime shm_dtime shm_ctime shm_cpid shm_lpid " +
        "shm_nattch")
msg_setinfo = collections.namedtuple("msg_setinfo",
        "msg_qbytes perm_uid perm_gid perm_mode")
sem_setinfo = collections.namedtuple("sem_setinfo",
        "perm_uid perm_gid perm_mode")
shm_setinfo = collections.namedtuple("shm_setinfo",
        "perm_uid perm_gid perm_mode")
sembuf = collections.namedtuple("sembuf", "sem_num sem_op sem_flg")
msginfo = collections.namedtuple("msginfo",
        "msgpool msgmap msgmax msgmnb msgmni msgssz msgtql msgseg")
seminfo = collections.namedtuple("seminfo",
        "semmap semmni semmns semmnu semmsl semopm semume semusz semvmx semaem")
shm_info = collections.namedtuple("shm_info",
        "used_ids shm_tot shm_rss shm_swp")

mq_attr = collections.namedtuple("mq_attr",
        "mq_flags mq_maxmsg mq_msgsize mq_curmsgs")

inotify_event = collections.namedtuple("inotify_event", "wd mask cookie name")
