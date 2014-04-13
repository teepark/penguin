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
msg_info = collections.namedtuple("msg_info",
        "msg_qbytes perm_uid perm_gid perm_mode")
sem_info = collections.namedtuple("sem_setinfo",
        "perm_uid perm_gid perm_mode")
sembuf = collections.namedtuple("sembuf", "sem_num sem_op sem_flg")
