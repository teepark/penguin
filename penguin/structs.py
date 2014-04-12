import collections


itimerspec = collections.namedtuple("itimerspec", "it_value it_interval")
siginfo = collections.namedtuple("siginfo", "ssi_signum ssi_code")
ipc_perm = collections.namedtuple("ipc_perm",
        "key uid gid cuid cgid mode seq")
msqid_ds = collections.namedtuple("msqid_ds",
        "msg_perm msg_stime msg_rtime msg_ctime msg_cbytes msg_qnum " +
        "msg_qbytes msg_lspid msg_lrpid")
